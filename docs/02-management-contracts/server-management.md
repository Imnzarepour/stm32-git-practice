# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Server Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **Server Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

هدف، مدیریت ارتباط داده‌ای دستگاه با Server بدون وابستگی سایر Managementها به جزئیات Cellular، TCP/IP، HTTP، MQTT یا پروتکل ارتباطی مورد استفاده است.

---

# ۲. جایگاه Server Management

Server Management مالک ارتباط منطقی دستگاه با Server است.

مسیر کلی:

```text
Managements
    ↓
Server Request / Telemetry
    ↓
Server Management
    ↓
EC200U Manager
    ↓
Cellular Network
    ↓
Server
```

پاسخ Server نیز از همین مرز به Management مربوطه بازمی‌گردد.

---

# ۳. مسئولیت‌ها

Server Management مسئول موارد زیر است:

```text
Server Communication
Request Management
Response Processing
Telemetry Transmission
Connection State
Communication Timeout
Retry / Recovery
Server Communication Fault
```

این Management مالک Cellular یا TCP/IP نیست.

---

# ۴. ارتباط با EC200U Manager

تمام جزئیات ارتباط Cellular در اختیار EC200U Manager است.

Server Management نباید مستقیماً با موارد زیر کار کند:

```text
AT Command
PDP Context
Socket
UART
SIM
Modem Register
```

مرز صحیح:

```text
Server Management
       ↓
EC200U Manager Contract
       ↓
Cellular Data Service
       ↓
Server
```

---

# ۵. Server Request

Managementهای دیگر در صورت نیاز، Request خود را به Server Management تحویل می‌دهند.

نمونه مفهومی:

```c
ServerRequest
{
    RequestType
    Payload
}
```

نمونه Requestها:

```text
Send Telemetry
Send Event
Send Diagnostic Data
Send GPS Data
Send Fault Data
```

فهرست نهایی Requestها در Contract سیستم مشخص می‌شود.

---

# ۶. عدم انتظار برای Response

ارسال Request نباید به شکل زیر انجام شود:

```text
Request
   ↓
Wait for Server
   ↓
Response
   ↓
Return
```

مدل صحیح:

```text
Management
    ↓
Server Request
    ↓
Queue
    ↓
Server Management
    ↓
Process()
    ↓
EC200U
```

Server Management وضعیت عملیات را نگهداری می‌کند و در فراخوانی‌های بعدی `Process()` ادامه می‌دهد.

---

# ۷. Telemetry

Telemetry یکی از ورودی‌های اصلی Server Management است.

مسیر:

```text
Provider / Management
        ↓
Telemetry Item
        ↓
Telemetry Queue
        ↓
Server Management
        ↓
Server
```

Server Management مسئول تصمیم‌گیری درباره تولید مقدار Telemetry نیست.

Provider یا Management مالک داده، مقدار و زمان Update آن است.

---

# ۸. Event

Event نیز می‌تواند طبق Event Policy به Server ارسال شود.

```text
Event
  ↓
Event Policy
  ↓
Server Action
  ↓
Server Management
  ↓
Server
```

Server Management فقط مسئول اجرای ارتباط است.

تشخیص Event و تصمیم درباره اینکه Event باید به Server ارسال شود، خارج از مسئولیت آن است.

---

# ۹. اولویت Event و Telemetry

Event و Telemetry باید در دو مسیر منطقی متفاوت باقی بمانند.

```text
Event
  ↓
Event Queue
  ↓
Server Management
```

و:

```text
Telemetry
  ↓
Telemetry Queue
  ↓
Server Management
```

Eventها نسبت به Telemetry از نظر تحویل اولویت بالاتری دارند.

Server Management نباید اجازه دهد حجم زیاد Telemetry باعث گرسنگی Event شود.

---

# ۱۰. Connection State

Server Management وضعیت ارتباط منطقی با Server را نگهداری می‌کند.

نمونه:

```text
DISCONNECTED
CONNECTING
CONNECTED
```

این State برای تصمیم‌گیری داخلی Management استفاده می‌شود.

Consumerها در صورت نیاز می‌توانند وضعیت ارتباط را از Contract آن دریافت کنند.

---

# ۱۱. Communication Timeout

Server Management مسئول Timeoutهای مربوط به عملیات ارتباط با Server است.

مثال:

```text
Request
   ↓
Waiting
   ↓
Timeout
   ↓
Retry / Fault
```

Timeout نباید باعث Blocking شدن Super Loop شود.

---

# ۱۲. Retry و Recovery

Retry و Recovery ارتباط Server در مالکیت Server Management است.

مثال:

```text
Communication Failure
       ↓
Retry
       ↓
Success
```

در صورت تکرار شکست:

```text
Communication Failure
       ↓
Retry Limit
       ↓
Server Fault
```

اگر مشکل ناشی از EC200U باشد و Recovery محلی کافی نباشد:

```text
Server Management
       ↓
Reset Request
       ↓
System Management
       ↓
EC200U Module Reset
```

Server Management مستقیماً Reset انجام نمی‌دهد.

---

# ۱۳. Offline Behavior

قطع ارتباط Server نباید باعث توقف Super Loop یا سایر Capabilityهای دستگاه شود.

```text
Server Unavailable
       ↓
Server Management
       ↓
Retry / Queue
```

سایر Managementها همچنان باید مستقل به فعالیت خود ادامه دهند.

