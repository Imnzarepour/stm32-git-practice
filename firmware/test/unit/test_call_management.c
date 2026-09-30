#include "unity.h"
#include "managements/call_management.h"
#include "managements/call_management_config.h"
#include "infrastructure/configuration.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "infrastructure/time_base.h"
#include "Mockec200u_manager.h"
#include "Mockpower_management.h"
#include "Mockreset_request.h"
#include "Mockconfiguration.h"
#include <string.h>
#include <stdio.h>

/* Call Management's real dependencies are the EC200U Manager Voice Call
 * Capability Contract, Power Mode, the Reset Request path, and (for the
 * incoming-call allowlist) Configuration Infrastructure - all four are
 * Mocked here (CMock), same pattern as test_gps_management.c /
 * test_sms_management.c. Queue/Event/Time infra is real, letting the
 * retry/timeout/Power-Mode-pacing timeline be driven deterministically
 * with Time_TickIncrement() instead of mocking Time itself.
 *
 * Every mocked call is wired via *_StubWithCallback (not
 * *_ExpectAndReturn) because Call_Process() calls several of these
 * conditionally, in an order that depends on internal state - same
 * technique test_gps_management.c uses. */

TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")

static EC200U_CallState s_stub_call_state;
static EC200U_Result    s_stub_dial_result;
static EC200U_Result    s_stub_hangup_result;
static EC200U_Result    s_stub_answer_result;

/* Controls EC200U_Call_GetIncomingNumber()'s stub: EC200U_OK copies
 * s_stub_incoming_number into the caller's buffer, anything else (e.g.
 * EC200U_NOT_READY/EC200U_ERROR) leaves the buffer untouched - same
 * contract as the real EC200U Manager Capability. Defaults (see setUp())
 * to setUp()'s default fixture entry in s_fixture_config so existing
 * "incoming call is accepted" tests stay meaningful without
 * each one having to configure it explicitly; tests exercising rejection
 * override one or both fields. */
static EC200U_Result s_stub_incoming_number_result;
static char          s_stub_incoming_number[CALL_MAX_DESTINATION_LENGTH];

static uint32_t s_dial_call_count;
static uint32_t s_hangup_call_count;
static uint32_t s_answer_call_count;
static uint32_t s_get_incoming_number_call_count;
static uint32_t s_get_state_call_count;
static char     s_last_dial_number[64];

static PowerMode s_stub_power_mode;

static int          s_reset_request_count;
static ResetRequest  s_last_reset_request;

/* Controls Configuration_Get()'s stub - a mutable fixture each test can
 * edit directly (e.g. clear allowed_phone_numbers.count to prove a
 * disallowed/empty list rejects everyone), same "test owns a static
 * fixture, stub just returns a pointer to it" idea Power Management's
 * test uses for GetDigitalInput(). */
static DeviceConfig s_fixture_config;

static const DeviceConfig* stub_configuration_get(int cmock_num_calls) {
    (void)cmock_num_calls;
    return &s_fixture_config;
}

static EC200U_CallState stub_get_call_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_get_state_call_count++;
    return s_stub_call_state;
}

static EC200U_Result stub_dial(const char* number, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_dial_call_count++;
    if (number) {
        strncpy(s_last_dial_number, number, sizeof(s_last_dial_number) - 1);
        s_last_dial_number[sizeof(s_last_dial_number) - 1] = '\0';
    }
    return s_stub_dial_result;
}

static EC200U_Result stub_hangup(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_hangup_call_count++;
    return s_stub_hangup_result;
}

static EC200U_Result stub_answer(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_answer_call_count++;
    return s_stub_answer_result;
}

static EC200U_Result stub_get_incoming_number(char* buffer, uint32_t buffer_size, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_get_incoming_number_call_count++;
    if (s_stub_incoming_number_result == EC200U_OK) {
        uint32_t len = (uint32_t)strlen(s_stub_incoming_number);
        if (len >= buffer_size) {
            len = buffer_size - 1u;
        }
        memcpy(buffer, s_stub_incoming_number, len);
        buffer[len] = '\0';
    }
    return s_stub_incoming_number_result;
}

static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

static void stub_submit_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_request_count++;
    if (request) {
        s_last_reset_request = *request;
    }
}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

