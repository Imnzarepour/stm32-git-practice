#include "unity.h"
#include "managements/sms_management.h"
#include "managements/gps_management.h"
#include "infrastructure/configuration.h"
#include "shared/queues.h"
#include "shared/event.h"
#include "shared/command_safety_policy.h"
#include "infrastructure/time_base.h"
#include "Mockec200u_manager.h"
#include "Mockactuator_management.h"
#include "Mockpower_management.h"
#include "Mockconfiguration.h"
#include "Mockcall_management.h"
#include "Mockgps_management.h"
#include "Mockcommand_router.h"
#include <string.h>

/* SMS Management's dependencies are EC200U Manager (SMS transport),
 * Actuator Management's request submission (Command forwarding),
 * Power Management's Power_GetMode() (contract 14, "SMS و Power Mode"),
 * Configuration Infrastructure (the alert destination number, see
 * configuration.h's ConfigAccess comment), Call Management's
 * Call_IsNumberAllowed() (reused directly for the Position Request/Door
 * command sender checks), GPS Management's GPS_GetPosition() (Position
 * Request path) and shared/command_router.c's CommandRouter_Submit()
 * (Door command path, sms_try_handle_door_command()) - all Mocked here,
 * per hardware-proxy.md's rule that a Management's test mocks exactly
 * what it depends on, nothing deeper - CommandRouter_Submit() itself is
 * Mocked (not linked real) so this test isolates SMS Management's own
 * behavior (does it build the right Command?) from the router's/
 * Diagnostic's own logic, same reasoning test_server_management.c
 * documents for its own Diagnostic dependency. shared/command_safety_policy.c
 * IS linked real (below) since it is a pure, deterministic function and
 * exactly what these tests exercise, not something to fake away - same
 * choice test_command_router.c already makes. Queue/Event/Time infra is
 * real (queue.c, shared/queues.c, time_base.c), same pattern as
 * test_event_policy.c - this also lets the retry/backoff/Power-Mode-pacing
 * timeline be driven deterministically with Time_TickIncrement() instead
 * of needing to mock Time itself. */

TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("shared/command_safety_policy.c")

static PowerMode s_stub_power_mode;
static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

/* Controls Configuration_Get()'s stub - same "test owns a static fixture,
 * stub just returns a pointer to it" pattern test_call_management.c uses
 * for its own Configuration dependency. */
static DeviceConfig s_fixture_config;
static const DeviceConfig* stub_configuration_get(int cmock_num_calls) {
    (void)cmock_num_calls;
    return &s_fixture_config;
}

/* --- Position Request path (sms_try_handle_position_request()) fixtures --- */
static bool        s_stub_number_allowed;
static char         s_last_checked_number[32];
static GpsPosition   s_stub_gps_position;

static bool stub_call_is_number_allowed(const char* number, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (number) {
        strncpy(s_last_checked_number, number, sizeof(s_last_checked_number) - 1);
    }
    return s_stub_number_allowed;
}

static GpsPosition stub_gps_get_position(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_gps_position;
}

/* --- Door command fixtures (sms_try_handle_door_command()) --- */
static int                  s_command_router_submit_call_count;
static Command               s_last_submitted_command;
static CommandRouterResult   s_stub_command_router_result;

static CommandRouterResult stub_command_router_submit(const Command* command, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_command_router_submit_call_count++;
    if (command) {
        s_last_submitted_command = *command;
    }
    return s_stub_command_router_result;
}

