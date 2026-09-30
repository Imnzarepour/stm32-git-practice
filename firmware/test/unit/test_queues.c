#include "unity.h"
#include "shared/queues.h"
#include "Mockactuator_management.h" /* only need sizeof(ActuatorRequest); mocked so this test does not pull in actuator_management.c's own dependencies (Time Base, Hardware Proxy, Reset Request) */
#include <string.h>
TEST_SOURCE_FILE("infrastructure/queue.c")

void setUp(void) {
    Queues_Init();
}
void tearDown(void) {}

void test_all_seven_queues_start_empty(void) {
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetServerEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetTelemetryQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetActuatorRequestQueue()));
}

void test_queues_are_independent_not_shared(void) {
    uint32_t v = 42;
    TEST_ASSERT_TRUE(Queue_Push(Queues_GetEventQueue(), &v, sizeof(v)));

    /* فقط Event Queue باید پر شده باشه؛ بقیه دست‌نخورده باقی می‌مونن */
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetSmsRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetCallRequestQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetServerEventQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetTelemetryQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetActuatorRequestQueue()));
}

void test_fault_queue_capacity_matches_definition(void) {
    Queue* q = Queues_GetFaultQueue();
    TEST_ASSERT_EQUAL_UINT32(FAULT_QUEUE_CAPACITY_BYTES, Queue_FreeSpace(q));
}

void test_actuator_request_queue_capacity_matches_definition(void) {
    Queue* q = Queues_GetActuatorRequestQueue();
    TEST_ASSERT_EQUAL_UINT32(ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES, Queue_FreeSpace(q));
}

void test_telemetry_queue_is_largest(void) {
    /* Telemetry باید بزرگترین ظرفیت رو داشته باشه - طبق تصمیم بسته‌شده در
     * queue-infrastructure.md که حجم Telemetry بیشتر از بقیه‌ست */
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > EVENT_QUEUE_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > SMS_REQUEST_QUEUE_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > CALL_REQUEST_QUEUE_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > SERVER_EVENT_QUEUE_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > FAULT_QUEUE_CAPACITY_BYTES);
    TEST_ASSERT_TRUE(TELEMETRY_QUEUE_CAPACITY_BYTES > ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES);
}

void test_actuator_request_queue_is_not_small(void) {
    /* فرمان‌های حساس (ENGINE_CUT_OFF, DOOR_LOCK/UNLOCK, ALARM) از این صف
     * عبور می‌کنند؛ باید بدون نزدیک‌شدن به Drop-Oldest حداقل چند ده
     * ActuatorRequest همزمان جا بگیرد. اگر یک روز کسی این صف را کوچک کرد،
     * این تست باید Fail شود. */
    const uint32_t entry_size = (uint32_t)sizeof(uint32_t) + (uint32_t)sizeof(ActuatorRequest);
    const uint32_t min_requests = 16;
    TEST_ASSERT_TRUE(ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES >= entry_size * min_requests);

    /* هم‌ردیف بزرگ‌ترین صف‌های Request/Event دیگر باشد، نه کوچک‌تر از همه */
    TEST_ASSERT_TRUE(ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES >= CALL_REQUEST_QUEUE_CAPACITY_BYTES);
}

void test_actuator_request_queue_holds_many_requests_without_dropping(void) {
    Queue* q = Queues_GetActuatorRequestQueue();
    ActuatorRequest req = { ACTUATOR_ENGINE_CUT_OFF, ACTUATOR_CMD_ON, 1000 };

    uint32_t pushed = 0;
    while (Queue_FreeSpace(q) >= sizeof(uint32_t) + sizeof(req) && pushed < 16) {
        req.timestamp = 1000 + pushed;
        TEST_ASSERT_TRUE(Queue_Push(q, &req, sizeof(req)));
        pushed++;
    }
    TEST_ASSERT_EQUAL_UINT32(16, pushed);

    /* هیچ‌کدام از ۱۶ Request نباید Drop شده باشند؛ اولی هنوز باید در صف باشد */
    ActuatorRequest out;
    uint32_t length;
    TEST_ASSERT_TRUE(Queue_Pop(q, &out, sizeof(out), &length));
    TEST_ASSERT_EQUAL_UINT32(1000, out.timestamp);
}