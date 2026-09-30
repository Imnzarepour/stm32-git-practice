#include "infrastructure/time_base.h"

static volatile TimeTick s_tick = 0;
static bool s_rtc_valid = false;
static uint32_t s_rtc_unix_time = 0;

void Time_Init(void) {
    s_tick = 0;
    s_rtc_valid = false;
}

/* از Timer ISR فراخوانی می‌شود؛ باید فوق‌العاده سریع باشد (Section 7-8 سند Time Base) */
void Time_TickIncrement(void) {
    s_tick++; /* هر Tick = ۱ میلی‌ثانیه، طبق تصمیم بسته‌شده معماری */
}

TimeTick Time_GetTick(void) {
    return s_tick;
}

/* Wrap-Around Safe: تفاضل uint32_t حتی وقتی now دور زده باشد درست کار می‌کند */
bool Time_Elapsed(TimeTick start, TimeTick duration) {
    TimeTick now = Time_GetTick();
    TimeTick elapsed = (TimeTick)(now - start); /* overflow-safe unsigned subtraction */
    return elapsed >= duration;
}

SystemTime Time_GetSystemTime(void) {
    SystemTime t;
    t.unix_time = s_rtc_unix_time;
    t.valid = s_rtc_valid;
    return t;
}

/* TODO: منبع Calendar Time واقعی (RTC/شبکه/GNSS) باید این مقادیر را ست کند.
 * این تابع فقط برای زمانی است که آن منبع پیاده‌سازی شود. */
void Time_SetSystemTime(uint32_t unix_time) {
    s_rtc_unix_time = unix_time;
    s_rtc_valid = true;
}
