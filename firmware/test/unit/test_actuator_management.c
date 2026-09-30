#include "unity.h"
#include "managements/actuator_management.h"
#include "Mockhardware_proxy_digital_io.h"
#include "Mockreset_request.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"

TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")

/*
 * Per docs/03-infrastructure-contracts/hardware-proxy.md: only the Hardware
 * Proxy domain this Management actually touches (digital I/O) is mocked -
 * never the driver underneath it. System_SubmitResetRequest is declared in
 * shared/reset_request.h (not managements/system_management.h - Actuator
 * Management must never include another Management's header), and is
 * mocked here too since System Management's real body is still a Stub
 * that only satisfies the linker (see system_management.c).
 *
 * Line numbers 4/5 mirror the placeholder mapping inside
 * actuator_management.c (LINE_ENGINE_CUT_OFF/LINE_ALARM) - an internal
 * design choice, not a formal Contract; if real board wiring changes it,
 * these tests must follow.
 */
#define LINE_ENGINE_CUT_OFF ((HWProxy_DigitalLine)4)
#define LINE_ALARM          ((HWProxy_DigitalLine)5)
#define LINE_DOOR_LOCK      ((HWProxy_DigitalLine)6) /* independent pin per Door direction - see OPEN-ITEMS.md 🟡#3 history */
#define LINE_DOOR_UNLOCK    ((HWProxy_DigitalLine)7)

static int           s_write_calls;
static HWProxy_Result s_write_result;
static HWProxy_DigitalLine s_last_written_line;
static bool           s_last_written_value;

static int           s_reset_calls;
static ResetRequest  s_last_reset_request;

static HWProxy_Result stub_write_digital_line(HWProxy_DigitalLine line, bool value, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_write_calls++;
    s_last_written_line  = line;
    s_last_written_value = value;
    return s_write_result;
}

static void stub_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_calls++;
    s_last_reset_request = *request;
}

static bool pop_event(Event* out) {
    uint32_t length;
    return Queue_Pop(Queues_GetEventQueue(), out, sizeof(*out), &length);
}

static bool queue_has_pending_request(void) {
    return !Queue_IsEmpty(Queues_GetActuatorRequestQueue());
}

void setUp(void) {
    Time_Init();
    Queues_Init();

    s_write_calls  = 0;
    s_write_result = HWPROXY_OK;
    s_reset_calls  = 0;

    HWProxy_WriteDigitalLine_StubWithCallback(stub_write_digital_line);
    System_SubmitResetRequest_StubWithCallback(stub_reset_request);

    Actuator_Init();
}

void tearDown(void) {}

/* --- Init / GetState --- */

void test_all_actuators_start_off_and_inactive(void) {
    ActuatorState state = Actuator_GetState(ACTUATOR_ENGINE_CUT_OFF);
    TEST_ASSERT_EQUAL(ACTUATOR_CMD_OFF, state.command);
    TEST_ASSERT_FALSE(state.active);

    state = Actuator_GetState(ACTUATOR_ALARM);
    TEST_ASSERT_FALSE(state.active);

    state = Actuator_GetState(ACTUATOR_DOOR_LOCK);
    TEST_ASSERT_FALSE(state.active);

    state = Actuator_GetState(ACTUATOR_DOOR_UNLOCK);
    TEST_ASSERT_FALSE(state.active);
}

void test_getstate_with_invalid_id_returns_safe_default_not_garbage(void) {
    ActuatorState state = Actuator_GetState((ActuatorId)99);
    TEST_ASSERT_EQUAL(ACTUATOR_CMD_OFF, state.command);
    TEST_ASSERT_FALSE(state.active);
    TEST_ASSERT_EQUAL_UINT32(0, state.timestamp);
}

/* --- SubmitRequest: structural validation --- */

void test_submit_with_invalid_actuator_is_rejected_and_not_queued(void) {
    ActuatorRequest request = { (ActuatorId)99, ACTUATOR_CMD_ON, 1000 };

    bool accepted = Actuator_SubmitRequest(&request);

    TEST_ASSERT_FALSE(accepted);
    TEST_ASSERT_FALSE_MESSAGE(queue_has_pending_request(), "An invalid Request must never reach the queue");

    Event event;
    TEST_ASSERT_TRUE(pop_event(&event));
    TEST_ASSERT_EQUAL(EVENT_ACTUATOR_COMMAND_REJECTED, event.event_type);
}

void test_submit_with_invalid_command_is_rejected(void) {
    ActuatorRequest request = { ACTUATOR_ALARM, (ActuatorCommand)99, 1000 };

    TEST_ASSERT_FALSE(Actuator_SubmitRequest(&request));
    TEST_ASSERT_FALSE(queue_has_pending_request());
}

