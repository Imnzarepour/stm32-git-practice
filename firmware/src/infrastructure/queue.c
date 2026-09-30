#include "infrastructure/queue.h"
#include <string.h>

/* پیاده‌سازی Queue بر پایه Buffer دایره‌ای با فرمت [uint32 length][payload].
 * سیاست پر شدن: Drop Oldest -> Insert Newest (طبق Contract، بدون استثنا). */

bool Queue_Init(Queue* queue, uint8_t* buffer, uint32_t capacity) {
    if (!queue || !buffer || capacity < sizeof(uint32_t)) return false;
    queue->buffer = buffer;
    queue->capacity = capacity;
    queue->head = 0;
    queue->tail = 0;
    queue->used = 0;
    return true;
}

static void queue_drop_oldest(Queue* q) {
    if (q->used == 0) return;
    uint32_t len;
    memcpy(&len, &q->buffer[q->head], sizeof(uint32_t));
    uint32_t entry_size = sizeof(uint32_t) + len;
    q->head = (q->head + entry_size) % q->capacity;
    q->used -= entry_size;
}

bool Queue_Push(Queue* queue, const void* data, uint32_t length) {
    if (!queue || !data) return false;
    uint32_t entry_size = sizeof(uint32_t) + length;
    if (entry_size > queue->capacity) return false; /* Item بزرگ‌تر از کل Queue - قابل ذخیره نیست */

    /* طبق Contract: اگر جا نیست، قدیمی‌ترین آیتم(ها) Drop می‌شود تا جا باز شود */
    while (queue->capacity - queue->used < entry_size) {
        queue_drop_oldest(queue);
    }

    /* نوشتن length (ممکن است دور بزند، بایت‌به‌بایت می‌نویسیم) */
    uint8_t len_bytes[sizeof(uint32_t)];
    memcpy(len_bytes, &length, sizeof(uint32_t));
    for (uint32_t i = 0; i < sizeof(uint32_t); i++) {
        queue->buffer[(queue->tail + i) % queue->capacity] = len_bytes[i];
    }
    uint32_t data_start = (queue->tail + sizeof(uint32_t)) % queue->capacity;
    const uint8_t* src = (const uint8_t*)data;
    for (uint32_t i = 0; i < length; i++) {
        queue->buffer[(data_start + i) % queue->capacity] = src[i];
    }

    queue->tail = (queue->tail + entry_size) % queue->capacity;
    queue->used += entry_size;
    return true;
}

bool Queue_Pop(Queue* queue, void* data, uint32_t buffer_size, uint32_t* length) {
    if (!queue || queue->used == 0) return false;

    uint8_t len_bytes[sizeof(uint32_t)];
    for (uint32_t i = 0; i < sizeof(uint32_t); i++) {
        len_bytes[i] = queue->buffer[(queue->head + i) % queue->capacity];
    }
    uint32_t len;
    memcpy(&len, len_bytes, sizeof(uint32_t));

    if (len > buffer_size) return false; /* Caller Buffer کوچک است */

    uint32_t data_start = (queue->head + sizeof(uint32_t)) % queue->capacity;
    uint8_t* dst = (uint8_t*)data;
    for (uint32_t i = 0; i < len; i++) {
        dst[i] = queue->buffer[(data_start + i) % queue->capacity];
    }

    uint32_t entry_size = sizeof(uint32_t) + len;
    queue->head = (queue->head + entry_size) % queue->capacity;
    queue->used -= entry_size;
    if (length) *length = len;
    return true;
}

bool Queue_IsEmpty(const Queue* queue) { return queue->used == 0; }
bool Queue_IsFull(const Queue* queue) { return queue->used >= queue->capacity; }
uint32_t Queue_Count(const Queue* queue) { return queue->used; } /* بر اساس بایت، نه تعداد Item - طبق Byte Capacity */
uint32_t Queue_FreeSpace(const Queue* queue) { return queue->capacity - queue->used; }
void Queue_Clear(Queue* queue) { queue->head = 0; queue->tail = 0; queue->used = 0; }
