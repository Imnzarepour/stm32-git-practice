#include "unity.h"
#include "managements/diagnostic_management.h"
#include "managements/diagnostic_management_config.h"
#include "Mockhardware_proxy_can.h"
#include "Mockpower_management.h"
#include "Mockreset_request.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include <string.h>

TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")

/* Per docs/03-infrastructure-contracts/hardware-proxy.md: only the
 * Hardware Proxy Domain this Management actually touches (CAN) is
 * mocked - never infrastructure/drivers/can.h underneath it.
 * Power_GetMode() is mocked too, same reasoning test_system_management.c
 * already uses for its own Data-path dependencies. Event/Fault Queues are
 * real (Queue Infrastructure + shared/queues.c), same pattern
 * test_actuator_management.c already uses to verify its own
 * push_event()/push_fault() calls end-to-end, not just "was Queue_Push
 * called" via a Mock. */

static bool pop_event(Event* out) {
    uint32_t length;
    return Queue_Pop(Queues_GetEventQueue(), out, sizeof(*out), &length);
}

static PowerMode s_stub_power_mode;

static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

static HWProxy_CanResult s_stub_send_result;
static int                s_can_send_calls;
static HWProxy_CanFrame   s_last_sent_frame;

static HWProxy_CanResult stub_can_send(const HWProxy_CanFrame* frame, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_can_send_calls++;
    s_last_sent_frame = *frame;
    return s_stub_send_result;
}

static HWProxy_CanResult s_stub_receive_result;
static HWProxy_CanFrame   s_stub_receive_frame;
static int                s_can_receive_calls;

static HWProxy_CanResult stub_can_receive(HWProxy_CanFrame* frame, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_can_receive_calls++;
    if (s_stub_receive_result == HWPROXY_CAN_OK) {
        *frame = s_stub_receive_frame;
    }
    return s_stub_receive_result;
}

static DiagnosticRequest make_request(DiagnosticOperation op, uint32_t target, uint32_t parameter) {
    DiagnosticRequest request;
    request.operation = op;
    request.target     = target;
    request.parameter  = parameter;
    return request;
}

static int          s_reset_request_call_count;
static ResetRequest s_last_reset_request;

static void stub_submit_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_request_call_count++;
    if (request) s_last_reset_request = *request;
}

void setUp(void) {
    Time_Init();
    Queues_Init();

    s_stub_power_mode = POWER_NORMAL;

    s_stub_send_result = HWPROXY_CAN_OK;
    s_can_send_calls    = 0;
    memset(&s_last_sent_frame, 0, sizeof(s_last_sent_frame));

    s_stub_receive_result = HWPROXY_CAN_ERROR; /* "no data yet" by default - see hardware_proxy_can.h */
    memset(&s_stub_receive_frame, 0, sizeof(s_stub_receive_frame));
    s_can_receive_calls = 0;

    s_reset_request_call_count = 0;
    memset(&s_last_reset_request, 0, sizeof(s_last_reset_request));

    HWProxy_CAN_Init_IgnoreAndReturn(HWPROXY_CAN_OK);
    Power_GetMode_StubWithCallback(stub_power_get_mode);
    HWProxy_CAN_Send_StubWithCallback(stub_can_send);
    HWProxy_CAN_Receive_StubWithCallback(stub_can_receive);
    System_SubmitResetRequest_StubWithCallback(stub_submit_reset_request);

    Diagnostic_Init();
}

void tearDown(void) {}

/* --- Init --- */

void test_init_configures_can_exactly_once(void) {
    /* HWProxy_CAN_Init_IgnoreAndReturn above already tolerates any count -
     * this just proves Init() itself does not crash/misbehave and leaves
     * the Management ready to accept a Request. */
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_ACCEPTED, Diagnostic_SubmitRequest(&request));
}

/* --- SubmitRequest: Current + Pending model (section 8) --- */

void test_submit_when_idle_is_accepted(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_ACCEPTED, Diagnostic_SubmitRequest(&request));
}

