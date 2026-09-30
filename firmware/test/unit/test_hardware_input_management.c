#include "unity.h"
#include "managements/hardware_input_management.h"
#include "managements/power_management.h"
#include "Mockhardware_proxy_digital_io.h"
#include "Mockhardware_proxy_analog_io.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"

TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("managements/power_management.c")

/*
 * Per docs/03-infrastructure-contracts/hardware-proxy.md: the only thing
 * that needs to be mocked to test a Management is the Hardware Proxy -
 * never the drivers underneath it (gpio.h/adc.h). Because
 * hardware_input_management.c no longer includes any infrastructure/
 * drivers header at all, mocking the two proxy domains it actually uses
 * (digital I/O, analog I/O) is enough; there is no linker conflict with
 * the real driver implementation because it is never pulled in here.
 *
 * Time Base does not need a mock: its real implementation (a plain tick
 * counter) is fully deterministic and can be driven directly through the
 * real Time_TickIncrement().
 *
 * The line/channel numbers here match the placeholder mapping inside
 * hardware_input_management.c (line = enum value, channel = 0) - that is
 * this Management's current internal design, not a formal Contract; if
 * the real board config changes that mapping, these tests must follow.
 */

#define LINE_ACC         ((HWProxy_DigitalLine)DIGITAL_INPUT_ACC)
#define LINE_DOOR        ((HWProxy_DigitalLine)DIGITAL_INPUT_DOOR)
#define LINE_SHOCK       ((HWProxy_DigitalLine)DIGITAL_INPUT_SHOCK)
#define BATTERY_CHANNEL           ((HWProxy_AnalogChannel)0)
#define INTERNAL_BATTERY_CHANNEL  ((HWProxy_AnalogChannel)1)

#define ACC_DEBOUNCE_MS   100u
#define DOOR_DEBOUNCE_MS  100u
#define SHOCK_DEBOUNCE_MS  50u

/* Mirrors hardware_input_management_config.h so these tests compute the
 * same expected values as production. */
#define BATTERY_VOLTAGE_GAIN 4u
#define BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS          30000u
#define INTERNAL_BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS 30000u
#define LOW_POWER_TELEMETRY_RATE_MULTIPLIER              4u

#define BATTERY_HIGH_RAW  4095u /* -> ~13200mV (gain 4), well above the low-battery threshold */
#define BATTERY_LOW_RAW      0u /* -> 0mV, well below the threshold */

static bool     s_line_state[8];
static uint16_t s_adc_value[8];

/* The Telemetry record shape is a provisional, internal detail of
 * hardware_input_management.c (OPEN-ITEMS.md #3 - the system-wide
 * Telemetry payload format is still open), not part of the public
 * header. This local mirror only exists to decode the raw bytes popped
 * from the queue for assertions; its layout must match the production
 * struct exactly. */
typedef struct {
    AnalogInputId id;
    uint32_t      millivolt;
    uint32_t      timestamp;
} TelemetryRecordMirror;

static HWProxy_Result stub_hwproxy_digital_init(int cmock_num_calls) {
    (void)cmock_num_calls;
    return HWPROXY_OK;
}

static void stub_hwproxy_analog_init(int cmock_num_calls) {
    (void)cmock_num_calls;
}

static HWProxy_Result stub_read_digital_line(HWProxy_DigitalLine line, bool* value, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (line < 0 || line >= 8 || value == NULL) return HWPROXY_INVALID;
    *value = s_line_state[line];
    return HWPROXY_OK;
}

static uint16_t stub_read_analog_channel(HWProxy_AnalogChannel channel, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (channel < 0 || channel >= 8) return 0;
    return s_adc_value[channel];
}

static void set_line(HWProxy_DigitalLine line, bool value) { s_line_state[line] = value; }
static void set_adc(HWProxy_AnalogChannel channel, uint16_t value) { s_adc_value[channel] = value; }

static void advance_time(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

static bool pop_event(Event* out) {
    uint32_t length;
    return Queue_Pop(Queues_GetEventQueue(), out, sizeof(*out), &length);
}

static void establish_inactive_baseline(void) {
    HardwareInput_Process();

    Event dummy;
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&dummy), "The initial baseline must not produce an Event");
}

