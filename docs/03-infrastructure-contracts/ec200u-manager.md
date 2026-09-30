# پیوست فنی — تعریف و قرارداد EC200U Manager

## 1. هدف

EC200U Manager مرز نرم‌افزاری بین Application و ماژول ارتباطی **EC200U** است.

هدف اصلی آن این است که Managementهای محصول بدون وابستگی به:

* AT Command
* UART
* PDP/Network
* TCP/IP
* GNSS Protocol
* SMS Protocol
* Voice Call Protocol
* Timing و Timeoutهای داخلی ماژول

بتوانند از قابلیت‌های EC200U استفاده کنند.

معماری:

```text
GPS Management ─────┐
Server Management ──┤
SMS Management ─────┤
Call Management ────┤
                    ▼
             EC200U Manager
                    │
                    ▼
              UART / Driver
                    │
                    ▼
                 EC200U
```

---

## 2. جایگاه EC200U Manager

EC200U Manager یک **Infrastructure معمولی مانند Queue یا Storage نیست**.

همچنین یک Management محصولی مانند GPS یا SMS نیز نیست.

جایگاه آن یک **Component/Service مشترک و Hardware-Specific** است که قابلیت‌های ماژول EC200U را به Contractهای مستقل برای Application تبدیل می‌کند.

```text
Application Capability
        │
        ▼
Capability Management
        │
        ▼
EC200U Contract
        │
        ▼
EC200U Manager
        │
        ▼
UART Driver
        │
        ▼
EC200U
```

---

## 3. اصل اساسی

Managementهای Application نباید بدانند که برای انجام یک Capability از چه AT Commandای استفاده می‌شود.

مثلاً GPS Management نباید شامل چیزی شبیه:

```text
AT+QGPS...
AT+QGPSLOC...
```

باشد.

بلکه باید چیزی در سطح Capability ببیند:

```c
GNSS_Start();
GNSS_GetLatestPosition();
GNSS_Stop();
GNSS_GetStatus();
```

جزئیات تبدیل این درخواست‌ها به AT Command کاملاً داخل EC200U Manager قرار دارد.

---

## 4. قابلیت‌های تحت پوشش

EC200U Manager می‌تواند قابلیت‌های زیر را ارائه کند:

### Cellular

* SIM Status
* Network Registration
* Signal Status
* Packet Data
* Network Connectivity

### GNSS

* GNSS Start
* GNSS Stop
* Position Acquisition
* Fix Status
* Satellite/Signal Information در صورت نیاز

### SMS

* SMS Receive
* SMS Send
* SMS Storage/Notification
* Message Status

### Voice Call

* Dial
* Answer
* Hang Up
* Call State

### ارتباطات Data

* PDP Context
* Socket / Data Channel
* Connection Status
* Data Send/Receive

این فهرست قابلیت‌های EC200U است، نه الزاماً API نهایی Application.

---

## 5. مالکیت Capability

یک اصل مهم:

> **EC200U Manager قابلیت فیزیکی/ارتباطی را فراهم می‌کند؛ Management محصولی مالک معنای Capability است.**

مثال:

```text
EC200U Manager
    ↓
"Position = X,Y"
    ↓
GPS Management
    ↓
"Vehicle Position"
```

یا:

```text
EC200U Manager
    ↓
Incoming SMS
    ↓
SMS Management
    ↓
Validate / Interpret Command
    ↓
Action Request
```

بنابراین EC200U Manager نباید بداند SMS دریافتی قرار است مثلاً موتور را قطع کند.

---

## 6. مدل اجرای داخلی

EC200U Manager باید **Stateful و Non-Blocking** باشد.

مثلاً یک عملیات:

```text
Request
  ↓
Send Command
  ↓
Wait Response
  ↓
Parse Response
  ↓
Update State
  ↓
Notify Consumer
```

نباید به شکل زیر پیاده شود:

```c
SendAT();
Wait();
Delay();
ReadResponse();
Wait();
...
```

بلکه State داخلی بین فراخوانی‌های Super Loop حفظ می‌شود.

---

## 7. Super Loop

EC200U Manager نیز در Super Loop سرویس می‌شود:

```c
while (1)
{
    Time_Process();

    HardwareInput_Process();
    Power_Process();
    Diagnostic_Process();

    GPS_Process();
    Server_Process();
    SMS_Process();
    Call_Process();
    FirmwareUpdate_Process();
    Actuator_Process();

    EC200U_Process();

    System_Process();
}
```

ترتیب دقیق بعداً با توجه به Dependency و Priorityهای واقعی تنظیم می‌شود.

---

## 8. رابطه با UART Driver

مسیر ارتباط:

```text
EC200U Manager
       ↓
UART Driver
       ↓
UART Peripheral
       ↓
EC200U
```

UART Driver فقط:

* ارسال Byte
* دریافت Byte
* Buffer
* DMA
* Interrupt
* Hardware Error

را مدیریت می‌کند.

EC200U Manager مسئول:

* AT Command
* Response Parsing
* URC
* Command State
* Timeout
* Retry
* Recovery
* Module State

است.

---

## 9. URC

EC200U می‌تواند بدون اینکه Application درخواست داده باشد، پیام‌هایی ارسال کند.

مانند:

```text
Incoming SMS
Network Registration Change
Incoming Call
GNSS Event
Socket Event
```

این پیام‌ها باید ابتدا توسط EC200U Manager دریافت و تشخیص داده شوند.

```text
EC200U
   ↓
UART RX
   ↓
EC200U Manager
   ↓
Parse URC
   ↓
Capability Event
   ↓
Relevant Management
```

بنابراین Managementها مستقیماً UART را برای پیدا کردن URCها Poll نمی‌کنند.

---

## 10. Stateهای اصلی

EC200U Manager حداقل باید وضعیت کلی ماژول را نگهداری کند:

```text
MODULE_OFF
MODULE_INITIALIZING
MODULE_READY
MODULE_DEGRADED
MODULE_ERROR
```

همچنین وضعیت Capabilityها می‌تواند جداگانه نگهداری شود:

```text
Network State
GNSS State
SMS State
Call State
Data Connection State
```

این Stateها متعلق به EC200U Manager هستند.

State محصولی Capability مانند Vehicle Position یا Low Battery متعلق به Management مربوطه است.

---

## 11. Initialization

راه‌اندازی پیشنهادی:

```text
MCU Startup
    ↓
UART / Driver Init
    ↓
EC200U Manager Init
    ↓
Module Power / Reset
    ↓
AT Interface Initialization
    ↓
SIM Detection
    ↓
Network Initialization
    ↓
Capability Initialization
    ↓
EC200U READY
```

Failure در هر مرحله نباید الزاماً باعث توقف کل Firmware شود.

مثلاً:

```text
GNSS Initialization Failure
        ↓
GPS unavailable
        ↓
Server / Actuator / Diagnostic
continue where possible
```

---

## 12. Timeout و Retry

Timeoutهای AT و ارتباطات داخلی EC200U متعلق به EC200U Manager هستند.

Management بالاتر نباید با جزئیات آنها درگیر شود.

مثلاً:

```text
GPS Management
     ↓
GNSS_Start()
     ↓
EC200U Manager
     ├── AT Command
     ├── Timeout
     ├── Retry
     └── Recovery
     ↓
GNSS Status
```

GPS Management فقط نتیجه Capability را می‌بیند.

---

## 13. Recovery

Recovery در دو سطح تفکیک می‌شود.

### Local Recovery

داخل EC200U Manager:

```text
Command Retry
    ↓
Channel Recovery
    ↓
Reinitialize Capability
```

### Module Recovery

اگر ماژول پاسخ‌گو نباشد:

```text
EC200U Manager
      ↓
Module Unresponsive
      ↓
Local Recovery Failed
      ↓
Reset Request
      ↓
System Management
      ↓
EC200U Module Reset
```

EC200U Manager خودش Reset سخت‌افزاری را مستقیماً انجام نمی‌دهد؛ مگر Driver/Hardware Reset mechanism طبق Contract مشخص شده باشد و مالکیت آن صراحتاً تعریف شود.

---

## 14. Power Mode

Power Management فقط Mode را اعلام می‌کند:

```text
NORMAL
LOW_POWER
```

EC200U Manager سیاست داخلی مصرف توان را اجرا می‌کند.

مثلاً در LOW_POWER:

* کاهش دفعات برخی عملیات
* کاهش Polling
* کاهش استفاده غیرضروری از Data Connection
* کاهش فعالیت GNSS در صورت مجاز بودن
* حفظ قابلیت SMS/ارتباطات ضروری
* حفظ قابلیت‌هایی که Requirement محصول الزام کرده است

اما EC200U Manager خودش تصمیم نمی‌گیرد که سیستم وارد LOW_POWER شود.

---

## 15. API Contract

Contract باید Capability-Oriented باشد.

نمونه:

```c
typedef enum
{
    EC200U_OK,
    EC200U_BUSY,
    EC200U_NOT_READY,
    EC200U_ERROR
} EC200U_Result;
```

### Module

```c
EC200U_Result EC200U_Init(void);

EC200U_Result EC200U_GetStatus(
    EC200U_Status* status
);

void EC200U_Process(void);
```

### GNSS

```c
EC200U_Result EC200U_GNSS_Start(void);

EC200U_Result EC200U_GNSS_Stop(void);

EC200U_Result EC200U_GNSS_GetLatest(
    GNSS_Data* data
);
```

### SMS

```c
EC200U_Result EC200U_SMS_Send(
    const SMS_Data* sms
);

EC200U_Result EC200U_SMS_GetReceived(
    SMS_Data* sms
);
```

### Call

```c
EC200U_Result EC200U_Call_Dial(
    const char* number
);

EC200U_Result EC200U_Call_Hangup(void);

EC200U_Result EC200U_Call_GetState(
    CallState* state
);
```

این APIها نمونه معماری هستند؛ Interface نهایی باید پس از تثبیت نیازمندی هر Capability نهایی شود.

---

## 16. Data Ownership

داده‌های دریافت‌شده باید مالک مشخص داشته باشند.

مثلاً:

```text
EC200U Manager
      ↓
Latest GNSS Data
      ↓
GPS Management
```

یا:

```text
EC200U Manager
      ↓
Incoming SMS
      ↓
SMS Management
```

EC200U Manager نباید داده را به‌صورت نامحدود نگه دارد.

برای Data Streamهای بزرگ، Buffer/Queue با Ownership مشخص استفاده می‌شود.

---

## 17. Event Contract

EC200U Manager می‌تواند Eventهای Capability را تولید کند.

نمونه:

```text
NETWORK_REGISTERED
NETWORK_LOST
SMS_RECEIVED
INCOMING_CALL
GNSS_FIX_ACQUIRED
GNSS_FIX_LOST
MODULE_READY
MODULE_FAILURE
```

اما Policy مربوط به این Eventها متعلق به Management/Policy بالاتر است.

مثلاً:

```text
SMS_RECEIVED
      ↓
SMS Management
      ↓
Parse / Validate
      ↓
Command
```

نه:

```text
EC200U Manager
      ↓
SMS_RECEIVED
      ↓
قطع موتور!
```

---

## 18. Server Data Path

Server Management مسئول منطق ارتباط با Server است.

EC200U Manager فقط Transport را فراهم می‌کند.

```text
Server Management
      ↓
Data Request
      ↓
EC200U Data Interface
      ↓
PDP / Socket
      ↓
EC200U
      ↓
Network
      ↓
Server
```

EC200U Manager نباید بداند:

* این Packet Telemetry است.
* Event است.
* Command است.
* مربوط به GPS است.
* مربوط به Firmware Update است.

این مفاهیم متعلق به Application هستند.

---

## 19. Firmware Update

Firmware Update Management مسئول Firmware Update است.

EC200U Manager فقط Data Transport را فراهم می‌کند.

```text
Firmware Update Management
       ↓
Download Request
       ↓
EC200U Data
       ↓
EC200U Manager
       ↓
Network
       ↓
Server
```

EC200U Manager نباید درباره:

* Version Firmware
* Valid بودن Image
* Bank
* Rollback
* Boot Decision

تصمیم بگیرد.

---

## 20. مستقیم نبودن وابستگی به EC200U

این اصل برای آینده مهم است:

```text
GPS ──┐
SMS ──┤
Call ─┤
Server┤
       ▼
 EC200U Contract
       │
       ▼
EC200U Implementation
```

در نتیجه در آینده امکان جایگزینی:

```text
EC200U
   ↓
Other Modem
```

وجود خواهد داشت، بدون اینکه منطق GPS/SMS/Server/Call از ابتدا نوشته شود.