void setUp(void) {
    Time_Init();
    Queues_Init();
    Call_Init();

    s_stub_call_state  = EC200U_CALL_IDLE;
    s_stub_dial_result  = EC200U_OK;
    s_stub_hangup_result = EC200U_OK;
    s_stub_answer_result = EC200U_OK;

    /* Default fixture: one allowed number, so pre-existing "incoming call
     * accepted" tests remain meaningful without each having to configure
     * Configuration explicitly. Tests exercising rejection either use a
     * different incoming number (still not on this list) or clear the
     * list itself. */
    memset(&s_fixture_config, 0, sizeof(s_fixture_config));
    s_fixture_config.access.allowed_phone_numbers.count = 1;
    strncpy(s_fixture_config.access.allowed_phone_numbers.numbers[0].number, "+980000000099",
            CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);
    /* Deliberately different from call_management_config.h's old, now-removed
     * CALL_ALERT_DESTINATION_PLACEHOLDER ("+980000000001") - see
     * test_policy_originated_alert_request_dials_the_configured_destination()
     * below for why this specific value matters: it is what actually proves
     * call_management.c reads this live from Configuration, not a leftover
     * compile-time constant. */
    strncpy(s_fixture_config.access.call_alert_destination, "+989121111111",
            CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);

    s_stub_incoming_number_result = EC200U_OK;
    strncpy(s_stub_incoming_number, "+980000000099", sizeof(s_stub_incoming_number) - 1);
    s_stub_incoming_number[sizeof(s_stub_incoming_number) - 1] = '\0';

    s_dial_call_count                = 0;
    s_hangup_call_count              = 0;
    s_answer_call_count              = 0;
    s_get_incoming_number_call_count = 0;
    s_get_state_call_count           = 0;
    memset(s_last_dial_number, 0, sizeof(s_last_dial_number));

    s_stub_power_mode = POWER_NORMAL;

    s_reset_request_count = 0;

    EC200U_Call_GetState_StubWithCallback(stub_get_call_state);
    EC200U_Call_Dial_StubWithCallback(stub_dial);
    EC200U_Call_Hangup_StubWithCallback(stub_hangup);
    EC200U_Call_Answer_StubWithCallback(stub_answer);
    EC200U_Call_GetIncomingNumber_StubWithCallback(stub_get_incoming_number);
    Power_GetMode_StubWithCallback(stub_power_get_mode);
    System_SubmitResetRequest_StubWithCallback(stub_submit_reset_request);
    Configuration_Get_StubWithCallback(stub_configuration_get);
}

void tearDown(void) {}

static bool submit(const char* destination) {
    CallRequest req;
    req.destination = destination;
    return Call_SubmitRequest(&req);
}

/* --- Call_SubmitRequest validation --- */

void test_submit_request_rejects_null_request(void) {
    TEST_ASSERT_FALSE(Call_SubmitRequest(NULL));
}

void test_submit_request_rejects_null_destination(void) {
    CallRequest req;
    req.destination = NULL;
    TEST_ASSERT_FALSE(Call_SubmitRequest(&req));
}

void test_submit_request_rejects_empty_destination(void) {
    TEST_ASSERT_FALSE(submit(""));
}

void test_submit_request_accepts_valid_destination(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
}

/* --- Initial state --- */

void test_initial_state_is_idle(void) {
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
}

/* --- Outgoing call lifecycle --- */

void test_outgoing_call_issues_dial_and_enters_outgoing_state(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));

    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_OUTGOING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32(1, s_dial_call_count);
    TEST_ASSERT_EQUAL_STRING("+15550001", s_last_dial_number);
}

void test_outgoing_call_progresses_through_ringing_to_connected(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process(); /* dials, enters CALL_OUTGOING */

    s_stub_call_state = EC200U_CALL_RINGING;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_RINGING, Call_GetState());

    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());
}

void test_outgoing_call_can_connect_directly_without_observed_ringing(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();

    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());
}

/* --- Incoming call detection + allowlist gating (contract section 8,
 * SECURITY warning at the top of call_management.c) --- */

void test_incoming_ringing_call_emits_event_and_sets_ringing_state(void) {
    /* setUp() defaults the simulated caller ID to the placeholder allowed
     * number, so this call is accepted. */
    s_stub_call_state = EC200U_CALL_RINGING; /* modem reports RINGING without Call Management ever dialing */

    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_RINGING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_answer_call_count, "An allowed incoming caller must be Answered");
    TEST_ASSERT_EQUAL_UINT32(0, s_hangup_call_count);

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_INCOMING_CALL, e.event_type);
}

