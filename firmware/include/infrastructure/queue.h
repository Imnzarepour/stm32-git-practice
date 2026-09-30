#ifndef INFRA_QUEUE_H
#define INFRA_QUEUE_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/queue-infrastructure.md
 * Policy پر شدن Queue: Drop Oldest -> Insert Newest (همیشه، بدون استثنا).
 * ظرفیت بر اساس Byte Capacity است، نه تعداد Item (چون Payloadها هم‌اندازه نیستند).
 * Queue تصمیم نمی‌گیرد؛ فقط نگه می‌دارد. Policy/Priority مسئولیت Consumer است. */

typedef struct {
    uint8_t* buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t used;
} Queue;

bool     Queue_Init(Queue* queue, uint8_t* buffer, uint32_t capacity);
bool     Queue_Push(Queue* queue, const void* data, uint32_t length);
bool     Queue_Pop(Queue* queue, void* data, uint32_t buffer_size, uint32_t* length);
bool     Queue_IsEmpty(const Queue* queue);
bool     Queue_IsFull(const Queue* queue);
uint32_t Queue_Count(const Queue* queue);
uint32_t Queue_FreeSpace(const Queue* queue);
void     Queue_Clear(Queue* queue);

#endif /* INFRA_QUEUE_H */