---

## 21. Fake / Stub

EC200U Manager باید از ابتدا Fake قابل استفاده داشته باشد.

مثلاً:

```text
GPS Management
      ↓
EC200U Contract
      ↓
Fake EC200U
      ↓
Fake Position
Fake Network
Fake SMS
Fake Call
```

در Unit Test می‌توان سناریوهایی مانند موارد زیر را شبیه‌سازی کرد:

```text
GNSS Fix Available
GNSS Fix Lost
Network Lost
Network Restored
SMS Received
Call Incoming
Module Unresponsive
Timeout
Retry
Recovery
```

بدون وجود سخت‌افزار واقعی EC200U.

---

## 22. خطاها

EC200U Manager باید خطاهای مرتبط با خودش را گزارش کند:

```text
UART Failure
AT Timeout
Invalid Response
Unexpected Response
Module Not Ready
SIM Failure
Network Failure
Capability Failure
Module Unresponsive
```

اما Recovery نهایی در سطح سیستم را خودش تصمیم‌گیری نمی‌کند.

در صورت نیاز:

```text
EC200U Manager
      ↓
Fault
      ↓
Local Recovery
      ↓
Reset Request
      ↓
System Management
```

---

## 23. مرز مسئولیت

| موضوع                     | مالک              |
| ------------------------- | ----------------- |
| UART Hardware             | UART Driver       |
| AT Command                | EC200U Manager    |
| AT Response Parsing       | EC200U Manager    |
| URC Detection             | EC200U Manager    |
| GNSS Capability Interface | EC200U Manager    |
| Vehicle Position Meaning  | GPS Management    |
| SMS Transport             | EC200U Manager    |
| SMS Command Meaning       | SMS Management    |
| Voice Call Capability     | EC200U Manager    |
| Call Policy               | Call Management   |
| TCP/Data Transport        | EC200U Manager    |
| Server Protocol/Business  | Server Management |
| Firmware Download Policy  | Firmware Update Management   |
| Firmware Validation/Boot  | Bootloader        |

---

## 24. اصل نهایی

> **EC200U Manager باید تمام پیچیدگی EC200U را در پشت یک Contract پایدار پنهان کند.**

> **Application Managementها باید Capability را ببینند، نه Modem Protocol را.**

> **AT Command، URC، Timeout، Retry، Buffer، Recovery و Module State متعلق به EC200U Manager هستند.**

> **معنای محصولی GPS، SMS، Call و Server متعلق به Management مربوطه است.**

> **EC200U Manager باید از ابتدا Fake/Stub داشته باشد تا توسعه Managementها بدون وابستگی به سخت‌افزار واقعی امکان‌پذیر باشد.**

---

## وضعیت این سند

⚠️ **این سند طبق `docs/04-open-items/OPEN-ITEMS.md` (مورد 🔴 #1) هنوز Contract نهایی و تأییدشده محسوب نمی‌شود (سطح API).** این نسخه، پیش‌نویس معماری سطح Application است که از مصاحبه‌های فنی استخراج شده؛ جزئیات API نهایی باید پس از Bring-up واقعی ماژول تثبیت شود.

✅ **به‌روزرسانی - برون‌سپاری این Component لغو شد (تصمیم صریح کاربر).** آنچه در بخش‌های ۶ تا ۲۲ همین سند توصیف شده (AT Command Engine غیرمسدودکننده، URC Dispatch، Timeout/Retry/Recovery داخلی، مرز محکم با Managementهای بالادست) حالا واقعاً در `firmware/src/infrastructure/ec200u_manager.c` پیاده شده است - هر ۶ Capability بخش ۴ (Cellular، GNSS، SMS، Voice Call، Data/MQTT، و HTTP که بعداً برای FOTA اضافه شد) با AT Command واقعی Quectel EC200U. `docs/05-outsourcing/ec200u-component-outsourcing-spec.md` دیگر SOW فعال نیست (تاریخی نگه داشته شده). جزئیات کامل پیاده‌سازی، محدودیت‌های شناخته‌شده (مثل مشکل Payload باینری MQTT با Quoted-String Parsing) و آنچه هنوز باید با سخت‌افزار واقعی تأیید شود: `docs/04-open-items/OPEN-ITEMS.md` بند 🔴 #۱.