void test_incoming_ringing_call_from_allowed_number_connects_on_next_cycle(void) {
    s_stub_call_state = EC200U_CALL_RINGING;
    Call_Process(); /* allowed -> Answer() issued, stays RINGING (not CONNECTED yet) */
    TEST_ASSERT_EQUAL_INT(CALL_RINGING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32(1, s_answer_call_count);

    s_stub_call_state = EC200U_CALL_ACTIVE; /* modem confirms the call actually connected */
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());
}

void test_incoming_ringing_call_from_disallowed_number_is_hungup_without_answer(void) {
    strncpy(s_stub_incoming_number, "+19995550000", sizeof(s_stub_incoming_number) - 1); /* not on the allowlist */
    s_stub_call_state = EC200U_CALL_RINGING;

    Call_Process();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_answer_call_count, "A disallowed caller must never be Answered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_hangup_call_count, "A disallowed caller must be actively rejected (Hangup), not merely ignored");
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "Rejecting an unauthorized caller is expected behavior, not a Fault");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count, "Rejecting an unauthorized caller must never escalate to a Reset Request");
}

void test_incoming_ringing_call_with_unverifiable_number_is_rejected_fail_closed(void) {
    /* EC200U_Call_GetIncomingNumber() fails to report a number at all -
     * this must be treated exactly like a disallowed number (Fail-Closed),
     * never as an implicit allow. */
    s_stub_incoming_number_result = EC200U_NOT_READY;
    s_stub_call_state = EC200U_CALL_RINGING;

    Call_Process();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_answer_call_count, "An unverifiable caller must never be Answered");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_hangup_call_count, "An unverifiable caller must be actively rejected");
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);
}

void test_incoming_call_answered_directly_still_detected_as_incoming(void) {
    /* setUp() defaults the simulated caller ID to the placeholder allowed
     * number, so this already-connected call is accepted. */
    s_stub_call_state = EC200U_CALL_ACTIVE; /* jumped straight to ACTIVE, never observed RINGING */

    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s_hangup_call_count, "An allowed already-connected caller must not be hung up");

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_INCOMING_CALL, e.event_type);
}

void test_incoming_active_call_from_disallowed_number_is_hungup_immediately(void) {
    /* Modem auto-answered before Call Management ever got a chance to
     * decide (the fallback path in the SECURITY warning at the top of
     * call_management.c) - the only protective action left is an
     * immediate Hangup. */
    strncpy(s_stub_incoming_number, "+19995550000", sizeof(s_stub_incoming_number) - 1);
    s_stub_call_state = EC200U_CALL_ACTIVE;

    Call_Process();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_hangup_call_count, "An already-connected disallowed caller must be hung up immediately");
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "Rejecting an unauthorized already-connected caller is expected behavior, not a Fault");
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE_MESSAGE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len), "The incoming call still happened and must still be reported");
    TEST_ASSERT_EQUAL_INT(EVENT_INCOMING_CALL, e.event_type);
}

void test_incoming_active_call_with_unverifiable_number_is_hungup_immediately(void) {
    s_stub_incoming_number_result = EC200U_ERROR;
    s_stub_call_state = EC200U_CALL_ACTIVE;

    Call_Process();

    TEST_ASSERT_EQUAL_UINT32(1, s_hangup_call_count);
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);
}

void test_outgoing_call_never_emits_incoming_call_event(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();
    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

void test_incoming_call_rejected_before_answer_returns_to_idle_without_fault(void) {
    s_stub_call_state = EC200U_CALL_RINGING;
    Call_Process(); /* allowed number (setUp() default) - detected as incoming, Answered, RINGING */
    Queue_Clear(Queues_GetEventQueue()); /* isolate from the INCOMING_CALL event already checked above */

    s_stub_call_state = EC200U_CALL_IDLE; /* caller hung up before the modem confirmed the connection */
    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "A missed/rejected incoming call is a normal outcome, not a Fault");
}

/* --- No-Connection timeout / retry / failure (contract section 10) --- */

