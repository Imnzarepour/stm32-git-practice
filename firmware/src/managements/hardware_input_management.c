#include "managements/hardware_input_management.h"
#include "managements/hardware_input_management_config.h"
#include "managements/power_management.h"
#include "hardware_proxy/hardware_proxy_digital_io.h"
#include "hardware_proxy/hardware_proxy_analog_io.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"

/* Full contract: docs/02-management-contracts/hardware-input-management.md
 * Output contract: docs/02-management-contracts/hardware-input-io-contract-DRAFT-A.md
 * All tunable numbers used in this file live in
 * managements/hardware_input_management_config.h - see that file for
 * why each one is a placeholder / what it controls. */

#define NUM_DIGITAL_INPUTS 3u /* Must stay in sync with DigitalInputId above; a 4th channel is not assigned yet */
#define NUM_ANALOG_INPUTS  2u /* Must stay in sync with AnalogInputId above */

typedef struct {
    HWProxy_DigitalLine line;
    uint32_t  debounce_ms;
    EventType event_on_active;
    EventType event_on_inactive;
    bool      has_inactive_event; /* SHOCK only has an active edge; ACC/DOOR have both edges */
} DigitalInputConfig;

/* Line numbers (0/1/2) are a placeholder pending real board config - see
 * docs/03-infrastructure-contracts/hardware-proxy.md. */
static const DigitalInputConfig DIGITAL_CONFIG[NUM_DIGITAL_INPUTS] = {
    [DIGITAL_INPUT_ACC]   = { .line = 0, .debounce_ms = ACC_DEBOUNCE_MS,   .event_on_active = EVENT_ACC_ON,        .event_on_inactive = EVENT_ACC_OFF,     .has_inactive_event = true  },
    [DIGITAL_INPUT_DOOR]  = { .line = 1, .debounce_ms = DOOR_DEBOUNCE_MS,  .event_on_active = EVENT_DOOR_OPENED,    .event_on_inactive = EVENT_DOOR_CLOSED, .has_inactive_event = true  },
    [DIGITAL_INPUT_SHOCK] = { .line = 2, .debounce_ms = SHOCK_DEBOUNCE_MS, .event_on_active = EVENT_SHOCK_DETECTED, .event_on_inactive = EVENT_SHOCK_DETECTED, .has_inactive_event = false },
};

typedef struct {
    HWProxy_AnalogChannel channel;
    uint32_t              gain;
    uint32_t              telemetry_interval_ms;
} AnalogInputConfig;

static const AnalogInputConfig ANALOG_CONFIG[NUM_ANALOG_INPUTS] = {
    [ANALOG_INPUT_BATTERY_VOLTAGE]          = { .channel = 0, .gain = BATTERY_VOLTAGE_GAIN,          .telemetry_interval_ms = BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS },
    [ANALOG_INPUT_INTERNAL_BATTERY_VOLTAGE] = { .channel = 1, .gain = INTERNAL_BATTERY_VOLTAGE_GAIN, .telemetry_interval_ms = INTERNAL_BATTERY_VOLTAGE_TELEMETRY_INTERVAL_MS },
};

/* TODO(open-item): the system-wide Telemetry payload format is still an
 * open architectural item (OPEN-ITEMS.md, red, #3 - "final payload
 * format for each Telemetry type is not closed"). This struct is a
 * provisional, local shape for only the analog records this Management
 * produces; it is not a system-wide Telemetry Contract. `id` exists so
 * a consumer can tell the two channels apart once both share the same
 * queue. */
typedef struct {
    AnalogInputId id;
    uint32_t      millivolt;
    uint32_t      timestamp;
} BatteryTelemetryRecord;

/* --- Digital input runtime state --- */
typedef struct {
    DigitalInputValue public_value; /* The stable, published State (Contract) */
    bool              has_baseline; /* Has a first valid sample been captured yet? (avoids a fake Event at startup) */
    bool              candidate_active;
    bool              candidate_value;
    TimeTick          candidate_start;
} DigitalInputRuntime;

static DigitalInputRuntime s_digital[NUM_DIGITAL_INPUTS];

/* --- Analog input runtime state --- */
typedef struct {
    BatteryVoltage public_value;   /* Filtered engineering value (Contract) - always already up to date, never computed on demand */
    uint32_t       filtered_mv;    /* EMA accumulator - same value as public_value.millivolt, kept separately for filter precision */
    bool           telemetry_started;
    TimeTick       last_telemetry_tick;
} AnalogInputRuntime;