void setUp(void) {
    Queues_Init();
    Time_Init();
    SMS_Init();

    /* Default: NORMAL, so every pre-existing test (none of which cares
     * about Power Mode) keeps behaving exactly as before - only the new
     * LOW_POWER-specific tests below override this. */
    s_stub_power_mode = POWER_NORMAL;
    Power_GetMode_StubWithCallback(stub_power_get_mode);

    memset(&s_fixture_config, 0, sizeof(s_fixture_config));
    strncpy(s_fixture_config.access.sms_alert_destination, "+980000000000",
            CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);
    Configuration_Get_StubWithCallback(stub_configuration_get);

    /* Default: allowed sender + a valid fix, so a dedicated test can
     * override just the one thing it wants to exercise instead of
     * repeating the whole fixture. */
    s_stub_number_allowed = true;
    memset(s_last_checked_number, 0, sizeof(s_last_checked_number));
    Call_IsNumberAllowed_StubWithCallback(stub_call_is_number_allowed);

    s_stub_gps_position.latitude  = 35699700;  /* 35.699700 */
    s_stub_gps_position.longitude = 51338100;  /* 51.338100 */
    s_stub_gps_position.timestamp = 12345u;
    s_stub_gps_position.valid     = true;
    GPS_GetPosition_StubWithCallback(stub_gps_get_position);

    s_command_router_submit_call_count = 0;
    memset(&s_last_submitted_command, 0, sizeof(s_last_submitted_command));
    s_stub_command_router_result = COMMAND_ROUTER_ACCEPTED;
    CommandRouter_Submit_StubWithCallback(stub_command_router_submit);
}

/* A real AT+CMGS round trip means EC200U_SMS_Send()'s own EC200U_OK now
 * only means "accepted, in progress" (see sms_management.c's
 * advance_waiting_result() and infrastructure/ec200u_manager.h's
 * EC200U_SMS_GetSendStatus() comment for why) - every test below that
 * still expects an immediate EVENT_SMS_SENT after a successful
 * EC200U_SMS_Send() also registers this same-cycle confirmation, matching
 * advance_waiting_result() calling EC200U_SMS_GetSendStatus() right after
 * advance_send() within the same SMS_Process(). */
static void expect_send_confirmed(void) {
    EC200U_SMS_GetSendStatus_ExpectAndReturn(EC200U_OK);
}
void tearDown(void) {}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

static void submit(const char* destination, const char* payload) {
    SmsRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.destination, destination, sizeof(req.destination) - 1);
    strncpy(req.payload, payload, sizeof(req.payload) - 1);
    req.payload_length = (uint32_t)strlen(payload);
    TEST_ASSERT_TRUE(SMS_SubmitRequest(&req));
}

/* --- Outgoing: direct SmsRequest --- */

void test_submitted_request_is_sent_on_next_process(void) {
    submit("+15550001", "hello");

    /* SMS_Process() polls incoming first, then advances the outgoing
     * Operation - expectations must be registered in that same order,
     * since CMock enforces call order across all mocks by default. */
    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();

    SMS_Process();

    /* Operation completed -> an SMS_SENT Event must be on the Event Queue */
    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

static EC200U_Result capture_send_result(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING("+15550001", sms->destination);
    TEST_ASSERT_EQUAL_STRING("hello", sms->payload);
    TEST_ASSERT_EQUAL_UINT32(5, sms->payload_length);
    return EC200U_OK;
}

void test_sent_payload_matches_submitted_request(void) {
    submit("+15550001", "hello");

    EC200U_SMS_Send_StubWithCallback(capture_send_result);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);
    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();

    SMS_Process();
}

/* --- Retry / Fault --- */

void test_send_failure_is_retried_not_faulted_immediately(void) {
    submit("+15550001", "hello");

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_ERROR);
    EC200U_SMS_Send_IgnoreArg_sms();
    SMS_Process();

    /* No Fault yet - only one failed attempt out of SMS_SEND_MAX_RETRIES */
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    /* And no SMS_SENT Event either */
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

void test_retry_exhaustion_produces_fault_and_frees_the_slot(void) {
    submit("+15550001", "hello");

    EC200U_SMS_GetReceived_IgnoreAndReturn(EC200U_NOT_READY);
    EC200U_SMS_Send_IgnoreAndReturn(EC200U_ERROR); /* every attempt fails */

    SMS_Process(); /* attempt 1/4 - immediate, no backoff yet */

    /* SMS_SEND_MAX_RETRIES (3) more attempts, each gated by the
     * SMS_SEND_RETRY_DELAY_MS backoff - advance real Tick time between
     * them exactly like the Super Loop would over several seconds. */
    for (int i = 0; i < 3; i++) {
        advance_ms(5000);
        SMS_Process();
    }

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue())); /* no SMS_SENT - it never succeeded */

    /* The slot must be free again - a fresh request can start immediately. */
    submit("+15550002", "next");
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();
    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

