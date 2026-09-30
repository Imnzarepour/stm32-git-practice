# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Actuator Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **Actuator Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

Actuator Management مالک اجرای فرمان‌های خروجی فیزیکی سیستم است؛ مانند قطع موتور، آژیر و قفل/بازکردن درب.

---

## ۲. جایگاه Actuator Management

```text id="1v7y8x"
Policy / Command Owner
        ↓
    Actuator Request
        ↓
Actuator Management
        ↓
 Physical Output
```

Actuator Management فقط **فرمان را اجرا می‌کند** و مالک تصمیم‌گیری درباره اینکه چه زمانی و تحت چه شرایطی فرمان صادر شود نیست.

---

## ۳. مسئولیت‌ها

Actuator Management مسئول موارد زیر است:

```text id="y0f4n8"
Receive Actuator Request
Validate Request
Execute Output Command
Maintain Output State
Report Execution Status
Report Actuator Fault
```

نمونه Actuatorها:

* Engine Cut-Off
* Alarm / Buzzer
* Door Lock
* Door Unlock

---

## ۴. دریافت فرمان

فرمان می‌تواند از منابع مختلف ایجاد شود:

```text id="j5c8s2"
Server
   ↓
Server Management
   ↓
Actuator Request
   ↓
Actuator Management
```

یا:

```text id="x2q7ma"
SMS
 ↓
SMS Management
 ↓
Actuator Request
 ↓
Actuator Management
```

هر Management دیگر نیز در صورت داشتن اختیار طبق Product Policy می‌تواند Request ایجاد کند.

---

## ۵. Actuator Request

Contract مفهومی:

```c id="4m8k1q"
typedef struct
{
    ActuatorId actuator;
    ActuatorCommand command;
    uint32_t timestamp;
} ActuatorRequest;
```

نمونه:

```text id="b7n1re"
ActuatorId:
    ENGINE_CUT_OFF
    ALARM
    DOOR_LOCK
    DOOR_UNLOCK

ActuatorCommand:
    ON
    OFF
    ACTIVATE
    DEACTIVATE
```

جزئیات دقیق Enumها در Implementation Contract تعیین می‌شود.

---

## ۶. اعتبارسنجی فرمان

Actuator Management باید حداقل اعتبارسنجی ساختاری انجام دهد:

```text id="x3v9kd"
Request
   ↓
Valid Actuator?
Valid Command?
Valid Request Format?
   ↓
Execute
```

اما مواردی مانند:

* آیا خودرو در حال حرکت است؟
* آیا کاربر مجاز است؟
* آیا این عملیات از نظر کسب‌وکار مجاز است؟
* آیا شرایط امنیتی برقرار است؟

در Actuator Management تصمیم‌گیری نمی‌شوند.

این تصمیم‌ها متعلق به صادرکننده فرمان یا Policy مربوطه هستند.

---

## ۷. اجرای خروجی

اجرای Actuator باید کوتاه و Non-Blocking باشد.

```text id="t4k6pa"
Request
  ↓
Validate
  ↓
Set Output
  ↓
Update State
  ↓
Return
```

Actuator Management نباید برای مدت طولانی منتظر بماند.

---

## ۸. State خروجی

Actuator Management وضعیت آخرین فرمان/خروجی را نگهداری می‌کند.

نمونه:

```c id="u3z8cw"
typedef struct
{
    ActuatorCommand command;
    bool            active;
    uint32_t        timestamp;
} ActuatorState;
```

مصرف‌کننده در صورت نیاز می‌تواند وضعیت فعلی را بخواند.

```text
Actuator_GetState(ActuatorId id)
```

این API باید:

* Non-Blocking
* Read-Only
* بدون Side Effect

باشد.

---

## ۹. Feedback

در معماری فعلی، Actuatorها Feedback فیزیکی مستقل ندارند.

بنابراین:

```text id="e5k9zn"
Request
  ↓
Output Command
  ↓
State Update
```

به معنی تأیید فیزیکی عملکرد Actuator نیست.

مثلاً `DOOR_LOCK = ON` به معنی «فرمان قفل صادر شده» است، نه الزاماً «درب واقعاً قفل شده است».

