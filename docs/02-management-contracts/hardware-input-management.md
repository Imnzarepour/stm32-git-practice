# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Hardware Input Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها و خروجی‌های **Hardware Input Management** در معماری Firmware دستگاه TCU را مشخص می‌کند.

هدف، ایجاد یک مرز مشخص میان سخت‌افزار ورودی و Managementهای کاربردی است؛ به‌گونه‌ای که Managementهای دیگر مستقیماً با GPIO، ADC، نویز، Debounce یا تبدیل مقادیر خام سخت‌افزار درگیر نشوند.

---

# ۲. جایگاه Hardware Input Management

Hardware Input Management مالک ورودی‌های فیزیکی دستگاه است.

این ورودی‌ها شامل:

* Digital Inputs / GPIO
* Analog Inputs / ADC

هستند.

نمونه ورودی‌های اصلی:

```text
Digital:
    ACC
    Door
    Shock
    ...

Analog:
    Battery Voltage
    ...
```

اصل حاکم:

> **Hardware Input Management مقدار فیزیکی را از سخت‌افزار دریافت، معتبرسازی و تبدیل می‌کند و آن را به‌صورت داده قابل مصرف و Event در اختیار سایر Managementها قرار می‌دهد.**

---

# ۳. مسئولیت‌ها

## ۳-۱. مدیریت Digital Input

Hardware Input Management مسئول دریافت و پردازش Digital Inputها است.

مسیر کلی:

```text
GPIO
 ↓
Qualification / Debounce
 ↓
Stable Digital Value
 ↓
Latest Value
```

Managementهای دیگر نباید مستقیماً GPIO را بخوانند.

---

## ۳-۲. Debounce و Qualification

Hardware Input Management مسئول حذف اثرات نویز و ناپایداری ورودی Digital است.

بنابراین مقدار خام GPIO مستقیماً به عنوان وضعیت سیستم منتشر نمی‌شود.

```text
Raw GPIO
   ↓
Debounce / Qualification
   ↓
Valid Stable State
```

جزئیات الگوریتم Debounce، زمان‌ها و روش Qualification در طراحی داخلی این Management تعیین می‌شود و بخشی از Contract سایر Managementها نیست.

---

## ۳-۳. ارائه Latest Value

برای هر Input که نیاز به وضعیت جاری دارد، Hardware Input Management آخرین مقدار معتبر را نگهداری می‌کند.

مثال:

```text
ACC Status
Door Status
Shock Status
```

مصرف‌کننده می‌تواند وضعیت جاری را بدون دسترسی مستقیم به سخت‌افزار دریافت کند.

```text
Hardware Input Management
          │
          ├── ACC = ON
          ├── Door = OPEN
          └── Shock = INACTIVE
```

---

## ۳-۴. تولید Event

Hardware Input Management علاوه بر Latest Value، در صورت تعریف شدن Condition مربوطه می‌تواند Event تولید کند.

مثال:

```text
GPIO
 ↓
Qualification
 ↓
Door State Changed
 ↓
DoorOpened Event
```

یا:

```text
Shock Input
 ↓
Qualification
 ↓
Shock Detected
 ↓
ShockDetected Event
```

Event زمانی ایجاد می‌شود که شرط تعریف‌شده برای آن Input برقرار شود.

---

# ۴. مدیریت Analog Input

Hardware Input Management مالک دریافت و پردازش Analog Inputها است.

مسیر:

```text
ADC Raw
   ↓
Conversion / Calibration
   ↓
Engineering Value
   ↓
Latest Value
```

مقدار خام ADC مستقیماً در اختیار Managementهای دیگر قرار نمی‌گیرد.

---

## ۴-۱. مقدار مهندسی‌شده

خروجی Analog Input باید در قالب مقدار مهندسی‌شده ارائه شود.

برای مثال:

```text
ADC Raw
   ↓
Voltage Conversion
   ↓
Battery Voltage = 12.4 V
```

بنابراین مصرف‌کننده نباید بداند:

* ADC Resolution چیست
* Reference Voltage چیست
* تقسیم مقاومتی چگونه است
* Calibration چگونه انجام شده است

این جزئیات در مرز Hardware Input Management پنهان می‌شوند.

---

# ۵. تشخیص Low Battery

تشخیص وضعیت Low Battery نیز مسئولیت Hardware Input Management است.

مسیر:

```text
Battery Voltage
       ↓
Threshold / Condition
       ↓
Low Battery Detected
       ↓
Event
```

بنابراین Managementهای دیگر نیازی ندارند خودشان مقدار Battery Voltage را بررسی کنند تا Low Battery را تشخیص دهند.

در نتیجه این Manager می‌تواند هر دو مورد را ارائه کند:

```text
Latest Value:
    Battery Voltage

Event:
    LowBatteryDetected
```

Thresholdها و شرایط دقیق تشخیص در Contract داخلی Battery Input تعریف خواهند شد.

---

# ۶. مدیریت ACC

ACC نیز به‌عنوان یک Hardware Input متعلق به این Management است.

مسیر:

```text
ACC GPIO
    ↓
Debounce / Qualification
    ↓
ACC Status
    ↓
Latest Value
```

