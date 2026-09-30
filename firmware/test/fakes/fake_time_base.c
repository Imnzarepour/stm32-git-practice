#include "infrastructure/time_base.h"

/* Fake Time Base برای Unit Test - امکان جلو بردن زمان بدون انتظار واقعی
 * طبق اصل صریح time-base.md بخش ۲۴ (Fake/Stub). */
static TimeTick s_fake_tick = 0;

void Time_Init(void) { s_fake_tick = 0; }
void Time_TickIncrement(void) { s_fake_tick++; }
TimeTick Time_GetTick(void) { return s_fake_tick; }
bool Time_Elapsed(TimeTick start, TimeTick duration) {
    return (TimeTick)(s_fake_tick - start) >= duration;
}
SystemTime Time_GetSystemTime(void) {
    SystemTime t = {0, false};
    return t;
}

/* برای تست: زمان را مستقیم جلو ببر */
void FakeTime_Advance(uint32_t ms) { s_fake_tick += ms; }
void FakeTime_Set(TimeTick tick) { s_fake_tick = tick; }