static AnalogInputRuntime s_analog[NUM_ANALOG_INPUTS];
static bool               s_low_battery_latched; /* Only tracked for ANALOG_INPUT_BATTERY_VOLTAGE */

static uint32_t s_next_event_id;

/* Pushes one Event onto the shared Event Queue. From there, EventPolicy
 * (shared/event_policy.c, wired into the Super Loop in firmware_main.c)
 * drains the queue and routes each event to SMS/Call/Server/Storage per
 * its policy table - this Management only produces the event, it never
 * decides what happens with it (Detection -> Event, not Event -> Action). */
static void emit_event(EventType type) {
    Event e;
    e.event_type = type;
    e.event_id   = s_next_event_id++;
    e.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetEventQueue(), &e, sizeof(e));
}

/* Time-based, non-blocking debounce/qualification:
 * a raw sample that differs from the current stable State is not accepted
 * immediately; it must stay unchanged as a "candidate" for debounce_ms
 * before it becomes the new State (and, if applicable, produces an Event).
 * A momentary glitch never reaches that point, because the very next
 * differing sample resets the candidate and its timer. */
static void process_digital_input(DigitalInputId id) {
    const DigitalInputConfig* cfg = &DIGITAL_CONFIG[id];
    DigitalInputRuntime*      rt  = &s_digital[id];
    bool raw;

    if (HWProxy_ReadDigitalLine(cfg->line, &raw) != HWPROXY_OK) {
        /* Driver error - mark the value untrustworthy until the next good
         * sample. This is not a system-level Fault (the formal Fault
         * struct is still an open item); it is just this Management's
         * own `valid` flag per its Contract. */
        rt->public_value.valid  = false;
        rt->candidate_active    = false;
        return;
    }

    if (!rt->has_baseline) {
        /* The very first valid sample in this Management's lifetime is
         * accepted as the baseline immediately, with no debounce and no
         * Event - this is not a real transition, just startup discovery. */
        rt->public_value.value     = raw;
        rt->public_value.valid     = true;
        rt->public_value.timestamp = Time_GetTick();
        rt->has_baseline           = true;
        rt->candidate_active       = false;
        return;
    }

    if (raw == rt->public_value.value) {
        /* Back to (or still at) the same stable State. If it had been
         * marked invalid earlier because of a driver error, it becomes
         * valid again right away - no need to re-debounce a value that
         * already matches the known State. */
        if (!rt->public_value.valid) {
            rt->public_value.valid     = true;
            rt->public_value.timestamp = Time_GetTick();
        }
        rt->candidate_active = false;
        return;
    }

    if (!rt->candidate_active || rt->candidate_value != raw) {
        /* Start (or restart) the debounce window for this new value */
        rt->candidate_active = true;
        rt->candidate_value  = raw;
        rt->candidate_start  = Time_GetTick();
        return;
    }

    if (!Time_Elapsed(rt->candidate_start, cfg->debounce_ms)) {
        return; /* Not stable long enough yet */
    }

    /* The candidate has been stable long enough - accept it as the new State */
    rt->public_value.value     = raw;
    rt->public_value.valid     = true;
    rt->public_value.timestamp = Time_GetTick();
    rt->candidate_active       = false;

    if (raw) {
        emit_event(cfg->event_on_active);
    } else if (cfg->has_inactive_event) {
        emit_event(cfg->event_on_inactive);
    }
}

/* Analog qualification: one raw sample is taken every cycle and smoothed
 * with an exponential moving average (EMA) filter so that a single noisy
 * reading cannot swing the engineering value - see
 * hardware_input_management_config.h for why EMA instead of a sample
 * buffer. Because the filter runs every cycle, `public_value` is always
 * already current; nothing needs to be freshly computed at Telemetry
 * time (see process_telemetry below), just read. Low-battery detection
 * (main battery channel only) runs on the filtered value with
 * hysteresis, so noise near the threshold cannot cause a repeated Event. */