Power Management این وضعیت را مصرف می‌کند:

```text
Hardware Input Management
          │
          │ ACC Status
          ▼
   Power Management
```

بنابراین Power Management مستقیماً به GPIO یا Driver مربوط به ACC دسترسی ندارد.

---

# ۷. خروجی‌ها

Hardware Input Management دو نوع خروجی اصلی دارد:

### ۷-۱. Latest Value

آخرین مقدار معتبر Input.

مثال:

```text
ACC Status
Door Status
Shock Status
Battery Voltage
```

---

### ۷-۲. Event

رخدادهای استخراج‌شده از Inputها.

مثال:

```text
DoorOpened
DoorClosed
ShockDetected
LowBatteryDetected
ACCOn
ACCOff
```

Eventها طبق Event Contract عمومی سیستم ایجاد می‌شوند.

---

# ۸. مدل ارتباطی

```text
                    ┌──────────────────────────┐
                    │ Hardware Input           │
                    │ Management               │
                    └────────────┬─────────────┘
                                 │
                    ┌────────────┴────────────┐
                    │                         │
                    ▼                         ▼
              Latest Value                  Event
                    │                         │
          ┌─────────┼─────────┐              │
          ▼         ▼         ▼              ▼
         Power     Server    سایر         Event
       Management Management Consumers     Policy
```

---

# ۹. رابطه با Event Architecture

Hardware Input Management **مالک Event Policy نیست**.

این Management فقط:

> **Detection → Event**

را انجام می‌دهد.

مثلاً:

```text
Shock Input
    ↓
Qualification
    ↓
ShockDetected Event
    ↓
Event Policy
    ├── Server
    ├── SMS
    ├── Call
    └── Store
```

اینکه با `ShockDetected` چه اقدامی انجام شود، خارج از مسئولیت Hardware Input Management است.

---

# ۱۰. رابطه با Power Management

رابطه ACC به‌صورت مشخص:

```text
ACC Hardware
      ↓
Hardware Input Management
      ↓
Qualified ACC Status
      ↓
Power Management
      ↓
NORMAL / LOW_POWER
```

Power Management فقط وضعیت معتبر ACC را مصرف می‌کند.

---

# ۱۱. رفتار در Power Mode

Hardware Input Management نیز مانند سایر Managementها باید با Power Mode سازگار باشد.

در `NORMAL`:

```text
Input Processing
    → Normal Activity
```

در `LOW_POWER`:

```text
Input Processing
    → Reduced Activity
```

اما کاهش فعالیت نباید باعث نقض Contract ورودی‌های ضروری شود.

به‌خصوص Inputهایی که برای Wake-up، امنیت یا تشخیص رخدادهای ضروری مورد نیاز هستند باید مطابق نیاز سیستم همچنان قابل پایش باشند.

جزئیات این رفتار در Contract هر Input تعیین خواهد شد.

---

# ۱۲. Contract سطح معماری

```text
Hardware Input Management

Responsibility:
    Digital Input Acquisition
    Debounce / Qualification
    Analog Input Acquisition
    Engineering Value Conversion
    Latest Value Management
    Input Event Detection
    Low Battery Detection

Inputs:
    GPIO / Digital Hardware Inputs
    ADC / Analog Hardware Inputs
    System Time
    Power Mode

Outputs:
    Digital Latest Values
    Analog Engineering Values
    Input Events
    Low Battery Event

Examples:
    ACC Status
    Door Status
    Shock Status
    Battery Voltage
    LowBatteryDetected

Constraints:
    Non-Blocking
    Super Loop Compatible
    No direct Consumer access to GPIO/ADC
    No Event Policy ownership
    No Action execution
```

---

# ۱۳. مرز با سایر اجزا

| جزء                 | رابطه با Hardware Input Management      |
| ------------------- | --------------------------------------- |
| Power Management    | مصرف ACC Status                         |
| Event / Policy      | مصرف Eventهای تشخیص‌داده‌شده            |
| Server Management   | مصرف Latest Value یا Event در صورت نیاز |
| SMS Management      | مصرف Event از مسیر Policy               |
| Call Management     | مصرف Event از مسیر Policy               |
| Actuator Management | مستقل از Input Acquisition              |
| GPIO Driver         | تأمین دسترسی سخت‌افزاری                 |
| ADC Driver          | تأمین داده خام ADC                      |
| Time Base           | تأمین زمان مورد نیاز Qualification      |
| System Management   | مصرف Fault/Status در صورت نیاز          |

---

# ۱۴. اصل نهایی

Hardware Input Management مرز بین **Physical Input** و **Firmware Logic** است.

اصل نهایی:

> **هیچ Management کاربردی نباید مستقیماً GPIO یا ADC را بخواند.**

تمام ورودی‌های فیزیکی ابتدا توسط Hardware Input Management:

```text
Acquire
   ↓
Qualify / Convert
   ↓
Latest Value / Event
   ↓
Consumers
```

می‌شوند.

در نتیجه:

> **Hardware Input Management مالک Input است؛ Event Policy مالک تصمیم است؛ و Managementهای دیگر مالک Action مربوط به Capability خود هستند.**