/* --- Incoming: plain SMS becomes an Event --- */

static EC200U_Result provide_incoming_sms(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination     = "+15550009";
    sms->payload          = "hi there";
    sms->payload_length   = 8;
    return EC200U_OK;
}

void test_incoming_plain_sms_becomes_sms_received_event(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_sms);

    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_RECEIVED, e.event_type);
}

void test_incoming_sms_never_reaches_actuator_yet(void) {
    /* Command grammar/authorization is an open question - see the TODO
     * in sms_try_parse_command(). Until answered, Actuator_SubmitRequest
     * must never be called, no matter what the SMS says. */
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_sms);
    /* No Actuator_SubmitRequest_Expect*() call registered at all -
     * CMock fails the test if it is invoked unexpectedly. */

    SMS_Process();
}

void test_empty_incoming_payload_is_ignored(void) {
    EC200U_SmsData empty = { "+15550009", NULL, 0 };
    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_GetReceived_ReturnThruPtr_sms(&empty);

    SMS_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

/* --- Event Policy alert wrapping (SMS_QUEUE_ITEM_ALERT path) --- */

static EC200U_Result capture_alert_destination(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING(s_fixture_config.access.sms_alert_destination, sms->destination);
    return EC200U_OK;
}

void test_alert_queue_item_uses_configured_destination(void) {
    SmsQueueItem item;
    item.kind                    = SMS_QUEUE_ITEM_ALERT;
    item.payload.alert.event_type = EVENT_SHOCK_DETECTED;
    item.payload.alert.timestamp  = 123;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item)));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_StubWithCallback(capture_alert_destination);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

void test_alert_queue_item_is_sent_with_composed_text(void) {
    SmsQueueItem item;
    item.kind                    = SMS_QUEUE_ITEM_ALERT;
    item.payload.alert.event_type = EVENT_SHOCK_DETECTED;
    item.payload.alert.timestamp  = 123;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item)));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();

    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

/* --- Alert text per event type (alert_text_for_event's other branches) --- */

static EC200U_Result capture_send_result_low_battery(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING("ALERT: LOW_BATTERY_DETECTED", sms->payload);
    return EC200U_OK;
}

static EC200U_Result capture_send_result_default_alert(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING("ALERT: EVENT", sms->payload);
    return EC200U_OK;
}

void test_alert_text_for_low_battery_event(void) {
    SmsQueueItem item;
    item.kind                     = SMS_QUEUE_ITEM_ALERT;
    item.payload.alert.event_type = EVENT_LOW_BATTERY_DETECTED;
    item.payload.alert.timestamp  = 1;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item)));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_StubWithCallback(capture_send_result_low_battery);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

void test_alert_text_for_unmapped_event_uses_default(void) {
    SmsQueueItem item;
    item.kind                     = SMS_QUEUE_ITEM_ALERT;
    item.payload.alert.event_type = EVENT_ACC_ON; /* not SHOCK/LOW_BATTERY - hits the default case */
    item.payload.alert.timestamp  = 1;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetSmsRequestQueue(), &item, sizeof(item)));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_StubWithCallback(capture_send_result_default_alert);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

/* --- Oversized destination/payload are truncated, not overflowed --- */

static EC200U_Result capture_send_result_truncated_destination(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    /* Must be exactly SMS_MAX_DESTINATION_LENGTH-1 'A's + NUL - proves
     * start_send()'s bounded_strlen() stopped at the limit instead of
     * reading past the (deliberately non-NUL-terminated) source buffer. */
    TEST_ASSERT_EQUAL_UINT32(SMS_MAX_DESTINATION_LENGTH - 1u, (uint32_t)strlen(sms->destination));
    return EC200U_OK;
}