void setUp(void) {
    Time_Init();
    Queues_Init();
    Power_Init(); /* Defaults to POWER_LOW_POWER (conservative, until the real ACC
                     status is known) - a test that needs NORMAL-rate behavior
                     must explicitly drive ACC ON and call Power_Process() first */

    for (int i = 0; i < 8; i++) {
        s_line_state[i] = false;
        s_adc_value[i] = 0;
    }

    HWProxy_DigitalIo_Init_StubWithCallback(stub_hwproxy_digital_init);
    HWProxy_ReadDigitalLine_StubWithCallback(stub_read_digital_line);
    HWProxy_AnalogIo_Init_StubWithCallback(stub_hwproxy_analog_init);
    HWProxy_ReadAnalogChannel_StubWithCallback(stub_read_analog_channel);

    set_adc(BATTERY_CHANNEL, BATTERY_HIGH_RAW);

    HardwareInput_Init();
}

void tearDown(void) {}

/* --- Startup / baseline --- */

void test_no_event_on_first_process_even_if_input_already_active(void) {
    /* If ACC is already ON at boot, the very first Process() must not be
     * treated as a transition (Unknown State -> Known State); it just
     * records the baseline, with no Event. */
    set_line(LINE_ACC, true);

    HardwareInput_Process();

    Event dummy;
    TEST_ASSERT_FALSE(pop_event(&dummy));

    DigitalInputValue acc = GetDigitalInput(DIGITAL_INPUT_ACC);
    TEST_ASSERT_TRUE(acc.valid);
    TEST_ASSERT_TRUE(acc.value);
}

/* --- Digital debounce / noise rejection --- */

void test_transient_glitch_within_debounce_window_produces_no_event(void) {
    establish_inactive_baseline();

    set_line(LINE_ACC, true);
    HardwareInput_Process(); /* Candidate starts */

    advance_time(30);
    set_line(LINE_ACC, false); /* Noise - back to the previous stable value */
    HardwareInput_Process();

    Event dummy;
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&dummy), "A momentary glitch must not produce an Event");

    DigitalInputValue acc = GetDigitalInput(DIGITAL_INPUT_ACC);
    TEST_ASSERT_TRUE(acc.valid);
    TEST_ASSERT_FALSE_MESSAGE(acc.value, "State must stay at the value it had before the glitch");
}

void test_acc_on_event_fires_only_after_debounce_elapses(void) {
    establish_inactive_baseline();

    set_line(LINE_ACC, true);
    HardwareInput_Process(); /* Candidate starts */

    Event dummy;
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&dummy), "No Event before the debounce window elapses");

    advance_time(ACC_DEBOUNCE_MS - 1);
    HardwareInput_Process();
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&dummy), "No Event one millisecond before the debounce window elapses");

    advance_time(1);
    HardwareInput_Process();

    Event e;
    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_ACC_ON, e.event_type);

    DigitalInputValue acc = GetDigitalInput(DIGITAL_INPUT_ACC);
    TEST_ASSERT_TRUE(acc.valid);
    TEST_ASSERT_TRUE(acc.value);
}

void test_acc_off_event_fires_after_debounce(void) {
    establish_inactive_baseline();

    set_line(LINE_ACC, true);
    HardwareInput_Process();
    advance_time(ACC_DEBOUNCE_MS);
    HardwareInput_Process();

    Event e;
    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_ACC_ON, e.event_type);

    set_line(LINE_ACC, false);
    HardwareInput_Process();
    advance_time(ACC_DEBOUNCE_MS);
    HardwareInput_Process();

    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_ACC_OFF, e.event_type);
}

void test_door_opened_and_closed_events(void) {
    establish_inactive_baseline();

    set_line(LINE_DOOR, true);
    HardwareInput_Process();
    advance_time(DOOR_DEBOUNCE_MS);
    HardwareInput_Process();

    Event e;
    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_DOOR_OPENED, e.event_type);

    set_line(LINE_DOOR, false);
    HardwareInput_Process();
    advance_time(DOOR_DEBOUNCE_MS);
    HardwareInput_Process();

    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_DOOR_CLOSED, e.event_type);
}

void test_shock_detected_only_on_active_edge_not_on_release(void) {
    establish_inactive_baseline();

    set_line(LINE_SHOCK, true);
    HardwareInput_Process();
    advance_time(SHOCK_DEBOUNCE_MS);
    HardwareInput_Process();

    Event e;
    TEST_ASSERT_TRUE(pop_event(&e));
    TEST_ASSERT_EQUAL_INT(EVENT_SHOCK_DETECTED, e.event_type);

    set_line(LINE_SHOCK, false);
    HardwareInput_Process();
    advance_time(SHOCK_DEBOUNCE_MS);
    HardwareInput_Process();

    /* The current catalog only has SHOCK_DETECTED; there is no "released" Event defined */
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&e), "Shock release must not produce an Event");
}

