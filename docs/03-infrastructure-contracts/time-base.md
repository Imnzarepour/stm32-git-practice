# پیوست فنی — تعریف و قرارداد Time Base

## ۱. هدف

**Time Base** زیرساخت مشترک تأمین زمان در Firmware است.

این زیرساخت یک مرجع یکنواخت برای:

* شمارش زمان اجرای سیستم
* اندازه‌گیری فاصله زمانی
* ایجاد Timestamp
* تشخیص Timeout
* زمان‌بندی داخلی Managementها

فراهم می‌کند.

Time Base **هیچ Policy یا رفتار محصولی را تعیین نمی‌کند**.

---

# ۲. جایگاه در معماری

```text
                ┌──────────────────────┐
                │     Managements      │
                │                      │
                │ GPS / Server / Firmware Update  │
                │ Diagnostic / ...     │
                └──────────┬───────────┘
                           │
                    Time Base API
                           │
                ┌──────────▼───────────┐
                │      Time Base       │
                └──────────┬───────────┘
                           │
                    Hardware Timer
                           │
                ┌──────────▼───────────┐
                │      MCU Timer       │
                └──────────────────────┘
```

---

# ۳. منبع زمان

Time Base باید از یک منبع زمان Monotonic در Firmware استفاده کند.

این زمان با Reset سیستم از مقدار اولیه شروع می‌شود و در طول اجرای Firmware افزایش می‌یابد.

```text
Reset
  ↓
Time Base = 0
  ↓
1 Tick
  ↓
2 Tick
  ↓
3 Tick
  ↓
...
```

این زمان برای اندازه‌گیری Duration و Timeout مناسب است.

---

# ۴. دو مفهوم زمان

در معماری باید بین دو مفهوم تفاوت وجود داشته باشد:

### ۴.۱ Monotonic Time

زمانی که فقط برای اندازه‌گیری فاصله زمانی استفاده می‌شود.

مثال:

```text
Start = 1000
Now   = 1500

Elapsed = 500
```

این زمان نباید با تغییر ساعت واقعی سیستم تغییر کند.

---

### ۴.۲ Real / Calendar Time

زمان واقعی مانند:

```text
2026-09-07 14:35:20
```

این زمان برای Timestamp رویدادهایی که باید به Server گزارش شوند کاربرد دارد.

منبع Real Time می‌تواند در آینده از RTC، شبکه یا GNSS تأمین شود.

بنابراین:

> **Monotonic Time و Calendar Time دو مفهوم متفاوت هستند و نباید در Contract با یکدیگر مخلوط شوند.**

---

# ۵. مسئولیت Time Base

Time Base مسئول موارد زیر است:

```text
System Tick
Monotonic Timestamp
Elapsed Time
Timeout Measurement
Calendar Time Access
```

در صورت وجود RTC، مدیریت Driver مربوط به RTC نیز باید از طریق لایه مناسب انجام شود.

---

# ۶. Time Base مالک Timing Policy نیست

Time Base فقط زمان را ارائه می‌کند.

مثلاً:

```text
GPS Management
    ↓
هر 1 ثانیه GPS را بررسی کن
```

این تصمیم متعلق به GPS Management است.

Time Base فقط می‌گوید:

```text
Now = 10500
```

بنابراین:

> **Time Base زمان را فراهم می‌کند؛ Management زمان‌بندی رفتار خودش را تعیین می‌کند.**

---

# ۷. System Tick

یک Tick سخت‌افزاری برای ایجاد Monotonic Time استفاده می‌شود.

```text
Hardware Timer
      ↓
     ISR
      ↓
Increment Tick
      ↓
Return
```

ISR باید حداقل کار ممکن را انجام دهد.

هیچ Processing مربوط به Management نباید داخل Tick ISR انجام شود.

---

# ۸. ISR Rule

Tick ISR نباید:

* `Process()` یک Management را اجرا کند.
* Timeout یک Management را پردازش کند.
* Queueهای سنگین را پردازش کند.
* Communication انجام دهد.
* Memory Allocation انجام دهد.
* Policy اجرا کند.

مدل صحیح:

```text
Timer Interrupt
      ↓
Update Time
      ↓
Return

Super Loop
      ↓
Management Process()
      ↓
Check Time
      ↓
Take Action
```

---

# ۹. Contract پایه

Contract پیشنهادی:

```c
uint32_t Time_GetTick(void);

bool Time_Elapsed(
    uint32_t start,
    uint32_t duration
);

uint32_t Time_GetTimestamp(void);
```

---