static EC200U_Result capture_send_result_truncated_payload(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_UINT32(SMS_MAX_PAYLOAD_LENGTH - 1u, sms->payload_length);
    TEST_ASSERT_EQUAL_UINT32(SMS_MAX_PAYLOAD_LENGTH - 1u, (uint32_t)strlen(sms->payload));
    return EC200U_OK;
}

void test_oversized_destination_is_truncated_not_overflowed(void) {
    SmsRequest req;
    memset(&req, 0, sizeof(req));
    /* Fill every byte of destination, deliberately leaving no room for a
     * NUL terminator - the exact "careless caller" case bounded_strlen()
     * in start_send() exists to survive without an out-of-bounds read. */
    memset(req.destination, 'A', sizeof(req.destination));
    strncpy(req.payload, "x", sizeof(req.payload) - 1);
    req.payload_length = 1;
    TEST_ASSERT_TRUE(SMS_SubmitRequest(&req));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_StubWithCallback(capture_send_result_truncated_destination);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

void test_oversized_payload_length_is_truncated_not_overflowed(void) {
    SmsRequest req;
    memset(&req, 0, sizeof(req));
    strncpy(req.destination, "+15550001", sizeof(req.destination) - 1);
    memset(req.payload, 'B', sizeof(req.payload) - 1);
    req.payload_length = SMS_MAX_PAYLOAD_LENGTH; /* caller claims more than the buffer can hold */
    TEST_ASSERT_TRUE(SMS_SubmitRequest(&req));

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_StubWithCallback(capture_send_result_truncated_payload);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

/* --- Retry backoff: a Process() inside the delay window must not re-send --- */

void test_process_during_backoff_window_does_not_retry_early(void) {
    submit("+15550001", "hello");

    EC200U_SMS_GetReceived_IgnoreAndReturn(EC200U_NOT_READY);
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_ERROR); /* first attempt fails, enters backoff */
    EC200U_SMS_Send_IgnoreArg_sms();
    SMS_Process();

    /* No time advanced - still well inside SMS_SEND_RETRY_DELAY_MS.
     * No further EC200U_SMS_Send_Expect*() registered - CMock fails the
     * test if advance_send() attempts to send again this early. */
    SMS_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

/* --- Real hardware timing: EC200U_SMS_Send()'s own EC200U_OK now only
 * means "accepted, an AT+CMGS sequence just started" - the real result is
 * polled via EC200U_SMS_GetSendStatus() (see setUp()'s default stub for
 * why every other test above does not need to know this at all). These
 * two tests exercise that polling explicitly. --- */

void test_send_stays_pending_until_get_send_status_confirms_it(void) {
    submit("+15550001", "hello");

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    EC200U_SMS_GetSendStatus_ExpectAndReturn(EC200U_BUSY); /* still in progress on this cycle */

    SMS_Process();

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetEventQueue()), "must not report SMS_SENT before EC200U_SMS_GetSendStatus() actually confirms it");

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_GetSendStatus_ExpectAndReturn(EC200U_OK); /* confirmed on a later cycle */
    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

void test_send_accepted_but_later_reported_failed_is_retried(void) {
    submit("+15550001", "hello");

    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    EC200U_SMS_GetSendStatus_ExpectAndReturn(EC200U_ERROR); /* the real AT+CMGS round trip failed */

    SMS_Process();

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "one failed attempt out of SMS_SEND_MAX_RETRIES must not Fault yet");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));

    /* Retried from the top after the backoff window - same
     * SMS_SEND_RETRY_DELAY_MS this file's other retry tests already use. */
    advance_ms(5000);
    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    EC200U_SMS_GetSendStatus_ExpectAndReturn(EC200U_OK);
    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

/* --- SMS_SubmitRequest(NULL) --- */

void test_submit_request_rejects_null(void) {
    TEST_ASSERT_FALSE(SMS_SubmitRequest(NULL));
}

