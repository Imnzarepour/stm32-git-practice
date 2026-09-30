# پیوست فنی — تعریف و قرارداد Queue Infrastructure

## ۱. هدف

**Queue Infrastructure** زیرساخت عمومی ذخیره و انتقال موقت داده بین بخش‌های مختلف Firmware است.

Queue برای جداکردن **تولیدکننده** از **مصرف‌کننده** استفاده می‌شود؛ به‌خصوص در مسیرهایی که نباید یک Management منتظر Management دیگر بماند.

Queue خود **تصمیم‌گیری، پردازش یا اجرای عملیات** انجام نمی‌دهد.

---

# ۲. جایگاه در معماری

```text id="qarch01"
Producer
   ↓
   Push()
   ↓
┌─────────────┐
│    Queue    │
└─────────────┘
   ↓
   Pop()
   ↓
Consumer
```

نمونه:

```text id="qarch02"
Hardware Input
      ↓
    Event
      ↓
 Event Queue
      ↓
 Policy / Dispatcher
```

یا:

```text id="qarch03"
Provider
   ↓
Telemetry
   ↓
Telemetry Queue
   ↓
Server Management
```

---

# ۳. مسئولیت Queue

Queue فقط مسئول موارد زیر است:

```text id="qresp01"
Store Item
Push Item
Pop Item
Report Status
Report Capacity
```

Queue مسئول موارد زیر نیست:

```text id="qresp02"
Policy
Retry
Priority Decision
Event Processing
Telemetry Processing
Management Execution
Fault Recovery
```

اصل:

> **Queue داده را نگه می‌دارد؛ Management تصمیم می‌گیرد با داده چه کند.**

---

# ۴. انواع استفاده

در معماری فعلی حداقل دو Queue منطقی داریم:

### Event Queue

برای Eventهایی که اهمیت و اولویت پردازشی بالاتری دارند.

```text id="qevent01"
Event
  ↓
Event Queue
  ↓
Dispatcher / Policy
```

### Telemetry Queue

برای داده‌های Telemetry.

```text id="qtele01"
Telemetry
   ↓
Telemetry Queue
   ↓
Server Management
```

این دو Queue از نظر منطقی مستقل هستند.

---

# ۵. اولویت Queueها

در سطح Firmware:

```text id="qprio01"
Event Queue
    ↑
Higher Processing Priority

Telemetry Queue
    ↓
Normal / Lower Processing Priority
```

این اولویت به معنی Priority داخل Queue نیست.

**Telemetry Itemها نسبت به یکدیگر Priority ندارند.**

---

# ۶. ظرفیت Queue

با توجه به متفاوت بودن اندازه Payloadها، ظرفیت Queue بهتر است بر اساس **Byte Capacity** تعریف شود، نه صرفاً تعداد Item.

مثلاً:

```text id="qcap01"
Queue Capacity = 4096 Bytes
```

یک Item کوچک فضای کمتری مصرف می‌کند و یک Item بزرگ فضای بیشتری.

Contract باید امکان تشخیص ظرفیت باقی‌مانده را فراهم کند.

---

# ۷. Full Queue Policy

برای Queueهای فعلی Policy زیر تصویب می‌شود:

```text id="qfull01"
Queue Full
    ↓
Drop Oldest
    ↓
Insert Newest
```

بنابراین در صورت پرشدن Queue:

```text id="qfull02"
[Oldest] [ ... ] [Newest]
    ↓
   Drop
    ↓
[New] [ ... ] [Newest]
```

هدف این است که داده تازه‌تر حفظ شود.

---

# ۸. Empty Queue

در صورت خالی بودن Queue:

```text id="qempty01"
Pop()
   ↓
Empty
   ↓
Return "No Item"
```

Consumer نباید برای رسیدن Item منتظر بماند.

---

# ۹. Non-Blocking

تمام APIهای Queue باید Non-Blocking باشند.

ممنوع:

```c id="qblock01"
while (Queue_IsEmpty())
{
}
```

یا:

```c id="qblock02"
Queue_WaitForItem();
```

مدل صحیح:

```c id="qnonblock01"
if (Queue_Pop(&item))
{
    Process(item);
}

return;
```

اگر داده وجود نداشته باشد، Consumer باید سریع برگردد.

---

# ۱۰. API پایه

Contract پایه:

```c id="qapi01"
Queue_Init(...)
Queue_Push(...)
Queue_Pop(...)
Queue_IsEmpty(...)
Queue_IsFull(...)
Queue_Count(...)
Queue_FreeSpace(...)
```

در صورت نیاز:

```c id="qapi02"
Queue_Clear(...)
```

نیز قابل ارائه است.

---

# ۱۱. Push

```c id="qpush01"
Queue_Push(queue, item);
```

رفتار:

```text id="qpush02"
Queue has space
      ↓
Insert Item
      ↓
Success
```

اگر Queue پر باشد:

```text id="qpush03"
Queue Full
   ↓
Drop Oldest
   ↓
Insert Newest
   ↓
Success
```

در صورت عدم امکان ذخیره، Failure باید به Caller گزارش شود.

---

# ۱۲. Pop

```c id="qpop01"
Queue_Pop(queue, &item);
```

رفتار:

```text id="qpop02"
Item Available
     ↓
Copy / Extract
     ↓
Remove from Queue
     ↓
Success
```

اگر Queue خالی باشد:

```text
Success = false
```

بدون انتظار.

---

# ۱۳. Ownership

پس از `Push`، Queue مالک داده ذخیره‌شده است.

پس از `Pop`، Ownership داده به Consumer منتقل می‌شود.

مدل:

```text id="qown01"
Producer
   ↓
 Push
   ↓
Queue owns item
   ↓
 Pop
   ↓
Consumer owns item
```

این موضوع باید در Implementation Contract دقیقاً مشخص شود، مخصوصاً برای Payloadهای دارای Buffer.

---

# ۱۴. Memory Allocation

Queue Infrastructure نباید در مسیر عادی `Push/Pop` به Dynamic Memory Allocation وابسته باشد، مگر اینکه دلیل معماری مشخصی برای آن وجود داشته باشد.

پیشنهاد پایه:

```text id="qmem01"
Static / Preallocated Buffer
```

مزایا:

* رفتار قابل پیش‌بینی
* عدم Fragmentation
* مناسب برای Bare-Metal
* مناسب برای سیستم طولانی‌مدت
* ساده‌تر برای تست

---

# ۱۵. Payload

Queue باید Payload را به‌صورت مشخص مدیریت کند.

دو مدل قابل استفاده است:

### Fixed Size

```text id="qpayload01"
Queue
├── Item
├── Item
├── Item
└── Item
```

### Variable Size

```text id="qpayload02"
[Length][Payload]
[Length][Payload]
[Length][Payload]
```

با توجه به متفاوت بودن Event و Telemetry، معماری Queue باید امکان مدیریت Payload با اندازه متفاوت را داشته باشد.

Implementation می‌تواند بر اساس Buffer ثابت + Length طراحی شود.

---

# ۱۶. Event Queue

Event Queue داده‌هایی مانند موارد زیر را نگهداری می‌کند:

```text id="qevent02"
ACC_ON
ACC_OFF
DOOR_OPENED
DOOR_CLOSED
SHOCK_DETECTED
LOW_BATTERY_DETECTED
GPS_FIX_ACQUIRED
GPS_FIX_LOST
...
```

Event ذاتاً یک Occurrence است و Queue فقط آن را تا زمان پردازش نگهداری می‌کند.

---

# ۱۷. Telemetry Queue

Telemetry Queue برای داده‌هایی است که باید بعداً توسط Server Management ارسال شوند.

```text id="qtele02"
Provider
   ↓
Telemetry
   ↓
Queue
   ↓
Server Management
```

در صورت قطع ارتباط Server:

```text id="qtele03"
Telemetry
   ↓
Queue
   ↓
Server unavailable
   ↓
Remain queued
```

تا زمانی که ظرفیت Queue اجازه دهد.

---

# ۱۸. Persistence

Queue ذاتاً Persistent نیست.

یعنی:

```text id="qpersist01"
RAM Queue
    ↓
MCU Reset
    ↓
Queue Lost
```

اگر Policy محصول نیاز به حفظ Event یا Telemetry پس از Reset داشته باشد، باید آن داده به **Memory / Storage Infrastructure** منتقل شود.

بنابراین:

> **Queue و Persistence دو مفهوم جدا هستند.**

---

# ۱۹. Queue و Memory

رابطه:

```text id="qmem02"
Producer
   ↓
RAM Queue
   ↓
Consumer
```

و در صورت نیاز:

```text id="qmem03"
Queue
   ↓
Persistence Policy
   ↓
Memory / Storage
```

Queue نباید خودش تصمیم بگیرد که چه زمانی داده را در NOR یا EEPROM ذخیره کند.

---

# ۲۰. Queue و Event Policy

Event Queue فقط Event را نگه می‌دارد.

```text id="qpolicy01"
Event
 ↓
Event Queue
 ↓
Policy / Dispatcher
 ├── Call
 ├── SMS
 ├── Server
 └── Store
```

Queue نباید Policy را اجرا کند.

---

# ۲۱. Queue و Request

Queue می‌تواند Request نیز نگهداری کند.

مثلاً:

```text id="qrequest01"
Server Command
     ↓
Validated Request
     ↓
Request Queue
     ↓
Owner Management
```

اما Queue فقط Request را ذخیره می‌کند.

Owner Management مسئول اجرای آن است.

---

# ۲۲. Processing در Super Loop

Queue با Super Loop سازگار است:

```text id="qsuper01"
Super Loop
    ↓
Management_Process()
    ↓
Queue_Pop()
    ↓
Process One Item
    ↓
Return
```

Management نباید تمام Queue را در یک فراخوانی پردازش کند اگر این کار باعث طولانی شدن `Process()` شود.

ترجیح پایه:

> **هر Process فقط به اندازه لازم Queue را سرویس کند و سریع بازگردد.**

---

# ۲۳. ISR و Queue

در صورت نیاز، ISR می‌تواند یک داده بسیار کوچک و مشخص را در Queue ثبت کند؛ اما نباید عملیات پیچیده انجام دهد.

ترجیح معماری:

```text id="qisr01"
ISR
 ↓
Register Event / Flag
 ↓
Return
 ↓
Super Loop
 ↓
Queue Push / Processing
```

اگر Queue از ISR قابل استفاده باشد، Contract باید صراحتاً مشخص کند که کدام APIها ISR-Safe هستند.

این ویژگی نباید به‌صورت ضمنی فرض شود.

---

# ۲۴. Concurrency

در معماری فعلی سیستم RTOS ندارد.

بنابراین Queue معمولاً بین:

```text id="qconc01"
ISR
Super Loop
```

قرار می‌گیرد.

در صورت استفاده مشترک از ISR و Super Loop، پیاده‌سازی باید Atomicity لازم را تضمین کند.

Managementها نباید خودشان Lock یا Critical Section مستقل برای Queue ایجاد کنند.

---

# ۲۵. Fault

Faultهای Queue می‌توانند شامل موارد زیر باشند:

```text id="qfault01"
Invalid Queue
Invalid Item
Corrupted State
Memory Corruption
Unexpected Capacity Error
```

رفتار Fault باید توسط Queue Contract مشخص شود.

Queue نباید خودش Reset سیستم را اجرا کند.

در صورت کشف خرابی جدی، Fault باید به مسیر Fault سیستم گزارش شود.

---

# ۲۶. Fake / Stub

Queue باید به‌سادگی قابل تست باشد.

در Unit Test می‌توان:

```text id="qtest01"
Push
Push
Push
Pop
Pop
```

را بدون Hardware واقعی اجرا کرد.

همچنین می‌توان سناریوی Full Queue را تست کرد:

```text id="qtest02"
Queue Full
   ↓
Push New
   ↓
Oldest Removed
   ↓
Newest Available
```

این موضوع برای Regression Test بسیار مهم است.

---

# ۲۷. Contract پیشنهادی

```c id="qcontract01"
typedef struct
{
    uint8_t* buffer;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
    uint32_t used;
} Queue;
```

API:

```c id="qcontract02"
bool Queue_Init(
    Queue* queue,
    uint8_t* buffer,
    uint32_t capacity
);

bool Queue_Push(
    Queue* queue,
    const void* data,
    uint32_t length
);

bool Queue_Pop(
    Queue* queue,
    void* data,
    uint32_t buffer_size,
    uint32_t* length
);

bool Queue_IsEmpty(
    const Queue* queue
);

bool Queue_IsFull(
    const Queue* queue
);

uint32_t Queue_Count(
    const Queue* queue
);

uint32_t Queue_FreeSpace(
    const Queue* queue
);
```

این Contract نمونه معماری است و جزئیات Memory Layout می‌تواند در Implementation تعیین شود.

---

# ۲۸. الزامات Contract

Queue API باید:

```text id="qreq01"
Non-Blocking
Deterministic
No Dynamic Allocation in Normal Path
No Business Logic
No Policy
No Management Dependency
Testable
Fakeable
```

باشد.

---

# ۲۹. مرز مسئولیت

| موضوع                   | Queue | Management |
| ----------------------- | ----: | ---------: |
| نگهداری موقت داده       |     ✓ |            |
| Push / Pop              |     ✓ |            |
| تشخیص Full / Empty      |     ✓ |            |
| Drop Oldest Mechanism   |     ✓ |            |
| تصمیم درباره اهمیت داده |       |          ✓ |
| Retry                   |       |          ✓ |
| Event Policy            |       |          ✓ |
| Telemetry Policy        |       |          ✓ |
| Persistence             |       |          ✓ |
| اجرای Request           |       |          ✓ |
| Recovery                |       |          ✓ |
| Reset                   |       |          ✓ |

---

# ۳۰. اصل نهایی

Queue یک **Storage/Transport Primitive** است، نه یک Manager.

```text id="qfinal01"
Producer
    ↓
  Queue
    ↓
Consumer
```

و سه اصل اصلی آن:

> **Queue منتظر نمی‌ماند.**

> **Queue تصمیم نمی‌گیرد.**

> **Queue اجرا نمی‌کند.**

در معماری فعلی، Queue فقط داده یا Request را با رفتار مشخص، Non-Blocking و قابل پیش‌بینی از یک نقطه به نقطه دیگر منتقل می‌کند.