داده‌هایی که نیاز به نگهداری تا زمان برقراری ارتباط دارند، بر اساس Policy مربوطه در Queue/Memory نگهداری می‌شوند.

---

# ۱۴. Queue و Memory

Server Management مالک Queue و Memory Infrastructure نیست.

از Infrastructure موجود استفاده می‌کند:

```text
Telemetry Queue
Event Queue
Memory
```

در صورت پر شدن Queue، رفتار آن مطابق Contract Queue عمومی سیستم خواهد بود.

Server Management فقط:

```text
Push / Pop
Process
Send
Retry
```

را انجام می‌دهد.

---

# ۱۵. دریافت Command از Server

Server Management می‌تواند Command دریافتی از Server را دریافت و Decode کند.

مسیر:

```text
Server
   ↓
EC200U Manager
   ↓
Server Management
   ↓
Decode / Validate
   ↓
Request
   ↓
Owner Management
```

Server Management نباید خودش Action مربوط به Capability دیگر را اجرا کند.

مثلاً:

```text
Server → Engine Cut-Off Command
```

مسیر صحیح:

```text
Server
   ↓
Server Management
   ↓
Validate Command
   ↓
Actuator Request
   ↓
Actuator Management
```

---

# ۱۶. اعتبارسنجی Command

Server Management باید قبل از انتشار Command حداقل موارد لازم برای معتبر بودن پیام را بررسی کند.

مانند:

```text
Message Integrity
Command Type
Required Fields
Message Validity
```

منطق ایمنی و شرایط اجرای Action متعلق به Management مالک Action است.

---

# ۱۷. Fault

Faultهای مرتبط با ارتباط Server در مالکیت Server Management هستند.

نمونه:

```text
Connection Failure
Send Timeout
Receive Timeout
Invalid Server Response
Communication Protocol Error
EC200U Communication Failure
```

Server Management:

1. Fault را تشخیص می‌دهد.
2. Recovery محلی را انجام می‌دهد.
3. در صورت نیاز Fault قابل مشاهده ایجاد می‌کند.
4. در صورت نیاز Reset Request ایجاد می‌کند.

---

# ۱۸. Power Mode

Server Management باید با Power Mode سازگار باشد.

در `NORMAL`:

```text
Normal Communication
Normal Telemetry
```

در `LOW_POWER`:

```text
Reduced Communication Activity
Reduced Telemetry Activity
```

اما Capabilityهای ضروری که باید در Low Power فعال بمانند، مطابق Policy سیستم ادامه خواهند داشت.

Power Management فقط Mode را اعلام می‌کند؛ تصمیم دقیق درباره کاهش فعالیت ارتباطی در Server Management انجام می‌شود.

---

# ۱۹. Non-Blocking

تمام عملیات Server Management باید Non-Blocking باشند.

به‌خصوص:

```text
Connection
Send
Receive
Timeout
Retry
Reconnect
```

هیچ‌یک نباید Super Loop را متوقف کنند.

مدل اجرایی:

```text
Process()
   ↓
Small Step
   ↓
Save State
   ↓
Return
```

و در فراخوانی بعدی:

```text
Process()
   ↓
Continue
```

---

# ۲۰. Contract سطح معماری

```text
Server Management

Responsibility:
    Server Communication
    Request Management
    Response Processing
    Telemetry Transmission
    Event Transmission
    Server Command Reception
    Connection State
    Timeout
    Retry / Recovery
    Server Communication Fault

Inputs:
    Server Request
    Telemetry Queue
    Event Queue
    Server Data from EC200U Manager
    System Time
    Power Mode

Outputs:
    Server Response
    Capability Request
    Communication State
    Server Fault
    Reset Request (if required)

Dependencies:
    EC200U Manager
    Event Queue
    Telemetry Queue
    Memory Infrastructure
    Time Base

Constraints:
    Non-Blocking
    Super Loop Compatible
    Stateful
    No Direct Cellular Hardware Access
    No Direct Server Protocol Access outside its Contract
    No Direct Action Execution
    No Direct Reset
```

---

# ۲۱. مرز با سایر Managementها

| جزء                       | رابطه با Server Management            |
| ------------------------- | ------------------------------------- |
| EC200U Manager            | ارائه Cellular Data Service           |
| GPS Management            | ارائه Position Telemetry              |
| Diagnostic Management     | ارائه Diagnostic Data                 |
| Hardware Input Management | ارائه Input State/Event               |
| Event Policy              | تعیین اینکه Event به Server ارسال شود |
| Actuator Management       | اجرای Command دریافتی                 |
| SMS Management            | مستقل                                 |
| Call Management           | مستقل                                 |
| System Management         | اجرای Reset Request                   |
| Memory Infrastructure     | نگهداری داده در صورت نیاز             |

---

# ۲۲. اصل نهایی

Server Management مالک **ارتباط با Server** است، نه مالک داده یا Actionهای Capabilityهای دیگر.

مدل نهایی:

```text
Capability Management
        ↓
Request / Telemetry / Event
        ↓
Server Management
        ↓
EC200U Manager
        ↓
Network
        ↓
Server
```

و برای Command:

```text
Server
   ↓
Server Management
   ↓
Validate / Decode
   ↓
Owner Management
   ↓
Action
```

اصل معماری:

> **Server Management مسئول انتقال و دریافت داده و مدیریت ارتباط با Server است؛ تولید داده، تصمیم‌گیری Policy و اجرای Action در مالکیت Management مربوطه باقی می‌ماند.**