void test_no_connection_timeout_retries_the_same_destination(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process(); /* attempt 1 - dial issued */
    TEST_ASSERT_EQUAL_UINT32(1, s_dial_call_count);

    advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
    Call_Process(); /* attempt 2 - still never progressed past IDLE */

    TEST_ASSERT_EQUAL_UINT32(2, s_dial_call_count);
    TEST_ASSERT_EQUAL_STRING("+15550001", s_last_dial_number);
    TEST_ASSERT_EQUAL_INT(CALL_OUTGOING, Call_GetState());
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_no_connection_timeout_exhausts_retries_then_faults_and_frees_the_slot(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process(); /* attempt 1/4 */

    /* Each loop iteration is one handle_dial_setup_failure() evaluation;
     * the first CALL_MAX_DIAL_ATTEMPTS of them retry (issuing one more
     * Dial each), and the (CALL_MAX_DIAL_ATTEMPTS + 1)-th one is what
     * finally gives up - see handle_dial_setup_failure()'s comment in
     * call_management.c. */
    for (uint32_t i = 0; i <= CALL_MAX_DIAL_ATTEMPTS; i++) {
        advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
        Call_Process();
    }

    TEST_ASSERT_EQUAL_UINT32(1u + CALL_MAX_DIAL_ATTEMPTS, s_dial_call_count);
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));

    /* The slot must be free again - a fresh request can start immediately. */
    TEST_ASSERT_TRUE(submit("+15550002"));
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_OUTGOING, Call_GetState());
    TEST_ASSERT_EQUAL_STRING("+15550002", s_last_dial_number);
}

/* Regression-style test, same lesson as gps_management.c's
 * test_recovery_attempts_are_spaced_out_even_when_start_also_fails:
 * back-to-back Call_Process() calls with zero elapsed time, right after
 * a retry Dial was just issued, must NOT be treated as additional
 * timeouts/retries - the retry-spacing reference tick must advance
 * unconditionally, regardless of whether the retry itself "succeeds". */
void test_retry_attempts_are_spaced_out_even_when_every_dial_fails(void) {
    s_stub_dial_result = EC200U_ERROR; /* the module rejects every Dial attempt outright */
    TEST_ASSERT_TRUE(submit("+15550001"));

    Call_Process(); /* initial attempt - rejected, but still counts as "an attempt was made" */
    TEST_ASSERT_EQUAL_UINT32(1, s_dial_call_count);

    /* No time advanced - must not retry yet. */
    for (int i = 0; i < 5; i++) {
        Call_Process();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_dial_call_count, "Must not retry before CALL_NO_CONNECTION_TIMEOUT_MS has elapsed");

    advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
    Call_Process(); /* 1st retry - also rejected */
    TEST_ASSERT_EQUAL_UINT32(2, s_dial_call_count);

    /* The core regression check: several immediate, back-to-back
     * Call_Process() calls right after a failed retry must not spawn
     * more retries. */
    for (int i = 0; i < 5; i++) {
        Call_Process();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, s_dial_call_count,
        "Retry attempts must be spaced a full CALL_NO_CONNECTION_TIMEOUT_MS apart, "
        "not repeated on every consecutive Process() call even when Dial() also fails");
}

/* --- Fault -> Reset Request escalation (contract section 11) --- */

void test_persistent_setup_failures_across_requests_escalate_to_reset_request(void) {
    s_stub_dial_result = EC200U_ERROR;

    for (uint32_t request_num = 0; request_num < CALL_MAX_CONSECUTIVE_OPERATION_FAILURES; request_num++) {
        char destination[16];
        (void)snprintf(destination, sizeof(destination), "+1555%04u", (unsigned)request_num);
        TEST_ASSERT_TRUE(submit(destination));

        Call_Process(); /* initial dial for this request */
        for (uint32_t attempt = 0; attempt <= CALL_MAX_DIAL_ATTEMPTS; attempt++) {
            advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
            Call_Process();
        }
        /* This request has now fully exhausted its own retries and given up. */
    }

    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_CALL, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_MODULE_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_MODULE_UNRESPONSIVE, s_last_reset_request.reason);
}