/* --- Power Mode (contract 14: "SMS و Power Mode") ---
 * Outgoing sends are spaced out further in LOW_POWER; incoming
 * reception must behave identically in both Modes - never slowed down
 * just because the device entered LOW_POWER. */

void test_low_power_throttles_successive_sends(void) {
    s_stub_power_mode = POWER_LOW_POWER;

    submit("+15550001", "first");
    EC200U_SMS_GetReceived_IgnoreAndReturn(EC200U_NOT_READY);
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();
    SMS_Process(); /* very first attempt ever - never throttled, s_send_throttled starts false */

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);

    /* A second request queued right away, no time advanced: in NORMAL
     * this would be sent on the very next Process() (see
     * test_normal_power_mode_does_not_throttle_successive_sends below);
     * in LOW_POWER it must NOT reach EC200U_SMS_Send() this cycle at
     * all - no EC200U_SMS_Send_Expect*() registered below, so CMock
     * fails the test if advance_send() calls it anyway. */
    submit("+15550002", "second");
    SMS_Process();
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));

    /* Once the LOW_POWER minimum interval has elapsed, the queued send
     * finally goes through. */
    advance_ms(SMS_LOW_POWER_MIN_SEND_INTERVAL_MS);
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();
    SMS_Process();

    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

void test_normal_power_mode_does_not_throttle_successive_sends(void) {
    s_stub_power_mode = POWER_NORMAL;

    submit("+15550001", "first");
    EC200U_SMS_GetReceived_IgnoreAndReturn(EC200U_NOT_READY);
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();
    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);

    /* No time advanced at all - in NORMAL the next queued send must
     * still go out on the very next Process() cycle. */
    submit("+15550002", "second");
    EC200U_SMS_Send_ExpectAndReturn(NULL, EC200U_OK);
    EC200U_SMS_Send_IgnoreArg_sms();
    expect_send_confirmed();
    SMS_Process();

    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
}

void test_incoming_reception_unaffected_by_low_power(void) {
    /* Same scenario/assertions as test_incoming_plain_sms_becomes_sms_received_event,
     * only with Power Mode forced to LOW_POWER - process_incoming() must
     * behave identically: no delay, no gating, called every cycle. */
    s_stub_power_mode = POWER_LOW_POWER;
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_sms);

    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_RECEIVED, e.event_type);

    /* And immediately again, no time advanced - still processed right
     * away, proving nothing in the receive path was made to wait for a
     * LOW_POWER interval (unlike the outgoing path above). */
    SMS_Process();
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_RECEIVED, e.event_type);
}

/* --- Non-blocking guarantee --- */

void test_process_with_nothing_to_do_does_not_touch_ec200u_send(void) {
    EC200U_SMS_GetReceived_ExpectAndReturn(NULL, EC200U_NOT_READY);
    EC200U_SMS_GetReceived_IgnoreArg_sms();
    /* No EC200U_SMS_Send_Expect*() registered - must not be called */

    SMS_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Position Request command (sms_try_handle_position_request(),
 * SMS_POSITION_REQUEST_KEYWORD = "LOC" today - see the provisional-syntax
 * TODO in sms_management_config.h) --- */

static EC200U_Result provide_incoming_position_request(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099"; /* SENDER for an incoming message - see the NOTE in sms_try_handle_position_request() about this field's direction-dependent meaning */
    sms->payload         = "LOC";
    sms->payload_length  = 3;
    return EC200U_OK;
}

static EC200U_Result capture_position_reply(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING("+15550099", sms->destination); /* reply goes back to the SAME number that asked */
    TEST_ASSERT_EQUAL_STRING("POS:35.699700,51.338100", sms->payload);
    return EC200U_OK;
}

void test_position_request_from_allowed_sender_is_answered_on_the_same_number(void) {
    Event    e;
    uint32_t len;

    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_position_request);
    EC200U_SMS_Send_StubWithCallback(capture_position_reply);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();

    TEST_ASSERT_EQUAL_STRING_MESSAGE("+15550099", s_last_checked_number,
        "Call_IsNumberAllowed() must be checked against the SENDER's number");

    /* The successful reply Send legitimately emits EVENT_SMS_SENT - what
     * must NOT happen is the command also falling through to
     * process_incoming()'s plain-message path (EVENT_SMS_RECEIVED). */
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_SENT, e.event_type);
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetEventQueue()),
        "a recognized command must never also fall through to EVENT_SMS_RECEIVED");
}

