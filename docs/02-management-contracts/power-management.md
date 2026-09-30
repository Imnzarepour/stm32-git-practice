# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Power Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها و خروجی‌های **Power Management** در معماری Firmware دستگاه TCU را مشخص می‌کند.

هدف، تعیین یک سیاست واحد برای وضعیت توان سیستم و فراهم کردن یک Interface مشخص برای سایر Managementها است تا هر Capability بتواند رفتار مصرف توان خود را متناسب با وضعیت سیستم تنظیم کند.

این سند در سطح Architecture و Contract تهیه شده و وارد جزئیات پیاده‌سازی داخلی، کنترل مستقیم سخت‌افزار یا الگوریتم کاهش مصرف هر Capability نمی‌شود.

---

# ۲. جایگاه Power Management

Power Management مالک **Power Mode سیستم** است.

در معماری فعلی دو وضعیت اصلی تعریف می‌شود:

```text
NORMAL
LOW_POWER
```

Power Management وضعیت سیستم را تعیین کرده و آن را در اختیار سایر Managementها قرار می‌دهد.

اصل حاکم:

> **Power Management تعیین‌کننده Mode است؛ هر Management مسئول تعیین رفتار خود در آن Mode است.**

بنابراین Power Management نباید وارد منطق داخلی GPS، Server، SMS، EC200U یا سایر Managementها شود.

---

# ۳. مسئولیت‌ها

## ۳-۱. تعیین Power Mode

Power Management بر اساس وضعیت ACC، Power Mode سیستم را تعیین می‌کند.

قاعده اصلی:

```text
ACC ON
   ↓
NORMAL

ACC OFF
   ↓
LOW_POWER
```

این دو وضعیت تنها Modeهای اصلی سیستم در معماری فعلی هستند.

---

## ۳-۲. اعلام Power Mode

Power Management باید Power Mode فعلی را برای Managementهای دیگر قابل دسترسی قرار دهد.

نمونه:

```text
Power Management
       │
       │ Power Mode
       ├──────────────► GPS
       ├──────────────► Server
       ├──────────────► SMS
       ├──────────────► Call
       ├──────────────► Firmware Update
       └──────────────► سایر Managementها
```

Managementهای دیگر با مشاهده Mode، رفتار داخلی خود را تغییر می‌دهند.

---

## ۳-۳. مدیریت رفتار کاهش توان در سطح سیستم

Power Management صرفاً **سیاست سطح سیستم** را مشخص می‌کند.

در `LOW_POWER`، هدف کاهش مصرف توان است؛ اما Capabilityها الزاماً خاموش نمی‌شوند.

برای مثال:

```text
NORMAL
    ↓
LOW_POWER

GPS      → کاهش فعالیت
Server   → کاهش فعالیت
EC200U   → کاهش مصرف
Inputs   → کاهش نرخ/شدت پردازش در صورت امکان
```

جزئیات این کاهش فعالیت متعلق به Management مربوطه است.

---

# ۴. محدودیت‌های مسئولیت

Power Management مسئول موارد زیر نیست:

* خاموش/روشن کردن مستقیم EC200U
* تعیین نحوه Sleep کردن GPS
* تعیین زمان‌بندی ارسال Server
* تعیین رفتار SMS
* تعیین الگوریتم کاهش مصرف هر Capability
* مدیریت داخلی Battery
* اجرای Recovery سایر Managementها

Power Management فقط:

```text
Determine Mode
      ↓
Publish Mode
```

را انجام می‌دهد.

---

# ۵. ورودی‌ها

## ۵-۱. ACC Status

وضعیت ACC ورودی اصلی Power Management است.

ACC توسط **Hardware Input Management** تشخیص داده می‌شود.

مسیر:

```text
ACC Hardware
     ↓
Hardware Input Management
     ↓
ACC Status
     ↓
Power Management
```

Power Management مستقیماً مسئول خواندن GPIO یا Register سخت‌افزاری ACC نیست.

---

## ۵-۲. System Time

Power Management در صورت نیاز می‌تواند از Time Base مشترک سیستم برای مدیریت تغییرات زمانی مرتبط با Policy توان استفاده کند.

Time Base یک Infrastructure مشترک است و Management مستقلی محسوب نمی‌شود.

---

# ۶. خروجی‌ها

## ۶-۱. Power Mode

خروجی اصلی Power Management، وضعیت فعلی Power Mode است:

```text
NORMAL
LOW_POWER
```

این وضعیت باید برای مصرف سایر Managementها قابل دسترسی باشد.

---

## ۶-۲. Power Mode Change

تغییر Mode می‌تواند به‌عنوان یک تغییر وضعیت قابل مشاهده باشد.

مثال:

```text
ACC ON
   ↓
Power Mode = NORMAL

ACC OFF
   ↓
Power Mode = LOW_POWER
```