void test_reaching_connected_clears_the_consecutive_failure_counter(void) {
    s_stub_dial_result = EC200U_ERROR;

    /* One full failed request - one step below the escalation threshold. */
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();
    for (uint32_t attempt = 0; attempt <= CALL_MAX_DIAL_ATTEMPTS; attempt++) {
        advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
        Call_Process();
    }
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);

    /* A genuinely successful call must reset the counter. */
    s_stub_dial_result = EC200U_OK;
    TEST_ASSERT_TRUE(submit("+15550002"));
    Call_Process();
    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());
    s_stub_call_state = EC200U_CALL_IDLE; /* end this call cleanly */
    Call_Process(); /* -> ENDING */
    Call_Process(); /* -> IDLE */

    /* If the counter had NOT been cleared, (CALL_MAX_CONSECUTIVE_OPERATION_FAILURES - 1)
     * more full failures should still be safely below the threshold. */
    s_stub_dial_result = EC200U_ERROR;
    for (uint32_t request_num = 0; request_num < CALL_MAX_CONSECUTIVE_OPERATION_FAILURES - 1u; request_num++) {
        char destination[16];
        (void)snprintf(destination, sizeof(destination), "+1666%04u", (unsigned)request_num);
        TEST_ASSERT_TRUE(submit(destination));
        Call_Process();
        for (uint32_t attempt = 0; attempt <= CALL_MAX_DIAL_ATTEMPTS; attempt++) {
            advance_ms(CALL_NO_CONNECTION_TIMEOUT_MS);
            Call_Process();
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count,
        "A prior CONNECTED call must reset the failure counter, so this second episode should not yet reach the threshold");
}

/* --- Natural end (remote hangup) --- */

void test_natural_remote_hangup_from_connected_returns_to_idle_via_ending(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();
    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());

    s_stub_call_state = EC200U_CALL_ENDED; /* remote hung up */
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_ENDING, Call_GetState());

    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Call_Terminate() (contract section 9) --- */

void test_terminate_with_nothing_active_returns_false(void) {
    TEST_ASSERT_FALSE(Call_Terminate());
    TEST_ASSERT_EQUAL_UINT32(0, s_hangup_call_count);
}

void test_terminate_during_active_call_hangs_up_and_returns_to_idle(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();
    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_CONNECTED, Call_GetState());

    TEST_ASSERT_TRUE(Call_Terminate());
    Call_Process(); /* issues Hangup, enters CALL_ENDING */
    TEST_ASSERT_EQUAL_INT(CALL_ENDING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32(1, s_hangup_call_count);

    s_stub_call_state = EC200U_CALL_IDLE; /* modem confirms the hangup */
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
}

void test_terminate_during_outgoing_setup_cancels_it(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process(); /* CALL_OUTGOING, never connected yet */
    TEST_ASSERT_EQUAL_INT(CALL_OUTGOING, Call_GetState());

    TEST_ASSERT_TRUE(Call_Terminate());
    Call_Process();
    TEST_ASSERT_EQUAL_INT(CALL_ENDING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32(1, s_hangup_call_count);
}

void test_termination_timeout_forces_idle_and_pushes_fault(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process();
    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();

    TEST_ASSERT_TRUE(Call_Terminate());
    Call_Process(); /* Hangup issued, but the modem never actually settles */
    TEST_ASSERT_EQUAL_INT(CALL_ENDING, Call_GetState());

    advance_ms(CALL_HANGUP_TIMEOUT_MS);
    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_IDLE, Call_GetState());
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Policy-originated call requests (contract section 12) ---
 * shared/event_policy.c dispatches EVENT_SHOCK_DETECTED by pushing a
 * CallQueueItem tagged CALL_QUEUE_ITEM_ALERT onto
 * Queues_GetCallRequestQueue() (see dispatch() in event_policy.c and
 * CallQueueItem's comment in call_management.h). This test reproduces
 * that exact wire item without depending on event_policy.c itself - same
 * style test_sms_management.c uses for SmsQueueItem's
 * SMS_QUEUE_ITEM_ALERT case. */
void test_policy_originated_alert_request_dials_the_configured_destination(void) {
    /* Bug fix regression: this used to dial a hardcoded compile-time
     * constant (CALL_ALERT_DESTINATION_PLACEHOLDER) that happened to equal
     * the old test fixture value, so this assertion passed even though the
     * production code never actually read Configuration at all. setUp()
     * now seeds a deliberately DIFFERENT value
     * ("+989121111111") specifically so this test only passes if
     * call_management.c's try_start_next() genuinely reads
     * Configuration_Get()->access.call_alert_destination live, mirroring
     * test_sms_management.c's own capture_alert_destination() proof for
     * the SMS side. */
    CallQueueItem item;
    item.kind = CALL_QUEUE_ITEM_ALERT;
    item.payload.alert.event_type = EVENT_SHOCK_DETECTED;
    item.payload.alert.timestamp  = 12345;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetCallRequestQueue(), &item, sizeof(item)));

    Call_Process();

    TEST_ASSERT_EQUAL_INT(CALL_OUTGOING, Call_GetState());
    TEST_ASSERT_EQUAL_UINT32(1, s_dial_call_count);
    TEST_ASSERT_EQUAL_STRING(s_fixture_config.access.call_alert_destination, s_last_dial_number);
}