static EC200U_Result capture_no_fix_reply(const EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    TEST_ASSERT_EQUAL_STRING("POS:NO_FIX", sms->payload);
    return EC200U_OK;
}

void test_position_request_no_fix_payload_is_the_no_fix_marker(void) {
    s_stub_gps_position.valid = false;

    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_position_request);
    EC200U_SMS_Send_StubWithCallback(capture_no_fix_reply);
    EC200U_SMS_GetSendStatus_IgnoreAndReturn(EC200U_OK);

    SMS_Process();
}

void test_position_request_from_disallowed_sender_gets_no_reply(void) {
    s_stub_number_allowed = false;

    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_position_request);
    /* No EC200U_SMS_Send_Expect*() registered - CMock fails the test if it
     * is called unexpectedly, which is exactly the Fail-Closed assertion
     * here: an unauthorized sender must never receive any reply. */

    SMS_Process();

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetEventQueue()),
        "a disallowed sender's recognized command must still not fall through to EVENT_SMS_RECEIVED either");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

static EC200U_Result provide_incoming_non_matching_keyword(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099";
    sms->payload         = "LOCATE"; /* not an exact match for SMS_POSITION_REQUEST_KEYWORD ("LOC") */
    sms->payload_length  = 6;
    return EC200U_OK;
}

void test_non_matching_payload_falls_through_to_plain_message_event(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_non_matching_keyword);
    /* No EC200U_SMS_Send_Expect*() registered - a non-command payload must
     * never trigger a reply, only the existing EVENT_SMS_RECEIVED path. */

    SMS_Process();

    Event e;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &e, sizeof(e), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_SMS_RECEIVED, e.event_type);
}

/* --- Door Actuator Test command (sms_try_handle_door_command(),
 * SMS_DOOR_OPEN_KEYWORD/SMS_DOOR_CLOSE_KEYWORD = "DOOR_OPEN"/"DOOR_CLOSE"
 * today - see the provisional-syntax TODO in sms_management_config.h).
 * UNLIKE the Position Request path, this one physically actuates the
 * vehicle - explicit user decision to open it Pass-Through anyway, see
 * OPEN-ITEMS.md. --- */

static EC200U_Result provide_incoming_door_open(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099";
    sms->payload         = "DOOR_OPEN";
    sms->payload_length  = 9;
    return EC200U_OK;
}

static EC200U_Result provide_incoming_door_close(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099";
    sms->payload         = "DOOR_CLOSE";
    sms->payload_length  = 10;
    return EC200U_OK;
}

void test_door_open_command_is_routed_with_correct_command(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open);
    /* No EC200U_SMS_Send_Expect*() registered - this command never replies. */

    SMS_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_command_router_submit_call_count,
        "CommandRouter_Submit() must be called exactly once for a well-formed, allowed door command");
    TEST_ASSERT_EQUAL_INT(COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST, s_last_submitted_command.domain);
    TEST_ASSERT_EQUAL_UINT32(SMS_DOOR_ACTUATOR_TARGET_ECU_ID, s_last_submitted_command.target);
    TEST_ASSERT_EQUAL_UINT32(SMS_DOOR_ACTUATOR_PARAMETER_OPEN, s_last_submitted_command.parameter);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("+15550099", s_last_checked_number,
        "Call_IsNumberAllowed() must be checked against the SENDER's number");
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetEventQueue()),
        "a recognized command must never also fall through to EVENT_SMS_RECEIVED");
}

