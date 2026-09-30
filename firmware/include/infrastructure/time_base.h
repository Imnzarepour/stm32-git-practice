#ifndef INFRA_TIME_BASE_H
#define INFRA_TIME_BASE_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/time-base.md
 * واحد: میلی‌ثانیه. Wrap-Around Safe. هیچ Delay/Blocking Wait مجاز نیست. */

typedef uint32_t TimeTick;

typedef struct {
    uint32_t unix_time;
    bool     valid;
} SystemTime;

TimeTick   Time_GetTick(void);
bool       Time_Elapsed(TimeTick start, TimeTick duration);
SystemTime Time_GetSystemTime(void);

/* فقط برای Tick ISR - جای دیگری صدا زده نشود. */
void Time_Init(void);
void Time_TickIncrement(void); /* از داخل Timer ISR فراخوانی می‌شود */

#endif /* INFRA_TIME_BASE_H */