/* --- One active operation (state-operation-contract.md section 15) --- */

void test_second_request_waits_while_first_is_in_progress(void) {
    TEST_ASSERT_TRUE(submit("+15550001"));
    TEST_ASSERT_TRUE(submit("+15550002"));

    Call_Process(); /* only the first is dialed */
    TEST_ASSERT_EQUAL_UINT32(1, s_dial_call_count);
    TEST_ASSERT_EQUAL_STRING("+15550001", s_last_dial_number);
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetCallRequestQueue()), "The second request must still be waiting in the Queue");

    s_stub_call_state = EC200U_CALL_ACTIVE;
    Call_Process();
    s_stub_call_state = EC200U_CALL_ENDED;
    Call_Process(); /* -> ENDING */
    Call_Process(); /* -> IDLE, slot free again */

    Call_Process(); /* now the second request is picked up */
    TEST_ASSERT_EQUAL_UINT32(2, s_dial_call_count);
    TEST_ASSERT_EQUAL_STRING("+15550002", s_last_dial_number);
}

/* --- Power Mode (contract section 13) ---
 * Only the background "nothing is happening at all" poll may be slowed
 * down in LOW_POWER; an in-progress call must never be. */

void test_low_power_throttles_idle_background_poll(void) {
    s_stub_power_mode = POWER_LOW_POWER;

    Call_Process(); /* first poll - never throttled (no prior reference point yet) */
    TEST_ASSERT_EQUAL_UINT32(1, s_get_state_call_count);

    /* No time advanced - still well inside the throttled interval. */
    Call_Process();
    Call_Process();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_get_state_call_count, "Must not poll again before the LOW_POWER idle interval elapses");

    advance_ms(CALL_IDLE_POLL_INTERVAL_MS * CALL_LOW_POWER_POLL_RATE_MULTIPLIER);
    Call_Process();
    TEST_ASSERT_EQUAL_UINT32(2, s_get_state_call_count);
}

void test_normal_power_mode_polls_idle_at_the_normal_interval(void) {
    s_stub_power_mode = POWER_NORMAL;

    Call_Process();
    TEST_ASSERT_EQUAL_UINT32(1, s_get_state_call_count);

    advance_ms(CALL_IDLE_POLL_INTERVAL_MS);
    Call_Process();
    TEST_ASSERT_EQUAL_UINT32(2, s_get_state_call_count);
}

/* --- Call_IsNumberAllowed() (security warning at the top of call_management.c) ---
 * Proves the allowlist-matching logic against s_fixture_config
 * (Configuration_Get()'s stubbed return) directly; the incoming-call
 * RINGING/ACTIVE tests above already prove this is actually wired into
 * poll_and_advance()'s enforcement point. */

void test_number_allowed_rejects_null(void) {
    TEST_ASSERT_FALSE(Call_IsNumberAllowed(NULL));
}

void test_number_allowed_rejects_number_not_on_the_list(void) {
    TEST_ASSERT_FALSE(Call_IsNumberAllowed("+15550001"));
}

void test_number_allowed_accepts_a_number_on_the_configured_list(void) {
    TEST_ASSERT_TRUE(Call_IsNumberAllowed("+980000000099")); /* setUp()'s default fixture entry */
}

void test_number_allowed_rejects_everyone_when_the_list_is_empty(void) {
    s_fixture_config.access.allowed_phone_numbers.count = 0;
    TEST_ASSERT_FALSE(Call_IsNumberAllowed("+980000000099"));
}

void test_active_call_is_polled_every_cycle_even_in_low_power(void) {
    s_stub_power_mode = POWER_LOW_POWER;

    TEST_ASSERT_TRUE(submit("+15550001"));
    Call_Process(); /* dials - CALL_OUTGOING, no longer "idle background" */
    uint32_t count_after_dial = s_get_state_call_count;

    /* No time advanced at all - an in-progress call must still be polled
     * on every single Process() cycle, unlike the idle background case. */
    Call_Process();
    Call_Process();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(count_after_dial + 2u, s_get_state_call_count,
        "An in-progress call must be polled every cycle regardless of Power Mode");
}