Managementهای دیگر در اجرای بعدی خود Mode جدید را مشاهده کرده و Policy داخلی خود را اعمال می‌کنند.

---

# ۷. رفتار در LOW_POWER

`LOW_POWER` به معنی خاموش شدن Capabilityها نیست.

اصل معماری:

> **در LOW_POWER، شدت و تناوب فعالیت کاهش می‌یابد؛ Capability تا حد امکان فعال باقی می‌ماند.**

بنابراین هر Management باید Contract داخلی خود را به‌گونه‌ای طراحی کند که برای هر دو Mode رفتار مشخص داشته باشد.

مثلاً:

| Management | NORMAL      | LOW_POWER                        |
| ---------- | ----------- | -------------------------------- |
| GPS        | فعالیت عادی | فعالیت کاهش‌یافته                |
| Server     | فعالیت عادی | فعالیت کاهش‌یافته                |
| SMS        | فعالیت عادی | فعال با مصرف کاهش‌یافته          |
| Inputs     | نرخ عادی    | نرخ/شدت کاهش‌یافته در صورت امکان |
| EC200U     | فعالیت عادی | Power behavior کاهش‌یافته        |
| Call       | فعالیت عادی | مطابق Capability و نیاز          |

این جدول صرفاً بیانگر اصل معماری است و جزئیات هر Management در Contract همان Management تعریف خواهد شد.

---

# ۸. تغییر Power Mode

Power Mode فقط توسط Power Management تعیین می‌شود.

سایر Eventها مستقیماً Power Mode را تغییر نمی‌دهند.

برای مثال:

```text
SMS Received
     ↓
SMS Management
     ↓
Normal SMS Processing

```

این Event به‌تنهایی باعث تغییر:

```text
LOW_POWER → NORMAL
```

نمی‌شود.

همچنین:

```text
Shock
Server Request
GPS Fix
Incoming Call
```

به‌صورت مستقیم باعث تغییر Power Mode نمی‌شوند.

`ACC ON` عامل قطعی ورود به `NORMAL` است.

---

# ۹. مدل ارتباطی

```text
              ┌─────────────────────┐
              │ Hardware Input      │
              │ Management          │
              └──────────┬──────────┘
                         │
                      ACC Status
                         │
                         ▼
              ┌─────────────────────┐
              │ Power Management    │
              └──────────┬──────────┘
                         │
                    Power Mode
                         │
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
         GPS           Server          SMS
          │              │              │
       Policy          Policy          Policy
          │              │              │
          ▼              ▼              ▼
     Reduced/Normal  Reduced/Normal  Reduced/Normal
```

Power Management در این مسیر **فرمان اجرایی به Managementها ارسال نمی‌کند**؛ بلکه Mode را در اختیار آن‌ها قرار می‌دهد.

---

# ۱۰. Contract سطح معماری

```text
Power Management

Responsibility:
    Determine Power Mode
    Publish Power Mode

Inputs:
    ACC Status
    System Time

Outputs:
    Power Mode
    Power Mode Change

Power Modes:
    NORMAL
    LOW_POWER

Rules:
    ACC ON  → NORMAL
    ACC OFF → LOW_POWER

Constraints:
    Non-Blocking
    Super Loop Compatible
    No direct hardware ownership of ACC
    No direct control of other Managements
    No capability-specific power algorithm
```

---

# ۱۱. مرز با سایر اجزا

| جزء                       | رابطه با Power Management               |
| ------------------------- | --------------------------------------- |
| Hardware Input Management | تأمین ACC Status                        |
| GPS Management            | دریافت Power Mode و تعیین رفتار خود     |
| Server Management         | دریافت Power Mode و تعیین رفتار خود     |
| SMS Management            | دریافت Power Mode و تعیین رفتار خود     |
| EC200U Manager            | اجرای Power Behavior داخلی خود          |
| System Management         | مستقل؛ Power Mode را مالک نیست          |
| Time Base                 | تأمین زمان مشترک                        |
| Drivers                   | خارج از مسئولیت مستقیم Power Management |

---

# ۱۲. اصل نهایی

Power Management یک **Policy Manager برای وضعیت توان سیستم** است، نه مدیر خاموش/روشن کردن اجزای Firmware.

اصل نهایی:

> **Power Management فقط تعیین می‌کند سیستم در چه Power Modeای قرار دارد؛ هر Management مسئول است رفتار و مصرف توان خود را متناسب با آن Mode تنظیم کند.**

در معماری فعلی:

```text
ACC ON  → NORMAL
ACC OFF → LOW_POWER
```

و:

> **LOW_POWER به معنی حذف Capability نیست؛ به معنی کاهش شدت و مصرف فعالیت Capabilityها است.**
