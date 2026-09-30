# پیوست فنی — تعریف مسئولیت، ورودی و خروجی SMS Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **SMS Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

هدف، مدیریت کامل قابلیت SMS بدون وابستگی سایر Managementها به جزئیات مودم، AT Command، دریافت پیام، Parse و ارسال SMS است.

---

# ۲. جایگاه SMS Management

SMS Management مالک Capability مربوط به SMS است.

```text id="3k1x9a"
SMS
 ↓
EC200U Manager
 ↓
SMS Management
 │
 ├── Incoming SMS
 └── Outgoing SMS
```

ارتباط با EC200U فقط از طریق Contract مربوط به **EC200U Manager** انجام می‌شود.

---

# ۳. مسئولیت‌ها

SMS Management مسئول موارد زیر است:

```text
SMS Reception
SMS Parsing
SMS Command Validation
SMS Sending
SMS Request Management
SMS Timeout
SMS Retry
SMS Fault Detection
```

جزئیات Cellular و Modem خارج از مسئولیت آن است.

---

# ۴. دریافت SMS

مسیر دریافت:

```text id="8o1r8v"
EC200U
   ↓
EC200U Manager
   ↓
Incoming SMS
   ↓
SMS Management
   ↓
Parse
   ↓
Validate
   ↓
Command / Event
```

SMS Management مسئول تشخیص معتبر بودن SMS از دید Capability خود است.

---

# ۵. SMS Command

در صورتی که SMS حاوی Command باشد:

```text id="7t7d2v"
Incoming SMS
      ↓
Parse
      ↓
Validate
      ↓
Command
      ↓
Owner Management
```

SMS Management نباید Action مربوط به Capability دیگر را خودش اجرا کند.

مثلاً برای Command قطع موتور:

```text id="4zq2cx"
SMS
 ↓
SMS Management
 ↓
Validate
 ↓
Actuator Request
 ↓
Actuator Management
```

---

# ۶. اعتبارسنجی Command

قبل از انتشار Command، SMS Management باید کنترل‌های لازم را انجام دهد.

حداقل موارد:

```text id="8x1c0k"
Message Format
Command Type
Required Parameters
Sender / Authorization
Message Integrity
```

شرایط ایمنی و مجاز بودن اجرای Action متعلق به Management مالک Action است.

---

# ۷. ارسال SMS

سایر Managementها در صورت نیاز می‌توانند درخواست ارسال SMS ایجاد کنند.

```text id="8b7u6k"
SMS Request
    ↓
SMS Management
    ↓
EC200U Manager
    ↓
Cellular Network
    ↓
Destination
```

SMS Management مسئول:

* آماده‌سازی پیام
* ارسال
* بررسی وضعیت ارسال
* Timeout
* Retry
* Fault

است.

---

# ۸. عدم انتظار برای نتیجه

ارسال SMS نباید Blocking باشد.

مدل صحیح:

```text id="q9h4z1"
Request
   ↓
SMS Management
   ↓
Start Send
   ↓
Save State
   ↓
Return
```

و در فراخوانی‌های بعدی:

```text id="x8s2v5"
Process()
   ↓
Check Status
   ↓
Continue / Complete / Retry
```

هیچ Management نباید برای پایان ارسال SMS منتظر بماند.

---

# ۹. SMS Request Contract

نمونه مفهومی:

```c id="j1v4c8"
SmsRequest
{
    Destination
    Payload
}
```

در صورت نیاز، اطلاعات زیر نیز می‌تواند به Contract اضافه شود:

```text id="r5n2wd"
RequestId
Encoding
MessageType
```

جزئیات نهایی Contract بر اساس نیاز محصول تعیین می‌شود.

---

# ۱۰. SMS Event

دریافت SMS معتبر می‌تواند Event ایجاد کند.

مثال:

```text id="4n9z3q"
Incoming SMS
     ↓
Validation
     ↓
Valid SMS
     ↓
SMS_RECEIVED Event
```

اما اگر SMS یک Command باشد، مسیر Command جدا است:

```text id="5g8m1a"
Incoming SMS
     ↓
Parse / Validate
     ↓
Command
     ↓
Owner Management
```

Event Policy مسئول تصمیم‌گیری درباره اثر Event است.

---

# ۱۱. SMS و Event Policy

SMS Management نباید خودش تصمیم بگیرد:

```text id="m8y0q3"
SMS Received
   ↓
Call()
SendToServer()
Reset()
```

بلکه:

```text id="w4c2az"
SMS Event
   ↓
Event Queue
   ↓
Event Policy
```

و Command نیز به Owner مربوطه تحویل می‌شود.

---

# ۱۲. Retry

Retry ارسال SMS در مالکیت SMS Management است.

مثال:

```text id="2e9k7v"
Send
 ↓
Failure
 ↓
Retry
 ↓
Success
```

در صورت شکست‌های متوالی:

```text id="s0f3me"
Retry Limit
    ↓
SMS Fault
```

Retry نباید باعث Blocking شود.

---

# ۱۳. Fault و Recovery

SMS Management مسئول Faultهای مربوط به Capability خود است.

نمونه:

```text id="7zq2rm"
SMS Send Failure
SMS Timeout
Invalid SMS
Unsupported Command
EC200U SMS Failure
```

ابتدا Recovery محلی انجام می‌شود.

در صورت نیاز به Reset:

```text id="9u4x1b"
SMS Management
      ↓
Reset Request
      ↓
System Management
```

SMS Management مستقیماً Module یا MCU را Reset نمی‌کند.

---

# ۱۴. SMS و Power Mode

SMS Management باید در هر دو Power Mode رفتار مشخص داشته باشد.

در `NORMAL`:

```text id="k3z1qv"
Normal SMS Activity
```

در `LOW_POWER`:

```text id="x7m5pd"
Reduced SMS Activity
```

اما قابلیت دریافت SMS که طبق نیاز محصول باید در Low Power فعال باقی بماند، نباید صرفاً به دلیل ورود به Low Power متوقف شود.

Power Management فقط Mode را اعلام می‌کند؛ رفتار دقیق SMS در اختیار SMS Management است.

---

# ۱۵. Contract سطح معماری

```text id="d7x2pk"
SMS Management

Responsibility:
    SMS Reception
    SMS Parsing
    SMS Command Validation
    SMS Sending
    Timeout
    Retry / Recovery
    SMS Fault Detection

Inputs:
    Incoming SMS from EC200U Manager
    SMS Request
    System Time
    Power Mode

Outputs:
    SMS Command
    SMS Event
    SMS Send Status
    SMS Fault
    Reset Request (if required)

Dependencies:
    EC200U Manager
    Event Queue
    Time Base

Constraints:
    Non-Blocking
    Super Loop Compatible
    Stateful
    No Direct Modem Access
    No Direct Hardware Access
    No Direct Action Execution
    No Direct Reset
    No Event Policy Execution
```

---

# ۱۶. مرز با سایر Managementها

| جزء                       | رابطه با SMS Management         |
| ------------------------- | ------------------------------- |
| EC200U Manager            | ارائه سرویس SMS                 |
| Event Policy              | تصمیم‌گیری درباره SMS Event     |
| Actuator Management       | اجرای Command مربوط به Actuator |
| Server Management         | مستقل                           |
| Call Management           | مستقل                           |
| GPS Management            | مستقل                           |
| Hardware Input Management | مستقل                           |
| System Management         | اجرای Reset Request             |

---

# ۱۷. اصل نهایی

SMS Management مالک **قابلیت SMS** است:

```text id="1y5qk0"
Incoming SMS
     ↓
SMS Management
     ├── Validate
     ├── Event
     └── Command
```

و:

```text id="2x7fmb"
SMS Request
     ↓
SMS Management
     ↓
EC200U Manager
     ↓
SMS
```

اصل معماری:

> **SMS Management مسئول دریافت، اعتبارسنجی، پردازش و ارسال SMS است؛ اما اجرای Action مربوط به Command و تصمیم‌گیری Policy در مالکیت Management یا Policy مربوطه باقی می‌ماند.**
