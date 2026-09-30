# پیوست فنی — تعریف زیرساخت‌های مشترک Firmware

## ۱. هدف

این پیوست چارچوب زیرساخت‌های مشترک Firmware را تعریف می‌کند.

زیرساخت‌ها برخلاف Managementها مالک یک Capability محصول نیستند؛ بلکه سرویس‌های عمومی و مشترکی را فراهم می‌کنند که Managementها برای اجرای مسئولیت خود از آن‌ها استفاده می‌کنند.

---

## ۲. اصل معماری

```text
┌──────────────────────────────────────┐
│          Management Layer            │
│                                      │
│ System │ Power │ GPS │ Server │ ...  │
└──────────────────┬───────────────────┘
                   │
             Contracts / APIs
                   │
┌──────────────────▼───────────────────┐
│        Infrastructure Layer          │
│                                      │
│ Time │ Queue │ Memory │ Drivers │ ...│
└──────────────────┬───────────────────┘
                   │
┌──────────────────▼───────────────────┐
│          Hardware / MCU              │
└──────────────────────────────────────┘
```

اصل:

> **Management تصمیم می‌گیرد؛ Infrastructure سرویس عمومی ارائه می‌کند.**

---

# ۳. زیرساخت‌های اصلی

در معماری فعلی زیرساخت‌های زیر تعریف می‌شوند:

1. Time Base
2. Queue
3. Memory / Storage
4. Driver Layer
5. Bootloader

و در انتهای طراحی:

6. EC200U Manager

EC200U به دلیل پیچیدگی و وابستگی چند Management، به‌صورت جداگانه طراحی خواهد شد.

**اضافه‌شده بعد از این سند (یادداشت به‌روزرسانی، نه بازنویسی فهرست بالا):**

7. Configuration Infrastructure (`docs/03-infrastructure-contracts/configuration-infrastructure.md`) - Runtime Config مشترک.
8. File Transfer Infrastructure (`docs/03-infrastructure-contracts/file-transfer-infrastructure.md`) - انتقال عمومی فایل بین Server و Storage (Download/Upload/Resume/Chunk)؛ مصرف‌کننده اصلی فعلی `Firmware Update Management` است، اما به هیچ فایل خاصی وابسته نیست.

هر دو همان اصل §۲ را رعایت می‌کنند (Management تصمیم می‌گیرد؛ Infrastructure سرویس عمومی ارائه می‌کند) و لیست ۱۰تایی Managementها را دست‌نخورده می‌گذارند.

---

# ۴. Time Base

Time Base منبع مشترک زمان Firmware است.

مسئولیت‌های آن:

```text
System Tick
Current Time
Elapsed Time
Timestamp
```

نمونه Contract:

```c
uint32_t Time_GetTick(void);
uint32_t Time_GetElapsed(uint32_t start);
uint32_t Time_GetTimestamp(void);
```

Time Base نباید مالک Timing Policy هیچ Management باشد.

مثلاً:

```text
Time Base:
    "الان چند Tick گذشته؟"

GPS Management:
    "هر چند وقت GPS را بررسی کنم؟"
```

بنابراین:

> **Time Base زمان را فراهم می‌کند؛ Management زمان‌بندی رفتار خودش را تعیین می‌کند.**

---

# ۵. Queue Infrastructure

Queue یک زیرساخت ذخیره موقت داده و Request است.

Queue خودش تصمیم‌گیری یا اجرای عملیات ندارد.

Contract پایه:

```c
Push()
Pop()
IsEmpty()
IsFull()
Count()
```

Queueها می‌توانند برای موارد مختلف استفاده شوند:

```text
Event Queue
Telemetry Queue
Request Queue
Communication Queue
```

ساختار Queue باید بتواند Payload با اندازه متغیر را در صورت نیاز مدیریت کند.

---

# ۶. Queue Policy

Policy مربوط به Queue در سطح استفاده‌کننده تعیین می‌شود.

برای Queueهای فعلی:

```text
Queue Full
    ↓
Drop Oldest
    ↓
Insert Newest
```

بنابراین Queue Infrastructure فقط مکانیزم ذخیره و بازیابی را فراهم می‌کند؛ تصمیم درباره اینکه چه چیزی Drop شود متعلق به Contract استفاده‌کننده است.

---

# ۷. Memory / Storage Infrastructure

Memory یک Capability محصول نیست.

این لایه سرویس ذخیره‌سازی را برای Managementها فراهم می‌کند.

نمونه:

```text
EEPROM
External NOR
Flash
Persistent Configuration
Firmware Image Storage
Event Persistence
```

Management نباید مستقیماً Register یا Driver حافظه را کنترل کند.

---

# ۸. Memory Contract

نمونه Contract مفهومی:

```c
Memory_Read(...)
Memory_Write(...)
Memory_Erase(...)
Memory_IsReady(...)
```

برای داده‌های Persistent، مکانیزم‌هایی مانند:

```text
Version
CRC / Integrity
Dual Bank
Recovery
```

در صورت نیاز در لایه Storage یا Component صاحب داده پیاده‌سازی می‌شوند.

---

# ۹. Driver Layer

Driver Layer مرز میان Firmware و Hardware است.

```text
Management
    ↓
Infrastructure Contract
    ↓
Driver
    ↓
MCU Peripheral
```

Management نباید مستقیماً با:

```text
GPIO Registers
ADC Registers
CAN Registers
UART Registers
Timer Registers
```

کار کند.

---

# ۱۰. Driver اصل مهم معماری

Driver نباید منطق Business یا Product را در خود داشته باشد.

