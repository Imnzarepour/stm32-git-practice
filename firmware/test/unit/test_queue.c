#include "unity.h"
#include "infrastructure/queue.h"
#include <string.h>

TEST_SOURCE_FILE("infrastructure/queue.c")

/* Direct tests of infrastructure/queue.c's own Queue_Push()/Queue_Pop()
 * primitives - the shared circular-buffer implementation every queue in
 * this firmware (Event, Fault, Telemetry, Log, SMS/Call/Actuator Request,
 * Server Event) is built on top of via shared/queues.c. Uses a private
 * Queue instance/buffer here, not any of shared/queues.c's per-Management
 * instances, so this file is entirely self-contained and does not depend
 * on any Management's own item shape.
 *
 * Contract source: docs/03-infrastructure-contracts/queue-infrastructure.md
 * "سیاست پر شدن: Drop Oldest -> Insert Newest (همیشه، بدون استثنا)."
 * Before this file existed, nothing in the project actually exercised the
 * Drop-Oldest code path (queue_drop_oldest() in queue.c) - every existing
 * queue-related test (test_queues.c) either stays comfortably within
 * capacity or explicitly proves it does NOT drop under normal load
 * (test_actuator_request_queue_holds_many_requests_without_dropping). This
 * file closes that gap directly against the primitive itself. */

#define TEST_QUEUE_CAPACITY_BYTES 24u /* holds exactly 3 uint32_t items: 3 * (4-byte length prefix + 4-byte payload) */

static Queue   s_queue;
static uint8_t s_buffer[TEST_QUEUE_CAPACITY_BYTES];

void setUp(void) {
    TEST_ASSERT_TRUE(Queue_Init(&s_queue, s_buffer, sizeof(s_buffer)));
}

void tearDown(void) {}

static bool push_u32(uint32_t value) {
    return Queue_Push(&s_queue, &value, sizeof(value));
}

static bool pop_u32(uint32_t* out) {
    uint32_t length;
    bool ok = Queue_Pop(&s_queue, out, sizeof(*out), &length);
    if (ok) {
        TEST_ASSERT_EQUAL_UINT32(sizeof(*out), length);
    }
    return ok;
}

/* --- Init --- */

void test_init_rejects_null_queue_or_buffer(void) {
    TEST_ASSERT_FALSE(Queue_Init(NULL, s_buffer, sizeof(s_buffer)));
    TEST_ASSERT_FALSE(Queue_Init(&s_queue, NULL, sizeof(s_buffer)));
}

void test_init_rejects_a_capacity_too_small_to_hold_even_the_length_prefix(void) {
    TEST_ASSERT_FALSE(Queue_Init(&s_queue, s_buffer, sizeof(uint32_t) - 1u));
}

void test_new_queue_is_empty(void) {
    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
    TEST_ASSERT_FALSE(Queue_IsFull(&s_queue));
    TEST_ASSERT_EQUAL_UINT32(0u, Queue_Count(&s_queue));
    TEST_ASSERT_EQUAL_UINT32(TEST_QUEUE_CAPACITY_BYTES, Queue_FreeSpace(&s_queue));
}

/* --- Basic Push/Pop --- */

void test_push_pop_roundtrip_preserves_order_and_content(void) {
    TEST_ASSERT_TRUE(push_u32(11u));
    TEST_ASSERT_TRUE(push_u32(22u));

    uint32_t value;
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(11u, value);
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(22u, value);
    TEST_ASSERT_FALSE(pop_u32(&value));
}

void test_queue_reports_full_at_exact_capacity(void) {
    TEST_ASSERT_TRUE(push_u32(1u));
    TEST_ASSERT_TRUE(push_u32(2u));
    TEST_ASSERT_TRUE(push_u32(3u));

    TEST_ASSERT_TRUE(Queue_IsFull(&s_queue));
    TEST_ASSERT_EQUAL_UINT32(TEST_QUEUE_CAPACITY_BYTES, Queue_Count(&s_queue));
    TEST_ASSERT_EQUAL_UINT32(0u, Queue_FreeSpace(&s_queue));
}

void test_push_rejects_an_item_larger_than_the_entire_queue(void) {
    uint8_t oversized[TEST_QUEUE_CAPACITY_BYTES]; /* + the 4-byte length prefix would exceed capacity entirely */
    memset(oversized, 0xAA, sizeof(oversized));

    TEST_ASSERT_FALSE(Queue_Push(&s_queue, oversized, sizeof(oversized)));
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(&s_queue), "a rejected oversized item must never partially corrupt the queue");
}

void test_pop_with_a_too_small_caller_buffer_leaves_the_item_in_place(void) {
    TEST_ASSERT_TRUE(push_u32(0xDEADBEEFu));

    uint8_t  too_small[2];
    uint32_t length;
    TEST_ASSERT_FALSE(Queue_Pop(&s_queue, too_small, sizeof(too_small), &length));
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(&s_queue), "a rejected Pop (buffer too small) must not consume the item");

    uint32_t value;
    TEST_ASSERT_TRUE_MESSAGE(pop_u32(&value), "the same item must still be retrievable with an adequately-sized buffer");
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, value);
}