در صورت اضافه‌شدن Feedback سخت‌افزاری در آینده، Contract می‌تواند توسعه پیدا کند.

---

## ۱۰. Event

Actuator Management در صورت نیاز می‌تواند Event ایجاد کند؛ مثلاً:

```text id="m6p2fs"
ACTUATOR_COMMAND_ACCEPTED
ACTUATOR_COMMAND_REJECTED
ACTUATOR_FAULT
```

اما Event باید فقط وقوع یک وضعیت/عملیات را گزارش کند.

Policy مربوط به ارسال آن به Server، SMS، ذخیره‌سازی یا سایر اقدامات، متعلق به لایه Policy است.

---

## ۱۱. Fault و Recovery

نمونه Faultها:

```text id="q8r3dv"
Invalid Request
Invalid Actuator
Output Driver Failure
Hardware Fault
```

Actuator Management ابتدا Recovery محلی را انجام می‌دهد، در صورت امکان.

اگر خطا نیازمند Reset باشد:

```text id="f2w6sa"
Actuator Management
       ↓
Reset Request
       ↓
System Management
```

Actuator Management مستقیماً MCU را Reset نمی‌کند.

---

## ۱۲. Power Mode

Actuator Management باید Power Mode را در رفتار خود لحاظ کند.

با این حال، Actuatorهای مورد نیاز سیستم نباید صرفاً به دلیل ورود به `LOW_POWER` غیرقابل‌استفاده شوند، مگر اینکه Product Policy صراحتاً چنین محدودیتی تعیین کند.

```text id="p3v7hc"
NORMAL
   ↓
Normal Actuator Operation

LOW_POWER
   ↓
Reduced Internal Activity
   ↓
Actuator Capability Remains Available
```

---

## ۱۳. عدم وجود FSM پیچیده

برای Actuatorهای فعلی، معماری نیازی به FSM پیچیده ندارد.

مدل پایه:

```text id="z5d1yt"
Request
  ↓
Validate
  ↓
Execute
  ↓
Update State
```

اگر در آینده Actuator دارای Feedback، Timeout یا Sequence چندمرحله‌ای شود، FSM داخلی می‌تواند اضافه شود؛ بدون تغییر در معماری کل سیستم.

---

## ۱۴. Contract سطح معماری

```text id="r9k2bw"
Actuator Management

Responsibility:
    Actuator Request Handling
    Request Validation
    Output Command Execution
    Output State Management
    Actuator Fault Detection

Inputs:
    Actuator Request
    Power Mode
    System Time

Outputs:
    Actuator State
    Actuator Event
    Actuator Fault
    Reset Request

Dependencies:
    Output Driver / GPIO Infrastructure
    System Management
    Time Base

Constraints:
    Non-Blocking
    Super Loop Compatible
    No Direct Server Access
    No Direct SMS Access
    No Business Policy
    No Safety Policy
    No Direct MCU Reset
```

---

## ۱۵. مرز با سایر Managementها

| جزء                       | مسئولیت                                |
| ------------------------- | -------------------------------------- |
| Server Management         | دریافت/تفسیر فرمان Server              |
| SMS Management            | دریافت/اعتبارسنجی فرمان SMS            |
| Policy / Command Owner    | تصمیم درباره مجاز بودن عملیات          |
| Actuator Management       | اجرای فرمان خروجی                      |
| Hardware Driver           | دسترسی فیزیکی به GPIO/Output           |
| System Management         | Reset                                  |
| Hardware Input Management | دریافت Feedbackهای ورودی، در صورت وجود |

---

## ۱۶. اصل نهایی

Actuator Management یک **Executor** است، نه Decision Maker.

```text id="c8n4hx"
Decision
   ↓
Request
   ↓
Actuator Management
   ↓
Physical Output
```

بنابراین:

> **تصمیم اینکه چه کاری باید انجام شود خارج از Actuator Management است؛ مسئولیت Actuator Management فقط اجرای صحیح فرمان و ارائه وضعیت/خطای اجرای آن است.**

و اصل مهم دیگر:

> **Actuator Management نباید برای اجرای یک فرمان، سایر Managementها را بشناسد یا مستقیماً آن‌ها را فراخوانی کند.**