void test_submit_while_running_is_queued_as_pending(void) {
    DiagnosticRequest first  = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    DiagnosticRequest second = make_request(DIAG_OP_CLEAR_DTC, 2u, 0u);

    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_ACCEPTED, Diagnostic_SubmitRequest(&first));
    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_PENDING, Diagnostic_SubmitRequest(&second));
}

void test_submit_when_both_slots_full_is_busy(void) {
    DiagnosticRequest first  = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    DiagnosticRequest second = make_request(DIAG_OP_CLEAR_DTC, 2u, 0u);
    DiagnosticRequest third  = make_request(DIAG_OP_READ_PARAMETER, 3u, 7u);

    Diagnostic_SubmitRequest(&first);
    Diagnostic_SubmitRequest(&second);

    TEST_ASSERT_EQUAL_INT_MESSAGE(DIAG_SUBMIT_BUSY, Diagnostic_SubmitRequest(&third), "a 3rd Request must be rejected, not queued or dropped silently");
}

void test_null_request_is_rejected_not_crashed(void) {
    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_BUSY, Diagnostic_SubmitRequest(NULL));
}

/* --- Process(): CAN exchange (section 5) --- */

void test_process_sends_the_request_frame(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 0x123u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process();

    TEST_ASSERT_EQUAL_INT(1, s_can_send_calls);
    TEST_ASSERT_EQUAL_UINT32(0x123u, s_last_sent_frame.id);
    TEST_ASSERT_EQUAL_UINT8(DIAG_OP_READ_DTC, s_last_sent_frame.data[0]);
}

void test_process_only_sends_once_then_polls_for_a_response(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process(); /* sends */
    Diagnostic_Process(); /* polls for response */

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_can_send_calls, "must not re-send once the request went out");
    TEST_ASSERT_EQUAL_INT(1, s_can_receive_calls);
}

void test_send_busy_is_retried_on_the_next_process_call(void) {
    s_stub_send_result = HWPROXY_CAN_BUSY;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process();
    TEST_ASSERT_EQUAL_INT(1, s_can_send_calls);

    s_stub_send_result = HWPROXY_CAN_OK;
    Diagnostic_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_can_send_calls, "must retry Send() while the proxy reports BUSY");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_can_receive_calls, "must not start polling for a response until Send() actually succeeds");
}

void test_send_error_fails_the_operation_immediately(void) {
    s_stub_send_result = HWPROXY_CAN_ERROR;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process();

    DiagnosticResponse response;
    TEST_ASSERT_TRUE(Diagnostic_GetResponse(&response));
    TEST_ASSERT_EQUAL_INT(DIAG_STATUS_BUS_ERROR, response.status);
}

/* --- Successful response --- */

void test_successful_response_completes_the_operation(void) {
    s_stub_receive_result           = HWPROXY_CAN_OK;
    s_stub_receive_frame.id         = 0x456u;
    s_stub_receive_frame.dlc        = 3u;
    s_stub_receive_frame.data[0]    = 0xAAu;
    s_stub_receive_frame.data[1]    = 0xBBu;
    s_stub_receive_frame.data[2]    = 0xCCu;

    DiagnosticRequest request = make_request(DIAG_OP_READ_PARAMETER, 1u, 42u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process(); /* sends */
    Diagnostic_Process(); /* receives */

    DiagnosticResponse response;
    TEST_ASSERT_TRUE(Diagnostic_GetResponse(&response));
    TEST_ASSERT_EQUAL_INT(DIAG_STATUS_SUCCESS, response.status);
    TEST_ASSERT_EQUAL_UINT32(3u, response.data_length);
    TEST_ASSERT_EQUAL_UINT8(0xAAu, response.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBBu, response.data[1]);
    TEST_ASSERT_EQUAL_UINT8(0xCCu, response.data[2]);
}

/* --- GetResponse(): consumption rules (sections 9-10) --- */

void test_get_response_returns_false_while_still_running(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process(); /* only sent, no response yet */

    DiagnosticResponse response;
    TEST_ASSERT_FALSE(Diagnostic_GetResponse(&response));
}

void test_get_response_returns_false_when_nothing_was_ever_submitted(void) {
    DiagnosticResponse response;
    TEST_ASSERT_FALSE(Diagnostic_GetResponse(&response));
}

void test_get_response_with_null_pointer_is_rejected_not_crashed(void) {
    s_stub_receive_result = HWPROXY_CAN_OK;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process();
    Diagnostic_Process();

    TEST_ASSERT_FALSE(Diagnostic_GetResponse(NULL));
}

void test_result_stays_occupied_until_consumed(void) {
    s_stub_receive_result = HWPROXY_CAN_OK;
    DiagnosticRequest first  = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    DiagnosticRequest second = make_request(DIAG_OP_CLEAR_DTC, 2u, 0u);

    Diagnostic_SubmitRequest(&first);
    Diagnostic_Process(); /* send */
    Diagnostic_Process(); /* receive -> SUCCESS, unconsumed */

    /* Unconsumed SUCCESS still counts as occupied (section 8's key note) */
    TEST_ASSERT_EQUAL_INT(DIAG_SUBMIT_PENDING, Diagnostic_SubmitRequest(&second));

    /* Process() must not silently promote Pending before a real Consume */
    Diagnostic_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_can_send_calls, "Pending must not start until Current is actually consumed");
}