مثلاً:

```text
GPIO Driver
    → Read Pin

Hardware Input Management
    → Debounce
    → Qualification
    → State
    → Event
```

یا:

```text
CAN Driver
    → Send / Receive CAN Frame

Diagnostic Management
    → Diagnostic Protocol
```

بنابراین:

> **Driver سخت‌افزار را می‌شناسد؛ Management محصول را می‌شناسد.**

---

# ۱۱. Bootloader

Bootloader از نظر معماری بخشی از Application Managementها نیست.

مسئولیت آن:

```text
Reset
 ↓
Bootloader
 ↓
Firmware Validation
 ↓
Select Valid Image
 ↓
Application
```

Bootloader مسئول:

* تشخیص Firmware معتبر
* اعتبارسنجی Image
* انتخاب Image قابل اجرا
* مدیریت Bankها در صورت Dual-Bank
* ورود به Update/Recovery در صورت نیاز

است.

---

# ۱۲. مرز Bootloader و Firmware Update Management

Firmware Update Management:

```text
Download
Validate
Prepare
Request Installation
```

Bootloader:

```text
Boot
Validate
Select
Execute
```

این دو مسئولیت نباید با هم مخلوط شوند.

---

# ۱۳. Super Loop و Infrastructure

Infrastructureها عموماً خودشان Task مستقل نیستند.

مدل کلی:

```text
main()
  ↓
Initialization
  ↓
while(1)
{
    System_Process();
    Power_Process();
    HardwareInput_Process();
    Diagnostic_Process();
    GPS_Process();
    Server_Process();
    SMS_Process();
    Call_Process();
    FirmwareUpdate_Process();
    Actuator_Process();
}
```

Infrastructureها توسط Managementها یا Super Loop استفاده می‌شوند.

---

# ۱۴. ISR و Infrastructure

ISR فقط باید کار سریع و ضروری را انجام دهد.

مدل:

```text
Hardware Interrupt
       ↓
ISR
       ↓
Register Event / Data
       ↓
Return
       ↓
Super Loop
       ↓
Management Process()
```

پردازش سنگین، Protocol Handling و تصمیم‌گیری نباید در ISR انجام شود.

---

# ۱۵. وابستگی‌ها

وابستگی باید یک‌طرفه باشد:

```text
Management
    ↓
Infrastructure
    ↓
Driver
    ↓
Hardware
```

نه:

```text
Driver
   ↓
Management
```

و Managementها نباید برای اجرای کار یکدیگر را مستقیم صدا بزنند.

---

# ۱۶. Fake / Stub

تمام Infrastructureهای مهم باید Contract مشخص داشته باشند تا در زمان توسعه بتوان نسخه Fake/Stub آن‌ها را استفاده کرد.

مثلاً:

```text
Production:
GPS Management
      ↓
EC200U Manager
      ↓
Real GNSS

Development:
GPS Management
      ↓
EC200U Stub
      ↓
Simulated GNSS
```

این اصل برای:

```text
Memory
Time
Queue
EC200U
Storage
Communication
```

نیز قابل استفاده است.

هدف:

> **Management نباید برای تست و توسعه به سخت‌افزار واقعی وابسته باشد.**

---

# ۱۷. ویژگی مشترک Infrastructure Contract

هر Infrastructure باید تا حد امکان دارای این ویژگی‌ها باشد:

```text
Clear Contract
Non-Blocking
Deterministic Behavior
No Business Logic
No Hidden Global Side Effects
Testable
Replaceable
```

در مواردی که State داخلی لازم است، State باید داخل همان Infrastructure نگهداری شود.

---

# ۱۸. تفاوت Infrastructure و Management

| ویژگی              | Management     | Infrastructure      |
| ------------------ | -------------- | ------------------- |
| مالک Capability    | بله            | خیر                 |
| تصمیم‌گیری Product | بله            | خیر                 |
| FSM محصول          | ممکن است       | معمولاً خیر         |
| دسترسی به Hardware | غیرمستقیم      | بله، از طریق Driver |
| Contract           | بله            | بله                 |
| قابل Fake شدن      | بله            | بله                 |
| Process مستقل      | معمولاً بله    | معمولاً خیر         |
| مثال               | GPS Management | Time Base           |

---

# ۱۹. اصل وابستگی قابل تعویض

معماری باید به شکلی باشد که یک Management بتواند بدون تغییر منطق خود، Infrastructure واقعی یا Fake را دریافت کند.

```text
             ┌── Real Infrastructure
Management ──┤
             └── Fake Infrastructure
```

این موضوع برای توسعه موازی چند تیم اهمیت ویژه دارد.

---

# ۲۰. خروجی نهایی این لایه

در پایان طراحی Infrastructure باید برای هر زیرساخت موارد زیر مشخص باشد:

```text
Responsibility
API / Contract
Input
Output
State Ownership
Timing Behavior
Error Behavior
Concurrency / ISR Rule
Power Behavior
Fake / Stub Interface
Hardware Boundary
```

---

# ۲۱. اصل نهایی

معماری Firmware بر سه سطح استوار است:

```text
Management
    ↓
Infrastructure
    ↓
Hardware / Driver
```

**Management** می‌گوید چه کاری باید انجام شود.

**Infrastructure** سرویس عمومی مورد نیاز را فراهم می‌کند.

**Driver** نحوه صحبت با سخت‌افزار را می‌داند.

و:

> **هیچ Management نباید برای انجام مسئولیت خود مجبور شود جزئیات سخت‌افزار یا Implementation یک Infrastructure دیگر را بداند.**