void test_door_close_command_carries_the_close_parameter(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_close);

    SMS_Process();

    TEST_ASSERT_EQUAL_INT(1, s_command_router_submit_call_count);
    TEST_ASSERT_EQUAL_INT(COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST, s_last_submitted_command.domain);
    TEST_ASSERT_EQUAL_UINT32(SMS_DOOR_ACTUATOR_PARAMETER_CLOSE, s_last_submitted_command.parameter);
}

void test_door_command_from_disallowed_sender_is_dropped(void) {
    s_stub_number_allowed = false;

    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open);
    /* No EC200U_SMS_Send_Expect*() registered - Fail-Closed, no reply either way. */

    SMS_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_command_router_submit_call_count,
        "an unauthorized sender's door command must never reach CommandRouter_Submit()");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_door_command_never_sends_a_reply_even_when_accepted(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open);
    /* No EC200U_SMS_Send_Expect*() registered - CMock fails the test if it
     * is called unexpectedly, which is exactly the assertion here: unlike
     * the Position Request path, this command never replies (see
     * sms_try_handle_door_command()'s own comment on why). */

    SMS_Process();

    TEST_ASSERT_EQUAL_INT(1, s_command_router_submit_call_count);
}

/* --- Door via direct GPIO relay (sms_try_handle_door_gpio_command(),
 * SMS_DOOR_OPEN_GPIO_KEYWORD/SMS_DOOR_CLOSE_GPIO_KEYWORD =
 * "DOOR_OPEN_GPIO"/"DOOR_CLOSE_GPIO" today) - second, independent
 * mechanism for the same physical action as the Diagnostic-based
 * commands above, explicit user request for "۲ کامند جدا". --- */

static EC200U_Result provide_incoming_door_open_gpio(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099";
    sms->payload         = "DOOR_OPEN_GPIO";
    sms->payload_length  = 14;
    return EC200U_OK;
}

static EC200U_Result provide_incoming_door_close_gpio(EC200U_SmsData* sms, int cmock_calls) {
    (void)cmock_calls;
    sms->destination    = "+15550099";
    sms->payload         = "DOOR_CLOSE_GPIO";
    sms->payload_length  = 15;
    return EC200U_OK;
}

void test_door_gpio_open_command_is_routed_with_correct_command(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open_gpio);

    SMS_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_command_router_submit_call_count,
        "CommandRouter_Submit() must be called exactly once for a well-formed, allowed door GPIO command");
    TEST_ASSERT_EQUAL_INT(COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO, s_last_submitted_command.domain);
    TEST_ASSERT_EQUAL_UINT32(1u, s_last_submitted_command.parameter);
    TEST_ASSERT_EQUAL_STRING("+15550099", s_last_checked_number);
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

void test_door_gpio_close_command_carries_the_close_parameter(void) {
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_close_gpio);

    SMS_Process();

    TEST_ASSERT_EQUAL_INT(1, s_command_router_submit_call_count);
    TEST_ASSERT_EQUAL_INT(COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO, s_last_submitted_command.domain);
    TEST_ASSERT_EQUAL_UINT32(0u, s_last_submitted_command.parameter);
}

void test_door_gpio_command_from_disallowed_sender_is_dropped(void) {
    s_stub_number_allowed = false;

    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open_gpio);

    SMS_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_command_router_submit_call_count,
        "an unauthorized sender's door GPIO command must never reach CommandRouter_Submit()");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

void test_door_gpio_and_diagnostic_door_keywords_are_distinct(void) {
    /* "DOOR_OPEN" (Diagnostic path) must never be mistaken for
     * "DOOR_OPEN_GPIO" (this path) or vice versa - both are exact-match
     * only, no prefix tolerance. */
    EC200U_SMS_GetReceived_StubWithCallback(provide_incoming_door_open); /* "DOOR_OPEN", not "DOOR_OPEN_GPIO" */

    SMS_Process();

    TEST_ASSERT_EQUAL_INT(1, s_command_router_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST, s_last_submitted_command.domain,
        "the shorter \"DOOR_OPEN\" keyword must route to the Diagnostic path, not the GPIO one");
}