void test_submit_with_null_request_is_rejected_not_crashed(void) {
    TEST_ASSERT_FALSE(Actuator_SubmitRequest(NULL));
    TEST_ASSERT_FALSE(queue_has_pending_request());
}

void test_submit_with_valid_request_is_accepted_and_queued(void) {
    ActuatorRequest request = { ACTUATOR_ENGINE_CUT_OFF, ACTUATOR_CMD_ON, 1000 };

    bool accepted = Actuator_SubmitRequest(&request);

    TEST_ASSERT_TRUE(accepted);
    TEST_ASSERT_TRUE(queue_has_pending_request());

    Event event;
    TEST_ASSERT_TRUE(pop_event(&event));
    TEST_ASSERT_EQUAL(EVENT_ACTUATOR_COMMAND_ACCEPTED, event.event_type);
}

/* --- Process: physical-output actuators (ENGINE_CUT_OFF, ALARM) --- */

void test_process_with_empty_queue_does_nothing(void) {
    Actuator_Process();

    TEST_ASSERT_EQUAL_INT(0, s_write_calls);
}

void test_process_executes_engine_cut_off_on_via_hardware_proxy(void) {
    ActuatorRequest request = { ACTUATOR_ENGINE_CUT_OFF, ACTUATOR_CMD_ON, 1000 };
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT(1, s_write_calls);
    TEST_ASSERT_EQUAL_INT(LINE_ENGINE_CUT_OFF, s_last_written_line);
    TEST_ASSERT_TRUE(s_last_written_value);

    ActuatorState state = Actuator_GetState(ACTUATOR_ENGINE_CUT_OFF);
    TEST_ASSERT_EQUAL(ACTUATOR_CMD_ON, state.command);
    TEST_ASSERT_TRUE(state.active);
}

void test_process_executes_alarm_deactivate_via_hardware_proxy(void) {
    ActuatorRequest request = { ACTUATOR_ALARM, ACTUATOR_CMD_DEACTIVATE, 1000 };
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT(1, s_write_calls);
    TEST_ASSERT_EQUAL_INT(LINE_ALARM, s_last_written_line);
    TEST_ASSERT_FALSE(s_last_written_value);

    ActuatorState state = Actuator_GetState(ACTUATOR_ALARM);
    TEST_ASSERT_FALSE(state.active);
}

void test_process_services_exactly_one_request_per_call(void) {
    ActuatorRequest first  = { ACTUATOR_ENGINE_CUT_OFF, ACTUATOR_CMD_ON, 1000 };
    ActuatorRequest second = { ACTUATOR_ALARM, ACTUATOR_CMD_ACTIVATE, 1001 };
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&first));
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&second));

    Actuator_Process();
    TEST_ASSERT_EQUAL_INT(1, s_write_calls);
    TEST_ASSERT_TRUE_MESSAGE(queue_has_pending_request(), "Second Request must still be waiting - Process() must not drain the whole queue in one call");

    Actuator_Process();
    TEST_ASSERT_EQUAL_INT(2, s_write_calls);
    TEST_ASSERT_FALSE(queue_has_pending_request());
}

/* --- Process: Door Lock/Unlock drive real GPIO output too (explicit user
 * decision - OPEN-ITEMS.md 🔴 critical item). Each now has its OWN
 * independent physical pin (OPEN-ITEMS.md 🟡#3 - flipped back to 4
 * independent outputs on 2026-09-19 after binary-protocol-final.md's
 * Hekar-proposed 4-bit Relay Mask conflicted with the earlier "shared
 * pin" decision). Both behave exactly like every other Actuator now -
 * ActuatorCommand (ON/ACTIVATE vs OFF/DEACTIVATE) decides the pin value,
 * not the ActuatorId. --- */

void test_door_lock_drives_its_own_pin(void) {
    ActuatorRequest request = { ACTUATOR_DOOR_LOCK, ACTUATOR_CMD_ACTIVATE, 1000 };
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT(1, s_write_calls);
    TEST_ASSERT_EQUAL(LINE_DOOR_LOCK, s_last_written_line);
    TEST_ASSERT_TRUE_MESSAGE(s_last_written_value, "ACTIVATE must drive the pin high, like every other Actuator");

    ActuatorState state = Actuator_GetState(ACTUATOR_DOOR_LOCK);
    TEST_ASSERT_EQUAL(ACTUATOR_CMD_ACTIVATE, state.command);
    TEST_ASSERT_TRUE(state.active);
}