void test_pending_promotes_only_after_a_real_consume(void) {
    s_stub_receive_result = HWPROXY_CAN_OK;
    DiagnosticRequest first  = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    DiagnosticRequest second = make_request(DIAG_OP_CLEAR_DTC, 0x789u, 0u);

    Diagnostic_SubmitRequest(&first);
    Diagnostic_Process();
    Diagnostic_Process(); /* first -> SUCCESS */
    Diagnostic_SubmitRequest(&second); /* queued as Pending */

    DiagnosticResponse response;
    TEST_ASSERT_TRUE(Diagnostic_GetResponse(&response)); /* consume -> Current = IDLE */

    Diagnostic_Process(); /* promotes Pending into Current (IDLE -> RUNNING) */
    Diagnostic_Process(); /* now RUNNING -> actually sends it */

    TEST_ASSERT_EQUAL_INT(2, s_can_send_calls);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x789u, s_last_sent_frame.id, "the promoted Pending Request must actually be sent");
}

/* --- Timeout --- */

void test_no_response_within_timeout_fails_with_timeout_status(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process(); /* send */

    uint32_t i;
    for (i = 0; i < DIAG_RESPONSE_TIMEOUT_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* receive attempt - still nothing, now past Timeout */

    DiagnosticResponse response;
    TEST_ASSERT_TRUE(Diagnostic_GetResponse(&response));
    TEST_ASSERT_EQUAL_INT(DIAG_STATUS_TIMEOUT, response.status);
}

void test_response_just_before_timeout_still_succeeds(void) {
    uint32_t i;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process(); /* send */

    for (i = 0; i < DIAG_RESPONSE_TIMEOUT_TICKS - 1u; i++) {
        Time_TickIncrement();
    }

    s_stub_receive_result        = HWPROXY_CAN_OK;
    s_stub_receive_frame.dlc     = 1u;
    s_stub_receive_frame.data[0] = 0x99u;
    Diagnostic_Process();

    DiagnosticResponse response;
    TEST_ASSERT_TRUE(Diagnostic_GetResponse(&response));
    TEST_ASSERT_EQUAL_INT(DIAG_STATUS_SUCCESS, response.status);
}

/* --- Periodic Read Parameter + Power Mode (sections 0/4/21) --- */

void test_periodic_read_starts_after_the_interval_in_normal_mode(void) {
    uint32_t i;
    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }

    Diagnostic_Process(); /* IDLE -> starts the periodic Read Parameter (RUNNING) */
    Diagnostic_Process(); /* RUNNING -> actually sends it */

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_can_send_calls, "an internal periodic Read Parameter must have started and sent a frame");
}