void test_get_digital_input_invalid_id_returns_invalid_default(void) {
    DigitalInputValue v = GetDigitalInput((DigitalInputId)99);
    TEST_ASSERT_FALSE(v.valid);
}

/* --- Battery / analog noise rejection --- */

void test_battery_normal_voltage_produces_no_low_battery_event(void) {
    establish_inactive_baseline();

    for (int i = 0; i < 5; i++) {
        HardwareInput_Process();
    }

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));

    BatteryVoltage battery = GetBatteryVoltage();
    TEST_ASSERT_TRUE(battery.valid);
    TEST_ASSERT_TRUE(battery.millivolt > 11500u);
}

void test_single_sample_battery_spike_is_smoothed_not_propagated(void) {
    establish_inactive_baseline();

    uint32_t before = GetBatteryVoltage().millivolt;

    set_adc(BATTERY_CHANNEL, BATTERY_LOW_RAW);
    HardwareInput_Process(); /* Just one outlier sample */
    set_adc(BATTERY_CHANNEL, BATTERY_HIGH_RAW); /* Immediately back to normal */

    uint32_t after_one_spike = GetBatteryVoltage().millivolt;

    /* The EMA filter must not let a single outlier sample crash the filtered value */
    TEST_ASSERT_TRUE_MESSAGE(after_one_spike > (before / 2), "A single noisy sample must not halve the filtered value");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

void test_battery_sustained_low_voltage_fires_low_battery_event_once(void) {
    establish_inactive_baseline();

    set_adc(BATTERY_CHANNEL, BATTERY_LOW_RAW);
    for (int i = 0; i < 15; i++) {
        HardwareInput_Process();
    }

    int low_battery_count = 0;
    Event e;
    while (pop_event(&e)) {
        if (e.event_type == EVENT_LOW_BATTERY_DETECTED) {
            low_battery_count++;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, low_battery_count, "A sustained voltage drop must produce exactly one Event, not one per cycle");

    BatteryVoltage battery = GetBatteryVoltage();
    TEST_ASSERT_TRUE(battery.millivolt < 11000u);
}

void test_battery_low_battery_event_rearms_after_recovery(void) {
    establish_inactive_baseline();

    set_adc(BATTERY_CHANNEL, BATTERY_LOW_RAW);
    for (int i = 0; i < 15; i++) {
        HardwareInput_Process();
    }
    int first_low_count = 0;
    Event e;
    while (pop_event(&e)) {
        if (e.event_type == EVENT_LOW_BATTERY_DETECTED) first_low_count++;
    }
    TEST_ASSERT_EQUAL_INT(1, first_low_count);

    /* Recovery - must not produce an Event by itself, just re-arm the latch */
    set_adc(BATTERY_CHANNEL, BATTERY_HIGH_RAW);
    for (int i = 0; i < 15; i++) {
        HardwareInput_Process();
    }
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetEventQueue()), "Voltage recovery must not produce an Event on its own");

    /* Drops again - must fire again now that it is re-armed */
    set_adc(BATTERY_CHANNEL, BATTERY_LOW_RAW);
    for (int i = 0; i < 15; i++) {
        HardwareInput_Process();
    }

    int second_low_count = 0;
    while (pop_event(&e)) {
        if (e.event_type == EVENT_LOW_BATTERY_DETECTED) second_low_count++;
    }
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, second_low_count, "After recovery, a new drop must produce a new Event");
}

/* --- Internal battery channel / gain --- */

void test_internal_battery_voltage_uses_its_own_gain(void) {
    set_adc(INTERNAL_BATTERY_CHANNEL, 1000u);
    HardwareInput_Process();

    BatteryVoltage internal_battery = GetInternalBatteryVoltage();
    TEST_ASSERT_TRUE(internal_battery.valid);
    /* raw 1000 -> pin voltage 1000*3300/4095 = 805mV -> * gain 2 = 1610mV */
    TEST_ASSERT_EQUAL_UINT32(1610u, internal_battery.millivolt);
}