void test_door_unlock_drives_a_different_independent_pin(void) {
    ActuatorRequest request = { ACTUATOR_DOOR_UNLOCK, ACTUATOR_CMD_DEACTIVATE, 1000 };
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT(1, s_write_calls);
    TEST_ASSERT_EQUAL_MESSAGE(LINE_DOOR_UNLOCK, s_last_written_line,
        "Door Lock and Door Unlock must drive INDEPENDENT physical pins, not a shared one");
    TEST_ASSERT_FALSE_MESSAGE(s_last_written_value, "DEACTIVATE must drive the pin low, like every other Actuator");

    ActuatorState state = Actuator_GetState(ACTUATOR_DOOR_UNLOCK);
    TEST_ASSERT_FALSE(state.active);
}

void test_door_lock_output_failure_reports_fault_same_as_other_actuators(void) { /* unchanged by the 3->4 line flip: this only asserts fault/state behavior on write failure, not which pin */
    ActuatorRequest request = { ACTUATOR_DOOR_LOCK, ACTUATOR_CMD_ACTIVATE, 1000 };
    s_write_result = HWPROXY_ERROR;
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));

    Actuator_Process();

    ActuatorState state = Actuator_GetState(ACTUATOR_DOOR_LOCK);
    TEST_ASSERT_FALSE_MESSAGE(state.active, "State must not claim the output changed when the Hardware Proxy write failed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Fault / local recovery / Reset escalation --- */

void test_output_driver_failure_reports_fault_event_and_does_not_update_state(void) {
    s_write_result = HWPROXY_ERROR;

    ActuatorRequest request = { ACTUATOR_ENGINE_CUT_OFF, ACTUATOR_CMD_ON, 1000 };
    Event discarded_accept_event;
    TEST_ASSERT_TRUE(Actuator_SubmitRequest(&request));
    (void)pop_event(&discarded_accept_event); /* discard the ACCEPTED event from Submit */

    Actuator_Process();

    ActuatorState state = Actuator_GetState(ACTUATOR_ENGINE_CUT_OFF);
    TEST_ASSERT_FALSE_MESSAGE(state.active, "A failed physical write must not be reflected as success in State");

    Event event;
    TEST_ASSERT_TRUE(pop_event(&event));
    TEST_ASSERT_EQUAL(EVENT_ACTUATOR_FAULT, event.event_type);
}

void test_single_output_failure_does_not_trigger_reset(void) {
    s_write_result = HWPROXY_ERROR;
    ActuatorRequest request = { ACTUATOR_ALARM, ACTUATOR_CMD_ON, 1000 };
    Actuator_SubmitRequest(&request);

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_calls, "One failure is local recovery territory, not a Reset yet");
}

void test_repeated_consecutive_output_failures_escalate_to_reset_request(void) {
    s_write_result = HWPROXY_ERROR;
    ActuatorRequest request = { ACTUATOR_ALARM, ACTUATOR_CMD_ON, 1000 };

    /* Three consecutive failures on the same Actuator */
    for (int i = 0; i < 3; i++) {
        Actuator_SubmitRequest(&request);
        Actuator_Process();
    }

    TEST_ASSERT_EQUAL_INT(1, s_reset_calls);
    TEST_ASSERT_EQUAL(RESET_SOURCE_ACTUATOR, s_last_reset_request.source);
    TEST_ASSERT_EQUAL(RESET_REASON_RECOVERY_FAILED, s_last_reset_request.reason);
    TEST_ASSERT_EQUAL(RESET_TYPE_SYSTEM_RESET, s_last_reset_request.type);
}

void test_success_after_failure_resets_the_consecutive_failure_counter(void) {
    ActuatorRequest request = { ACTUATOR_ALARM, ACTUATOR_CMD_ON, 1000 };

    s_write_result = HWPROXY_ERROR;
    Actuator_SubmitRequest(&request);
    Actuator_Process();
    Actuator_SubmitRequest(&request);
    Actuator_Process();

    s_write_result = HWPROXY_OK; /* recovers before hitting the threshold */
    Actuator_SubmitRequest(&request);
    Actuator_Process();

    s_write_result = HWPROXY_ERROR;
    Actuator_SubmitRequest(&request);
    Actuator_Process();
    Actuator_SubmitRequest(&request);
    Actuator_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_calls, "The counter must not carry over across an intervening success");
}

void test_invalid_request_popped_from_queue_reports_fault_not_crash(void) {
    /* Defense-in-depth path: a structurally invalid item should never be
     * queued by SubmitRequest, but Process() must not trust the queue
     * blindly either. Push the raw bytes directly to bypass SubmitRequest. */
    ActuatorRequest bad_request = { (ActuatorId)99, ACTUATOR_CMD_ON, 1000 };
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetActuatorRequestQueue(), &bad_request, sizeof(bad_request)));

    Actuator_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_write_calls, "Must never reach the Hardware Proxy with an invalid Actuator id");
}