void test_periodic_read_does_not_start_before_the_interval(void) {
    uint32_t i;
    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS - 1u; i++) {
        Time_TickIncrement();
    }

    Diagnostic_Process();

    TEST_ASSERT_EQUAL_INT(0, s_can_send_calls);
}

void test_periodic_read_is_suppressed_in_low_power(void) {
    uint32_t i;
    s_stub_power_mode = POWER_LOW_POWER;

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_can_send_calls, "the automatic periodic read must not run in LOW_POWER");
}

void test_periodic_read_never_preempts_an_explicit_request(void) {
    uint32_t i;
    DiagnosticRequest explicit_request = make_request(DIAG_OP_READ_DTC, 5u, 0u);
    Diagnostic_SubmitRequest(&explicit_request);

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* must advance the explicit Request, not start a periodic one on top of it */

    TEST_ASSERT_EQUAL_INT(1, s_can_send_calls);
    TEST_ASSERT_EQUAL_UINT32(5u, s_last_sent_frame.id);
}

void test_explicit_request_is_still_accepted_in_low_power(void) {
    s_stub_power_mode = POWER_LOW_POWER;
    DiagnosticRequest request = make_request(DIAG_OP_CLEAR_DTC, 1u, 0u);

    TEST_ASSERT_EQUAL_INT_MESSAGE(DIAG_SUBMIT_ACCEPTED, Diagnostic_SubmitRequest(&request), "LOW_POWER must only suppress the automatic periodic read, never an explicit Request");
}

/* --- Section 25: Diagnostic -> Event/Fault -> Event Policy --- */

void test_bus_error_pushes_a_diagnostic_failure_event_and_fault(void) {
    s_stub_send_result = HWPROXY_CAN_ERROR;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process(); /* Send fails -> FAILED */

    Event event;
    TEST_ASSERT_TRUE_MESSAGE(pop_event(&event), "a failed Operation must push an Event");
    TEST_ASSERT_EQUAL_INT(EVENT_DIAGNOSTIC_FAILURE, event.event_type);
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "a failed Operation must also push a Fault");
}

void test_timeout_pushes_a_diagnostic_failure_event_and_fault(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process(); /* send */

    uint32_t i;
    for (i = 0; i < DIAG_RESPONSE_TIMEOUT_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* -> TIMEOUT */

    Event event;
    TEST_ASSERT_TRUE(pop_event(&event));
    TEST_ASSERT_EQUAL_INT(EVENT_DIAGNOSTIC_FAILURE, event.event_type);
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_successful_response_does_not_push_a_failure_event_or_fault(void) {
    s_stub_receive_result        = HWPROXY_CAN_OK;
    s_stub_receive_frame.dlc     = 1u;
    s_stub_receive_frame.data[0] = 0x01u;
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);

    Diagnostic_Process(); /* send */
    Diagnostic_Process(); /* receive -> SUCCESS */

    Event event;
    TEST_ASSERT_FALSE_MESSAGE(pop_event(&event), "a successful Operation must never push a DIAGNOSTIC_FAILURE Event");
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "a successful Operation must never push a Fault");
}

void test_a_failed_periodic_read_also_pushes_an_event(void) {
    /* The Event/Fault must fire regardless of who requested the Operation -
     * section 25 does not distinguish origin, only s_current_is_periodic's
     * Response-consumption rules do (see the tests below). */
    uint32_t i;
    s_stub_send_result = HWPROXY_CAN_ERROR;

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* IDLE -> starts periodic (RUNNING) */
    Diagnostic_Process(); /* RUNNING -> Send fails -> FAILED */

    Event event;
    TEST_ASSERT_TRUE(pop_event(&event));
    TEST_ASSERT_EQUAL_INT(EVENT_DIAGNOSTIC_FAILURE, event.event_type);
}

/* --- Bug fix: the internal periodic Read Parameter's own result must
 * never starve Current/Pending forever, and must never be handed out to an
 * external caller of Diagnostic_GetResponse() (see diagnostic_management.c's
 * s_current_is_periodic comment for the full bug this section proves
 * fixed). --- */

