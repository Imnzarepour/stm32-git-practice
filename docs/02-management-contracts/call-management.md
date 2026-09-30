# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Call Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **Call Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

هدف، مدیریت قابلیت تماس صوتی از طریق EC200U بدون وابستگی سایر Managementها به جزئیات Modem، AT Command و فرآیند برقراری تماس است.

---

# ۲. جایگاه Call Management

Call Management مالک Capability مربوط به تماس صوتی است.

```text
Call Request
     ↓
Call Management
     ↓
EC200U Manager
     ↓
Cellular Network
     ↓
Destination
```

تماس ورودی نیز از مسیر زیر دریافت می‌شود:

```text
Cellular Network
     ↓
EC200U Manager
     ↓
Call Management
     ↓
Call State / Event
```

---

# ۳. مسئولیت‌ها

Call Management مسئول موارد زیر است:

```text
Call Request Management
Outgoing Call
Incoming Call Detection
Call State Management
Call Termination
Call Timeout
Call Retry / Recovery
Call Fault Detection
```

جزئیات Cellular و Modem در اختیار EC200U Manager است.

---

# ۴. درخواست برقراری تماس

سایر Managementها یا Event Policy در صورت نیاز می‌توانند درخواست تماس ایجاد کنند.

```text
Call Request
     ↓
Call Management
     ↓
EC200U Manager
     ↓
Call
```

Call Management مسئول اجرای فرآیند تماس است.

---

# ۵. Call Request Contract

نمونه مفهومی:

```c
CallRequest
{
    Destination
}
```

در صورت نیاز می‌توان اطلاعاتی مانند:

```text
RequestId
CallType
Reason
```

را نیز اضافه کرد.

جزئیات نهایی در Contract پیاده‌سازی تعیین می‌شود.

---

# ۶. Call State

Call Management وضعیت جاری تماس را نگهداری می‌کند.

نمونه وضعیت‌ها:

```text
IDLE
OUTGOING
RINGING
CONNECTED
ENDING
```

این Stateها نمونه هستند و ساختار FSM داخلی Management را الزام نمی‌کنند.

Consumer در صورت نیاز فقط وضعیت قابل مشاهده تماس را دریافت می‌کند.

---

# ۷. عدم Blocking

برقراری یا پایان تماس نباید باعث توقف Super Loop شود.

مدل صحیح:

```text
Process()
   ↓
Start Call
   ↓
Save State
   ↓
Return
```

سپس:

```text
Process()
   ↓
Check Call Status
   ↓
Continue
   ↓
Return
```

انتظار برای:

```text
Ringing
Connection
Remote Answer
Call Termination
```

نباید Blocking باشد.

---

# ۸. تماس ورودی

در صورت دریافت تماس ورودی:

```text
Incoming Call
      ↓
EC200U Manager
      ↓
Call Management
      ↓
Call State
```

در صورت نیاز می‌تواند Event ایجاد شود:

```text
INCOMING_CALL
```

تصمیم درباره اقدام بعدی در Event Policy یا منطق Capability مربوطه انجام می‌شود.

---

# ۹. پایان تماس

Call Management مسئول خاتمه تماس است.

```text
Terminate Request
       ↓
Call Management
       ↓
EC200U Manager
       ↓
Terminate Call
```

همچنین پایان طبیعی تماس باید تشخیص داده شود:

```text
CONNECTED
    ↓
Remote Hangup
    ↓
Call Ended
```

---

# ۱۰. Timeout و Retry

Timeoutهای مربوط به فرآیند تماس در مالکیت Call Management هستند.

مثال:

```text
Call Request
     ↓
No Connection
     ↓
Timeout
     ↓
Retry / Failure
```

Retry در صورت تعریف شدن باید داخل Call Management انجام شود.

Retry نباید باعث Blocking شود.

---

# ۱۱. Fault و Recovery

Call Management مسئول Faultهای مربوط به تماس است.

نمونه:

```text
Call Setup Failure
Call Timeout
Call Termination Failure
EC200U Call Failure
Unexpected Call State
```

ابتدا Recovery محلی انجام می‌شود.

در صورت نیاز:

```text
Call Management
      ↓
Reset Request
      ↓
System Management
```

Call Management مستقیماً Module یا MCU را Reset نمی‌کند.

---

# ۱۲. Call و Event Policy

Call Management نباید خودش Policy عمومی سیستم را اجرا کند.

مثلاً:

```text
Shock Event
    ↓
Call Management
```

به این معنا نیست که Call Management باید خودش تصمیم بگیرد چه زمانی تماس برقرار شود.

مسیر صحیح:

```text
Shock Event
     ↓
Event Policy
     ↓
Call Request
     ↓
Call Management
     ↓
Call
```

بنابراین:

> **Policy تعیین می‌کند چه زمانی تماس لازم است؛ Call Management فقط Capability تماس را اجرا می‌کند.**

---

# ۱۳. Power Mode

Call Management باید با Power Mode سازگار باشد.

در `NORMAL`:

```text
Normal Call Activity
```

در `LOW_POWER`:

```text
Reduced Background Activity
```

اما در صورت وجود یک Call Request معتبر، Low Power نباید مانع اجرای Capability تماس شود مگر اینکه Policy سیستم آن را مجاز نداند.

Power Management فقط Mode را اعلام می‌کند.

---

# ۱۴. Contract سطح معماری

```text
Call Management

Responsibility:
    Outgoing Call
    Incoming Call
    Call State
    Call Termination
    Timeout
    Retry / Recovery
    Call Fault Detection

Inputs:
    Call Request
    Incoming Call Data
    Call Status from EC200U Manager
    System Time
    Power Mode

Outputs:
    Call State
    Call Event
    Call Status
    Call Fault
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
    No Policy Ownership
    No Direct Reset
```

---

# ۱۵. مرز با سایر Managementها

| جزء                       | رابطه با Call Management           |
| ------------------------- | ---------------------------------- |
| EC200U Manager            | ارائه سرویس Voice Call             |
| Event Policy              | تصمیم‌گیری برای ایجاد Call Request |
| Server Management         | می‌تواند منشأ Call Request باشد    |
| SMS Management            | مستقل                              |
| GPS Management            | مستقل                              |
| Hardware Input Management | تولیدکننده احتمالی Event           |
| System Management         | اجرای Reset Request                |

---

# ۱۶. اصل نهایی

Call Management مالک **قابلیت تماس صوتی** است، نه مالک تصمیم‌گیری درباره اینکه چه زمانی باید تماس برقرار شود.

```text
Policy
  ↓
Call Request
  ↓
Call Management
  ↓
EC200U Manager
  ↓
Voice Call
```

اصل معماری:

> **Call Management مسئول اجرای Capability تماس، مدیریت وضعیت، Timeout، Retry و Fault است؛ تصمیم درباره ضرورت تماس در Policy یا Management صاحب تصمیم باقی می‌ماند.**