# ۱۰. GetTick

```c
uint32_t Time_GetTick(void);
```

وظیفه:

> مقدار فعلی Monotonic Tick را برمی‌گرداند.

ویژگی‌ها:

```text
Non-Blocking
Read-Only
No Side Effect
Fast
```

نمونه:

```c
uint32_t start = Time_GetTick();

/* Later */

if (Time_Elapsed(start, timeout))
{
    ...
}
```

---

# ۱۱. Elapsed Time

بهتر است Managementها خودشان محاسبات Tick را انجام ندهند.

به جای:

```c
if ((now - start) > timeout)
```

می‌توان Contract زیر را ارائه کرد:

```c
bool Time_Elapsed(start, duration);
```

این موضوع اجازه می‌دهد منطق مربوط به Wrap-Around در یک نقطه متمرکز شود.

---

# ۱۲. Wrap-Around

اگر Tick با `uint32_t` نگهداری شود، بعد از رسیدن به مقدار نهایی دوباره از صفر شروع می‌شود.

```text
0 → 1 → 2 → ... → MAX → 0 → 1 → ...
```

بنابراین Time Base باید محاسبه Elapsed Time را به‌صورت Wrap-Around Safe پیاده‌سازی کند.

Management نباید فرض کند:

```text
Now > Start
```

همیشه صحیح است.

---

# ۱۳. Timeout

Timeout باید بر اساس Monotonic Time محاسبه شود.

```text
Start
  ↓
Operation
  ↓
Process()
  ↓
Time_Elapsed()
  ↓
Timeout
```

این روش نسبت به استفاده از Calendar Time قابل اطمینان‌تر است.

---

# ۱۴. Calendar Time

برای Event و Telemetry، در صورت نیاز Timestamp واقعی سیستم مورد استفاده قرار می‌گیرد.

نمونه:

```c
typedef struct
{
    uint32_t unix_time;
    bool     valid;
} SystemTime;
```

API مفهومی:

```c
SystemTime Time_GetSystemTime(void);
```

اگر زمان واقعی معتبر نباشد:

```text
valid = false
```

و سیستم نباید زمان ساختگی را به‌عنوان زمان معتبر گزارش کند.

---

# ۱۵. Timestamp Event

در Contract مربوط به Event:

```c
typedef struct
{
    EventType event_type;
    uint32_t  event_id;
    uint32_t  timestamp;
} Event;
```

Timestamp باید از Time Infrastructure تأمین شود.

Event Generator نباید منبع مستقل زمان داشته باشد.

---

# ۱۶. Timestamp Telemetry

Telemetry نیز می‌تواند Timestamp داشته باشد:

```text
Provider
   ↓
Latest Value
   ↓
Timestamp
   ↓
Telemetry
```

این Timestamp باید نشان‌دهنده زمان معتبر اندازه‌گیری/دریافت داده باشد، نه الزاماً زمان ارسال آن به Server.

---

# ۱۷. تفاوت Measurement Time و Send Time

مثلاً:

```text
10:00:00
Battery = 12.4V
```

ارتباط قطع می‌شود.

داده در:

```text
10:05:00
```

به Server ارسال می‌شود.

Timestamp داده باید در صورت نیاز:

```text
Measurement Time = 10:00:00
Send Time        = 10:05:00
```

را از یکدیگر متمایز کند.

این موضوع مخصوصاً برای Telemetry ذخیره‌شده و Offline Mode اهمیت دارد.

---

# ۱۸. Power Mode

Time Base باید در `LOW_POWER` نیز مرجع زمانی قابل استفاده باقی بماند.

در صورت کاهش Clock یا ورود MCU به Sleep، معماری باید مشخص کند که منبع زمان چگونه استمرار پیدا می‌کند.

اصل:

> **کاهش مصرف نباید باعث شود Timeout و Timestampهای Firmware به‌صورت غیرقابل پیش‌بینی رفتار کنند.**

جزئیات Sleep/Timer Source وابسته به MCU و طراحی Hardware است.

---

# ۱۹. Resolution

Resolution زمان باید یکسان و مشخص باشد.

پیشنهاد Contract:

```text
Unit: millisecond
```

بنابراین:

```c
uint32_t tick;
```

به‌صورت Millisecond تعریف می‌شود.

مثلاً:

```text
1000 Tick = 1 second
```

اگر محدودیت Hardware یا Power Consumption نیاز به Resolution دیگری داشته باشد، این تصمیم باید در سطح Architecture ثبت شود و تمام Contractها بر اساس همان Unit عمل کنند.

---