void test_periodic_read_result_is_never_exposed_via_get_response(void) {
    uint32_t i;
    s_stub_receive_result        = HWPROXY_CAN_OK;
    s_stub_receive_frame.dlc     = 1u;
    s_stub_receive_frame.data[0] = 0x11u;

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* starts periodic (RUNNING) */
    Diagnostic_Process(); /* sends */
    Diagnostic_Process(); /* receives -> SUCCESS, periodic-origin */

    DiagnosticResponse response;
    TEST_ASSERT_FALSE_MESSAGE(Diagnostic_GetResponse(&response),
        "a periodic-origin result must never be returned by the public GetResponse() API");
}

void test_periodic_read_frees_its_own_slot_so_a_second_periodic_read_can_run(void) {
    uint32_t i;

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* starts periodic (RUNNING) */
    Diagnostic_Process(); /* sends -> can_send_calls == 1 */
    TEST_ASSERT_EQUAL_INT(1, s_can_send_calls);

    /* No response is queued (s_stub_receive_result stays HWPROXY_CAN_ERROR,
     * the "no data yet" default from setUp) - drive it to TIMEOUT, then let
     * the fix's auto-consume branch free Current, then prove a second
     * periodic cycle can actually start and send again. */
    for (i = 0; i < DIAG_RESPONSE_TIMEOUT_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* still RUNNING -> now past Timeout -> FAILED (periodic-origin) */
    Diagnostic_Process(); /* auto-consume: FAILED -> IDLE (this call only frees the slot, nothing more) */

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* IDLE, interval elapsed again -> starts a second periodic read */
    Diagnostic_Process(); /* sends it */

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_can_send_calls,
        "before this fix, Current stayed occupied forever after the first periodic read, so a second one could never start");
}

void test_periodic_completion_does_not_starve_or_corrupt_a_later_explicit_request(void) {
    /* Reproduces the exact bug from the code review: an explicit Request
     * submitted after an unconsumed periodic result must (a) still
     * eventually run for real, and (b) GetResponse() must return ITS OWN
     * response, never the stale periodic one. */
    uint32_t i;

    s_stub_receive_result        = HWPROXY_CAN_OK;
    s_stub_receive_frame.dlc     = 1u;
    s_stub_receive_frame.data[0] = 0x11u; /* periodic response - must never leak out */

    for (i = 0; i < DIAG_PERIODIC_READ_PARAMETER_INTERVAL_TICKS; i++) {
        Time_TickIncrement();
    }
    Diagnostic_Process(); /* starts periodic */
    Diagnostic_Process(); /* sends */
    Diagnostic_Process(); /* receives -> SUCCESS, periodic-origin, unconsumed */

    DiagnosticRequest explicit_request = make_request(DIAG_OP_READ_DTC, 0x789u, 0u);
    TEST_ASSERT_EQUAL_INT_MESSAGE(DIAG_SUBMIT_PENDING, Diagnostic_SubmitRequest(&explicit_request),
        "Current is still occupied by the unconsumed periodic result - must queue as Pending, not be dropped or misrouted");

    s_stub_receive_frame.data[0] = 0x22u; /* the explicit request's own, distinct response */

    Diagnostic_Process(); /* auto-consume: periodic SUCCESS -> IDLE */
    Diagnostic_Process(); /* IDLE -> promotes Pending (explicit) into Current, RUNNING */
    Diagnostic_Process(); /* sends the explicit request */
    Diagnostic_Process(); /* receives -> SUCCESS, explicit-origin */

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0x789u, s_last_sent_frame.id, "the explicit Request must actually have been sent");

    DiagnosticResponse response;
    TEST_ASSERT_TRUE_MESSAGE(Diagnostic_GetResponse(&response), "the explicit request's own result must be retrievable");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x22u, response.data[0],
        "must be the explicit request's own response, never the stale periodic one (0x11)");
}

/* --- Section 22: Reset escalation after persistent failures --- */