void test_push_and_pop_null_arguments_are_rejected_not_crashed(void) {
    uint32_t value = 1u;
    uint32_t length;
    TEST_ASSERT_FALSE(Queue_Push(NULL, &value, sizeof(value)));
    TEST_ASSERT_FALSE(Queue_Push(&s_queue, NULL, sizeof(value)));
    TEST_ASSERT_FALSE(Queue_Pop(NULL, &value, sizeof(value), &length));
}

void test_clear_empties_the_queue(void) {
    push_u32(1u);
    push_u32(2u);

    Queue_Clear(&s_queue);

    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
    TEST_ASSERT_EQUAL_UINT32(0u, Queue_Count(&s_queue));
}

/* --- The core ask: Drop-Oldest -> Insert-Newest, "همیشه، بدون استثنا" --- */

void test_push_drops_the_oldest_item_when_full_and_keeps_the_newest(void) {
    TEST_ASSERT_TRUE(push_u32(100u)); /* will be dropped */
    TEST_ASSERT_TRUE(push_u32(200u));
    TEST_ASSERT_TRUE(push_u32(300u));
    TEST_ASSERT_TRUE(Queue_IsFull(&s_queue));

    TEST_ASSERT_TRUE_MESSAGE(push_u32(400u),
        "Push must still report success when it has to drop an old item to make room - Drop-Oldest is not a failure");

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(TEST_QUEUE_CAPACITY_BYTES, Queue_Count(&s_queue),
        "still exactly full - one dropped, one inserted");

    uint32_t value;
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(200u, value,
        "the oldest item (100) must have been dropped - this must now be the oldest remaining");
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(300u, value);
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(400u, value, "the newest item must always be kept");
    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
}

void test_push_can_drop_more_than_one_oldest_item_if_needed(void) {
    /* Fill with 3 small items, then push one larger item that needs more
     * room than dropping just a single oldest item would free - must drop
     * TWO, not stop after one. */
    TEST_ASSERT_TRUE(push_u32(1u));
    TEST_ASSERT_TRUE(push_u32(2u));
    TEST_ASSERT_TRUE(push_u32(3u));

    uint64_t big_value = 0x1122334455667788ULL; /* entry_size = 4 + 8 = 12 bytes - dropping only one 8-byte entry (leaves 8 free) is not enough */
    TEST_ASSERT_TRUE(Queue_Push(&s_queue, &big_value, sizeof(big_value)));

    uint32_t value;
    TEST_ASSERT_TRUE_MESSAGE(pop_u32(&value), "items 1 and 2 must both have been dropped");
    TEST_ASSERT_EQUAL_UINT32(3u, value);

    uint64_t popped_big;
    uint32_t length;
    TEST_ASSERT_TRUE(Queue_Pop(&s_queue, &popped_big, sizeof(popped_big), &length));
    TEST_ASSERT_EQUAL_UINT32(sizeof(big_value), length);
    TEST_ASSERT_EQUAL_UINT64(big_value, popped_big);
    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
}

/* --- Circular buffer wrap-around correctness --- */

void test_repeated_push_pop_cycles_correctly_wrap_around_the_buffer(void) {
    /* Each push+pop cycle advances head/tail by one entry (8 bytes) inside
     * a 24-byte buffer - after just 3 cycles, head/tail have already
     * wrapped past the end of the buffer at least once. 20 cycles gives
     * several full wraps, proving the circular (%) arithmetic in
     * Queue_Push()/Queue_Pop() stays correct indefinitely, not just within
     * the first pass through the raw buffer. */
    uint32_t i;
    for (i = 0; i < 20u; i++) {
        uint32_t value;
        TEST_ASSERT_TRUE(push_u32(i));
        TEST_ASSERT_TRUE(pop_u32(&value));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(i, value, "value must survive a wrap-around push/pop cycle unchanged");
    }
    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
}

void test_drop_oldest_still_correct_after_the_buffer_has_wrapped(void) {
    /* Warm up with several push/pop cycles first, so the Drop-Oldest
     * scenario below starts from a head/tail position that has already
     * wrapped past the end of the raw buffer - proving Drop-Oldest itself
     * stays correct under wrap-around, not just from a fresh buffer. */
    uint32_t i;
    for (i = 0; i < 7u; i++) {
        uint32_t value;
        push_u32(0xFFu);
        pop_u32(&value);
    }

    TEST_ASSERT_TRUE(push_u32(100u)); /* will be dropped */
    TEST_ASSERT_TRUE(push_u32(200u));
    TEST_ASSERT_TRUE(push_u32(300u));
    TEST_ASSERT_TRUE(push_u32(400u));

    uint32_t value;
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(200u, value);
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(300u, value);
    TEST_ASSERT_TRUE(pop_u32(&value));
    TEST_ASSERT_EQUAL_UINT32(400u, value);
    TEST_ASSERT_TRUE(Queue_IsEmpty(&s_queue));
}