static void process_analog_input(AnalogInputId id) {
    const AnalogInputConfig* cfg = &ANALOG_CONFIG[id];
    AnalogInputRuntime*      rt  = &s_analog[id];

    uint16_t raw       = HWProxy_ReadAnalogChannel(cfg->channel);
    uint32_t pin_mv    = (uint32_t)raw * ADC_REF_MILLIVOLT / ADC_MAX_RAW;
    uint32_t sample_mv = pin_mv * cfg->gain;

    if (!rt->public_value.valid) {
        rt->filtered_mv = sample_mv; /* Seed the filter with the first sample, no delay */
    } else {
        rt->filtered_mv = (uint32_t)((int32_t)rt->filtered_mv
            + (((int32_t)sample_mv - (int32_t)rt->filtered_mv) >> BATTERY_EMA_SHIFT));
    }

    rt->public_value.millivolt  = rt->filtered_mv;
    rt->public_value.valid      = true;
    rt->public_value.timestamp  = Time_GetTick();

    if (id != ANALOG_INPUT_BATTERY_VOLTAGE) {
        return; /* Low-battery detection only applies to the main vehicle battery */
    }

    if (rt->filtered_mv < LOW_BATTERY_THRESHOLD_MV) {
        if (!s_low_battery_latched) {
            s_low_battery_latched = true;
            emit_event(EVENT_LOW_BATTERY_DETECTED);
        }
    } else if (rt->filtered_mv >= (LOW_BATTERY_THRESHOLD_MV + LOW_BATTERY_HYSTERESIS_MV)) {
        s_low_battery_latched = false; /* Re-arm for the next detection */
    }
}

/* Periodically (non-blocking, time-based) sends the current filtered
 * value of one analog channel to the Telemetry Queue. In LOW_POWER, the
 * interval is stretched by LOW_POWER_TELEMETRY_RATE_MULTIPLIER - Power
 * Mode is read via the Data path (Power_GetMode(), Power Management is
 * the Provider), never decided here. Digital inputs are deliberately
 * not slowed down by Power Mode - see hardware-input-management.md
 * section 11. */
static void process_telemetry(AnalogInputId id) {
    const AnalogInputConfig* cfg = &ANALOG_CONFIG[id];
    AnalogInputRuntime*      rt  = &s_analog[id];

    uint32_t interval_ms = cfg->telemetry_interval_ms;
    if (Power_GetMode() == POWER_LOW_POWER) {
        interval_ms *= LOW_POWER_TELEMETRY_RATE_MULTIPLIER;
    }

    if (!rt->telemetry_started) {
        rt->last_telemetry_tick = Time_GetTick();
        rt->telemetry_started   = true;
        return;
    }

    if (!Time_Elapsed(rt->last_telemetry_tick, interval_ms)) {
        return;
    }
    rt->last_telemetry_tick = Time_GetTick();

    if (!rt->public_value.valid) {
        return; /* Nothing real to send yet */
    }

    BatteryTelemetryRecord record;
    record.id        = id;
    record.millivolt = rt->public_value.millivolt;
    record.timestamp = Time_GetTick();
    (void)Queue_Push(Queues_GetTelemetryQueue(), &record, sizeof(record));
}

void HardwareInput_Init(void) {
    HWProxy_DigitalIo_Init();
    HWProxy_AnalogIo_Init();

    for (uint32_t i = 0; i < NUM_DIGITAL_INPUTS; i++) {
        s_digital[i] = (DigitalInputRuntime){0};
    }

    for (uint32_t i = 0; i < NUM_ANALOG_INPUTS; i++) {
        s_analog[i] = (AnalogInputRuntime){0};
    }

    s_low_battery_latched  = false;
    s_next_event_id        = 1;
}

void HardwareInput_Process(void) {
    /* Hardware Proxy reads (which are themselves fast register operations
     * under the hood, per driver-layer.md section 17) are synchronous and
     * predictable; reading every input in one cycle does not meaningfully
     * block the Super Loop. */
    for (uint32_t i = 0; i < NUM_DIGITAL_INPUTS; i++) {
        process_digital_input((DigitalInputId)i);
    }

    for (uint32_t i = 0; i < NUM_ANALOG_INPUTS; i++) {
        process_analog_input((AnalogInputId)i);
    }

    for (uint32_t i = 0; i < NUM_ANALOG_INPUTS; i++) {
        process_telemetry((AnalogInputId)i);
    }
}

DigitalInputValue GetDigitalInput(DigitalInputId id) {
    if ((uint32_t)id >= NUM_DIGITAL_INPUTS) {
        DigitalInputValue invalid = {0};
        return invalid;
    }
    return s_digital[id].public_value;
}

BatteryVoltage GetBatteryVoltage(void) {
    return s_analog[ANALOG_INPUT_BATTERY_VOLTAGE].public_value;
}

BatteryVoltage GetInternalBatteryVoltage(void) {
    return s_analog[ANALOG_INPUT_INTERNAL_BATTERY_VOLTAGE].public_value;
}
