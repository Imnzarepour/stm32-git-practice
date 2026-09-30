#include "unity.h"
#include "shared/event_policy.h"
#include "shared/queues.h"
#include "shared/log_record.h"
#include "managements/sms_management.h"
#include "managements/call_management.h"
#include "Mockec200u_manager.h"
#include "Mockactuator_management.h"
#include "Mockpower_management.h"
#include "Mockreset_request.h"
#include "Mockconfiguration.h"
#include "Mockgps_management.h"
#include "Mockdiagnostic_management.h"
#include "Mockfirmware_update_management.h"
#include <string.h>

/* event_policy.c now includes managements/sms_management.h (for
 * SmsQueueItem) and managements/call_management.h (for CallQueueItem -
 * see each header's comment), which pulls sms_management.c and
 * call_management.c themselves into this test's link. None of their
 * functions are ever called here (EventPolicy_Process only pushes onto
 * queues, it never calls SMS_Init/Process or Call_Init/Process), but
 * their own dependencies (EC200U Manager, Actuator Management, Power
 * Management, Reset Request, Configuration, GPS Management's
 * GPS_GetPosition() (Position Request path), and - since
 * sms_management.c's Door command path added it -
 * shared/command_router.c (real, linked below, same reasoning as
 * test_sms_management.c/test_server_management.c) and, through it,
 * Diagnostic/Firmware Update Management's own Submit functions - still
 * need to resolve at link time - Mocked here since this test never
 * exercises them. shared/command_safety_policy.c is linked real (below)
 * instead of mocked - it is a pure, dependency-free function, same choice
 * test_sms_management.c/test_command_router.c make.
 *
 * Persisting an Event no longer touches Storage directly (see
 * event_policy.c's persist_to_log()) - it only pushes a LogRecord onto
 * the Log Queue, so infrastructure/storage.c and its Hardware Proxy Mock
 * are not needed here anymore; Log Infrastructure itself is
 * test_log.c's job. */
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("shared/command_router.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("shared/command_safety_policy.c")

void setUp(void) {
    Queues_Init();
    EventPolicy_Init();
}
void tearDown(void) {}

static void push_event(EventType type) {
    Event e = { type, 1, 12345 };
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetEventQueue(), &e, sizeof(e)));
}

void test_shock_detected_dispatches_to_sms_call_and_server(void) {
    push_event(EVENT_SHOCK_DETECTED);
    EventPolicy_Process();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetServerEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_sms_request_payload_carries_correct_event_type(void) {
    push_event(EVENT_SHOCK_DETECTED);
    EventPolicy_Process();

    /* Event Policy wraps its alert in SmsQueueItem so SMS Management can
     * tell it apart from a direct SmsRequest on the same queue - see the
     * comment on SmsQueueItem in managements/sms_management.h. */
    SmsQueueItem item;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetSmsRequestQueue(), &item, sizeof(item), &len));
    TEST_ASSERT_EQUAL_INT(SMS_QUEUE_ITEM_ALERT, item.kind);
    TEST_ASSERT_EQUAL_INT(EVENT_SHOCK_DETECTED, item.payload.alert.event_type);
}

void test_door_closed_dispatches_nowhere(void) {
    push_event(EVENT_DOOR_CLOSED);
    EventPolicy_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetServerEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_battery_disconnected_dispatches_to_sms_and_server_not_call(void) {
    /* Same routing as EVENT_LOW_BATTERY_DETECTED - see event_policy.c's
     * comment on this row for why (no documented severity decision exists
     * yet, OPEN-ITEMS.md #15). */
    push_event(EVENT_BATTERY_DISCONNECTED);
    EventPolicy_Process();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetServerEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_acc_on_only_goes_to_server(void) {
    push_event(EVENT_ACC_ON);
    EventPolicy_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetServerEventQueue()));
}

void test_unknown_event_type_creates_fault_not_crash(void) {
    Event e = { (EventType)9999, 1, 500 };
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetEventQueue(), &e, sizeof(e)));

    EventPolicy_Process();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetServerEventQueue()));
}

void test_process_with_empty_queue_does_nothing(void) {
    EventPolicy_Process();
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Persist (log-infrastructure.md section 0's correction: Log Record -> Log Queue, not a direct Storage_Write) --- */

void test_persist_true_pushes_a_log_record(void) {
    push_event(EVENT_SHOCK_DETECTED); /* persist=true in POLICY_TABLE */
    EventPolicy_Process();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetLogQueue()));

    LogRecord record;
    uint32_t  len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetLogQueue(), &record, sizeof(record), &len));
    TEST_ASSERT_EQUAL_INT(LOG_SOURCE_EVENT_POLICY, record.source);
    TEST_ASSERT_EQUAL_UINT32(EVENT_SHOCK_DETECTED, record.type);
    TEST_ASSERT_EQUAL_UINT32(12345u, record.timestamp);
    TEST_ASSERT_EQUAL_UINT32(sizeof(Event), record.payload_length);

    Event carried_event;
    memcpy(&carried_event, record.payload, sizeof(carried_event));
    TEST_ASSERT_EQUAL_INT(EVENT_SHOCK_DETECTED, carried_event.event_type);
}

void test_persist_false_pushes_nothing_to_the_log_queue(void) {
    push_event(EVENT_DOOR_CLOSED); /* persist=false in POLICY_TABLE */
    EventPolicy_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetLogQueue()));
}