void test_main_and_internal_battery_channels_are_independent(void) {
    set_adc(BATTERY_CHANNEL, BATTERY_HIGH_RAW);
    set_adc(INTERNAL_BATTERY_CHANNEL, BATTERY_LOW_RAW);
    HardwareInput_Process();

    TEST_ASSERT_TRUE(GetBatteryVoltage().millivolt > 11500u);
    TEST_ASSERT_EQUAL_UINT32(0u, GetInternalBatteryVoltage().millivolt);
}

/* --- Telemetry cadence --- */

void test_telemetry_pushes_battery_voltage_every_interval(void) {
    /* This test asserts the NORMAL-rate interval, so Power Mode must
     * actually be NORMAL - Power_Init() alone now defaults to LOW_POWER
     * (conservative, until ACC is known), which would silently apply the
     * 4x-slower LOW_POWER rate instead and never fire within this test. */
    set_line(LINE_ACC, true);
    establish_inactive_baseline(); /* First Process() only seeds the telemetry timers, no push yet */
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()), "No telemetry right after startup");

    advance_time(BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS - 1);
    HardwareInput_Process();
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()), "No telemetry one millisecond before the interval elapses");

    advance_time(1);
    HardwareInput_Process();

    TelemetryRecordMirror record;
    uint32_t length;
    TEST_ASSERT_TRUE_MESSAGE(Queue_Pop(Queues_GetTelemetryQueue(), &record, sizeof(record), &length),
                              "A Telemetry record must be pushed once the interval elapses");
    TEST_ASSERT_EQUAL_INT(ANALOG_INPUT_BATTERY_VOLTAGE, record.id);
    TEST_ASSERT_EQUAL_UINT32(13200u, record.millivolt); /* BATTERY_HIGH_RAW with gain 4 */
}

void test_telemetry_does_not_push_twice_within_one_interval(void) {
    establish_inactive_baseline();

    advance_time(BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS);
    HardwareInput_Process();

    TelemetryRecordMirror record;
    uint32_t length;
    /* Both channels share the queue and both just reached their interval - drain everything */
    while (Queue_Pop(Queues_GetTelemetryQueue(), &record, sizeof(record), &length)) { }

    /* A handful more cycles with no time passing must not push again */
    for (int i = 0; i < 5; i++) {
        HardwareInput_Process();
    }
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()), "Telemetry must not repeat before the next interval");
}

void test_internal_battery_telemetry_pushes_on_its_own_interval(void) {
    /* Same reasoning as test_telemetry_pushes_battery_voltage_every_interval:
     * this asserts the NORMAL-rate interval, so Mode must be driven to
     * NORMAL explicitly rather than relying on Power_Init()'s default. */
    set_adc(INTERNAL_BATTERY_CHANNEL, 1000u); /* -> 1610mV, see test_internal_battery_voltage_uses_its_own_gain */
    set_line(LINE_ACC, true);
    establish_inactive_baseline();
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_NORMAL, Power_GetMode());

    advance_time(INTERNAL_BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS);
    HardwareInput_Process();

    bool found_internal = false;
    TelemetryRecordMirror record;
    uint32_t length;
    while (Queue_Pop(Queues_GetTelemetryQueue(), &record, sizeof(record), &length)) {
        if (record.id == ANALOG_INPUT_INTERNAL_BATTERY_VOLTAGE) {
            found_internal = true;
            TEST_ASSERT_EQUAL_UINT32(1610u, record.millivolt);
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_internal, "The internal battery channel must also reach the Telemetry Queue, tagged with its own id");
}

/* --- Power Mode awareness --- */

void test_analog_telemetry_interval_is_slower_in_low_power_mode(void) {
    /* ACC starts inactive (setUp default) and becomes the debounced
     * baseline here; Power_Process() then reads it via the Data path
     * (GetDigitalInput), exactly like the real Super Loop would. */
    establish_inactive_baseline();
    Power_Process();
    TEST_ASSERT_EQUAL_INT(POWER_LOW_POWER, Power_GetMode());

    /* The normal-rate interval alone must NOT be enough in LOW_POWER */
    advance_time(BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS);
    HardwareInput_Process();
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
                              "LOW_POWER must not use the normal (faster) telemetry interval");

    /* The remaining time to reach interval * multiplier */
    advance_time(BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS * (LOW_POWER_TELEMETRY_RATE_MULTIPLIER - 1));
    HardwareInput_Process();
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
                               "Telemetry must fire once the LOW_POWER (4x) interval elapses");
}