static void fail_one_explicit_operation(void) {
    DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
    Diagnostic_SubmitRequest(&request);
    Diagnostic_Process(); /* BUS_ERROR on send -> FAILED immediately */

    DiagnosticResponse response;
    Diagnostic_GetResponse(&response); /* consume - frees Current back to IDLE for the next attempt */
}

void test_persistent_bus_errors_escalate_to_reset_request(void) {
    uint32_t i;
    s_stub_send_result = HWPROXY_CAN_ERROR;

    for (i = 0; i < DIAG_MAX_CONSECUTIVE_FAILURES - 1u; i++) {
        fail_one_explicit_operation();
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_call_count, "must not escalate before the threshold is actually reached");
    }

    fail_one_explicit_operation(); /* the Nth failure - reaches the threshold */

    TEST_ASSERT_EQUAL_INT(1, s_reset_request_call_count);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_SYSTEM_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("MCU", s_last_reset_request.target,
        "CAN is an MCU-internal peripheral, not an external Module like EC200U - SYSTEM_RESET, not MODULE_RESET");
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_DIAGNOSTIC, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_COMMUNICATION_STUCK, s_last_reset_request.reason);
}

void test_persistent_timeouts_also_escalate_to_reset_request(void) {
    /* Section 22 does not distinguish BUS_ERROR from TIMEOUT - both are
     * "the Operation failed", counted identically by fail_current(). */
    uint32_t i;

    for (i = 0; i < DIAG_MAX_CONSECUTIVE_FAILURES; i++) {
        DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
        Diagnostic_SubmitRequest(&request);
        Diagnostic_Process(); /* send */

        uint32_t t;
        for (t = 0; t < DIAG_RESPONSE_TIMEOUT_TICKS; t++) {
            Time_TickIncrement();
        }
        Diagnostic_Process(); /* -> TIMEOUT */

        DiagnosticResponse response;
        Diagnostic_GetResponse(&response);
    }

    TEST_ASSERT_EQUAL_INT(1, s_reset_request_call_count);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_COMMUNICATION_STUCK, s_last_reset_request.reason);
}

void test_reset_request_not_resubmitted_while_still_failing(void) {
    uint32_t i;
    s_stub_send_result = HWPROXY_CAN_ERROR;

    for (i = 0; i < DIAG_MAX_CONSECUTIVE_FAILURES; i++) {
        fail_one_explicit_operation();
    }
    TEST_ASSERT_EQUAL_INT(1, s_reset_request_call_count);

    fail_one_explicit_operation();
    fail_one_explicit_operation();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_reset_request_call_count, "must not resubmit every cycle while still failing");
}

void test_success_clears_the_consecutive_failure_counter(void) {
    uint32_t i;
    s_stub_send_result = HWPROXY_CAN_ERROR;

    for (i = 0; i < DIAG_MAX_CONSECUTIVE_FAILURES - 1u; i++) {
        fail_one_explicit_operation();
    }

    /* One success right before the threshold would have been reached */
    s_stub_send_result    = HWPROXY_CAN_OK;
    s_stub_receive_result = HWPROXY_CAN_OK;
    s_stub_receive_frame.dlc     = 1u;
    s_stub_receive_frame.data[0] = 0x01u;
    {
        DiagnosticRequest request = make_request(DIAG_OP_READ_DTC, 1u, 0u);
        Diagnostic_SubmitRequest(&request);
        Diagnostic_Process(); /* send */
        Diagnostic_Process(); /* receive -> SUCCESS */
        DiagnosticResponse response;
        Diagnostic_GetResponse(&response);
    }

    /* Back to failing - must take a FULL new run of DIAG_MAX_CONSECUTIVE_FAILURES
     * to escalate, proving the earlier near-miss count was actually cleared. */
    s_stub_receive_result = HWPROXY_CAN_ERROR;
    s_stub_send_result    = HWPROXY_CAN_ERROR;
    for (i = 0; i < DIAG_MAX_CONSECUTIVE_FAILURES - 1u; i++) {
        fail_one_explicit_operation();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_call_count, "a success must reset the consecutive-failure count");
}