# ۲۰. Overflow و عمر سیستم

با `uint32_t` و Resolution میلی‌ثانیه:

```text
2^32 ms ≈ 49.7 days
```

اما این موضوع نباید باعث محدودیت Timeoutهای معمول شود.

با طراحی Wrap-Around Safe، اندازه‌گیری فاصله‌های زمانی معمول در اطراف Overflow همچنان صحیح خواهد بود، مشروط به اینکه Duration مورد انتظار از محدوده مجاز محاسبه تجاوز نکند.

---

# ۲۱. عدم استفاده از Delay

Managementها نباید از Time Base برای ایجاد Blocking Delay استفاده کنند.

غلط:

```c
Time_Delay(1000);
```

یا:

```c
while (!Time_Elapsed(start, 1000))
{
}
```

صحیح:

```text
Process()
   ↓
Check Time
   ↓
Not Expired → Return

Process()
   ↓
Check Time
   ↓
Expired → Execute Step
```

این اصل برای حفظ Non-Blocking بودن Super Loop الزامی است.

---

# ۲۲. نمونه استفاده توسط Management

```c
void GPS_Process(void)
{
    if (!Time_Elapsed(gps_last_check, GPS_PERIOD))
    {
        return;
    }

    gps_last_check = Time_GetTick();

    /* Execute one small GPS step */
}
```

در این مدل:

```text
GPS Process
    ↓
Check Time
    ↓
Return immediately
```

در صورت نرسیدن زمان، هیچ انتظار یا Blocking اتفاق نمی‌افتد.

---

# ۲۳. State مالکیت زمان‌بندی

هر Management باید State مربوط به Timing خودش را نگهداری کند.

مثلاً:

```c
typedef struct
{
    uint32_t last_check;
    uint32_t timeout_start;
} GpsTiming;
```

Time Base نباید برای GPS، Server، Firmware Update و سایر Managementها Timer Object ایجاد و مدیریت کند، مگر اینکه در آینده نیاز معماری مشخصی برای چنین سرویسی ایجاد شود.

---

# ۲۴. Fake / Stub

Time Base باید قابل جایگزینی با Fake باشد.

```text
Production
Management
    ↓
Real Time Base
    ↓
Hardware Timer


Test
Management
    ↓
Fake Time Base
    ↓
Controlled Test Time
```

در Unit Test می‌توان زمان را به‌صورت مصنوعی جلو برد:

```text
Start = 1000

Advance 500 ms

Now = 1500
```

بدون نیاز به انتظار واقعی 500 میلی‌ثانیه.

این قابلیت برای تست Timeout، Retry و State Transition بسیار مهم است.

---

# ۲۵. Contract نهایی

```c
typedef uint32_t TimeTick;

typedef struct
{
    uint32_t unix_time;
    bool     valid;
} SystemTime;

TimeTick Time_GetTick(void);

bool Time_Elapsed(
    TimeTick start,
    TimeTick duration
);

SystemTime Time_GetSystemTime(void);
```

### ویژگی‌های API

```text
Non-Blocking
Deterministic
Fast
Wrap-Around Safe
No Business Logic
No Management Dependency
Testable
Fakeable
```

---

# ۲۶. مرزهای Time Base

| مورد                     | مالک                    |
| ------------------------ | ----------------------- |
| تولید Tick               | Time Base               |
| اندازه‌گیری Elapsed Time | Time Base               |
| Timeout Measurement      | Time Base               |
| تصمیم Timeout            | Management              |
| Periodic Policy          | Management              |
| Event Timestamp          | Time Base               |
| Telemetry Timestamp      | Time Base / Data Owner  |
| Calendar Time Source     | Time/RTC Infrastructure |
| Business Time Policy     | Management              |
| Delay                    | ممنوع                   |
| Blocking Wait            | ممنوع                   |

---

# ۲۷. اصل نهایی

Time Base باید یک سرویس بسیار کوچک و قابل اعتماد باشد.

```text
             Time Base
          ┌──────┼──────┐
          ↓      ↓      ↓
        GPS    Server   Firmware Update
          ↓      ↓      ↓
       Timing  Timeout Timestamp
```

و اصل معماری:

> **Time Base فقط زمان را فراهم می‌کند؛ هیچ Management را زمان‌بندی یا کنترل نمی‌کند.**

همچنین:

> **هیچ Management نباید برای انتظار سپری‌شدن زمان، Super Loop را متوقف کند؛ زمان باید در هر `Process()` بررسی شود و Management فقط یک Step کوچک را اجرا کند.**
