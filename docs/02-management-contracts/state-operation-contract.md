# پیوست فنی — قرارداد حالت و عملیات Management

## 1. هدف

این قرارداد یک الگوی مشترک برای تعریف **State** و **Operation** در Managementهای Firmware ارائه می‌کند.

هدف این است که:

* وضعیت جاری هر Management مشخص و قابل مشاهده باشد.
* عملیات طولانی یا چندمرحله‌ای به‌صورت Non-Blocking اجرا شوند.
* State داخلی بین فراخوانی‌های `Process()` حفظ شود.
* هر Request بتواند وضعیت اجرای مشخصی داشته باشد.
* منطق داخلی FSM به Contract عمومی Management تحمیل نشود.

**این سند به همه Managementها اعمال می‌شود** — هر Management در صورت داشتن Operation باید Contract خودش را بر پایه همین الگو تعریف کند (نه اینکه الگوی جدیدی اختراع کند).

---

## 2. تفکیک State و Operation

### State

State بیان‌کننده **وضعیت فعلی Capability یا Management** است.

مثال:

```text
IDLE
INITIALIZING
READY
BUSY
ERROR
```

State یک وضعیت پایدار است و می‌تواند توسط Consumer خوانده شود.

---

### Operation

Operation بیان‌کننده **کاری است که Management باید انجام دهد یا در حال انجام آن است**.

مثال:

```text
SEND_SMS
DIAL_CALL
READ_DTC
DOWNLOAD_FIRMWARE
LOCK_DOOR
```

Operation می‌تواند چندین مرحله داشته باشد و بین چند فراخوانی `Process()` ادامه پیدا کند.

---

## 3. مدل کلی

```text
             Request
                │
                ▼
        ┌────────────────┐
        │   Operation    │
        └───────┬────────┘
                │
                ▼
        Operation State
                │
       ┌────────┼────────┐
       ▼        ▼        ▼
    Running   Success   Failed
       │
       ▼
   Management
      State
```

نکته مهم:

> **Operation State با Management State یکی نیست.**

ممکن است Management در وضعیت `READY` باشد ولی یک Operation در وضعیت `RUNNING` قرار داشته باشد.

---

## 4. قرارداد Management State

هر Management در صورت نیاز می‌تواند یک State عمومی ارائه کند:

```c
typedef enum
{
    MGMT_STATE_UNINITIALIZED,
    MGMT_STATE_INITIALIZING,
    MGMT_STATE_READY,
    MGMT_STATE_BUSY,
    MGMT_STATE_DEGRADED,
    MGMT_STATE_ERROR
} ManagementState;
```

تمام Managementها الزاماً به تمام این Stateها نیاز ندارند.

Stateهای اختصاصی Capability می‌توانند در Contract همان Management تعریف شوند.

مثلاً:

```text
GPS:
    NO_FIX
    FIX_ACQUIRING
    FIX_VALID

Call:
    IDLE
    DIALING
    RINGING
    ACTIVE
```

---

## 5. قرارداد Operation

هر Operation دارای شناسه و وضعیت اجرای مستقل است:

```c
typedef enum
{
    OPERATION_IDLE,
    OPERATION_PENDING,
    OPERATION_RUNNING,
    OPERATION_SUCCESS,
    OPERATION_FAILED,
    OPERATION_CANCELLED
} OperationState;
```

ساختار پایه:

```c
typedef struct
{
    uint32_t        operation_id;
    OperationState  state;
    uint32_t        timestamp;
    uint32_t        error_code;
} OperationStatus;
```

`error_code` فقط در صورت نیاز Capability استفاده می‌شود.

---

## 6. Lifecycle عملیات

چرخه استاندارد:

```text
IDLE
 │
 │ Request
 ▼
PENDING
 │
 │ Accepted by Management
 ▼
RUNNING
 │
 ├───────────────┐
 │               │
 ▼               ▼
SUCCESS        FAILED
 │
 └──────────────┘
```

در صورت پشتیبانی از Cancellation:

```text
PENDING / RUNNING
       │
       ▼
   CANCELLED
```

---

## 7. Request

Request فقط عملیات موردنظر را توصیف می‌کند.

نمونه:

```c
typedef struct
{
    OperationId operation;
    uint32_t    request_id;
    void*       parameters;
} OperationRequest;
```

`request_id` برای Traceability است و در صورت عدم نیاز می‌تواند حذف شود.

---

## 8. اصل مهم Request

پذیرفته شدن Request به معنی موفق شدن Operation نیست.

مثلاً:

```text
SMS_Send()
    ↓
Request Accepted
    ↓
PENDING
    ↓
RUNNING
    ↓
Network / Module Processing
    ↓
SUCCESS
```

بنابراین نباید از Return Value تابع Request برای اعلام نتیجه نهایی عملیات استفاده شود.

Return Value صرفاً می‌تواند وضعیت پذیرش اولیه Request را مشخص کند.

---

## 9. API پیشنهادی

```c
OperationResult Management_Request(
    const OperationRequest* request
);

OperationResult Management_GetOperationStatus(
    uint32_t request_id,
    OperationStatus* status
);

ManagementState Management_GetState(void);

void Management_Process(void);
```

نمونه `OperationResult`:

```c
typedef enum
{
    OP_ACCEPTED,
    OP_REJECTED,
    OP_BUSY,
    OP_INVALID
} OperationResult;
```

---

## 10. Process و Operation

`Process()` مسئول ادامه دادن Operationهای فعال است.

مثال:

```text
Management_Process()
       │
       ├── Check Request
       │
       ├── Advance Operation
       │
       ├── Check Timeout
       │
       ├── Update State
       │
       └── Return
```

هر فراخوانی فقط یک Step کوچک از Operation را اجرا می‌کند.

---

## 11. مثال — ارسال SMS

```text
SMS Management State
        │
      READY
        │
        ▼
   Send SMS Request
        │
        ▼
    Operation=PENDING
        │
        ▼
    Operation=RUNNING
        │
        ├── Prepare
        ├── Send to EC200U
        ├── Wait for response
        └── Validate result
                │
          ┌─────┴─────┐
          ▼           ▼
       SUCCESS      FAILED
```

هیچ مرحله‌ای نباید باعث Blocking Super Loop شود.

---

## 12. State قابل مشاهده و State داخلی

تفکیک زیر الزامی است:

```text
Public State
    ↓
Contract / Consumer
```

در مقابل:

```text
Internal FSM State
    ↓
Implementation Detail
```

مثلاً:

```text
Public:
    CALL_ACTIVE

Internal:
    WAIT_AT_RESPONSE
    WAIT_CONNECT
    PARSE_RESPONSE
    CHECK_TIMEOUT
```

Consumer نباید به Stateهای داخلی وابسته شود.

---

## 13. Timeout

Timeout متعلق به Operation است، نه State عمومی Management.

مثلاً:

```text
Operation RUNNING
       │
       ▼
  Timeout Check
       │
   ┌───┴───┐
   ▼       ▼
Normal   Timeout
           │
           ▼
        Recovery
           │
      ┌────┴────┐
      ▼         ▼
   Retry      Failed
```

اندازه و سیاست Timeout در Contract عمومی تعیین نمی‌شود؛ هر Management بر اساس Capability خود تعیین می‌کند.

---

## 14. Retry

Retry بخشی از Operation Policy است.

```text
RUNNING
   ↓
Failure
   ↓
Retry Allowed?
 ┌─┴──────┐
Yes      No
 │        │
 ▼        ▼
Retry    FAILED
```

تعداد Retry و شرایط آن نباید در قرارداد عمومی ثابت شود.

---

## 15. همزمانی عملیات

هر Management باید مشخص کند آیا:

* فقط یک Operation همزمان دارد.
* چند Operation مستقل دارد.
* Operation جدید در حالت Busy رد می‌شود.
* Operation جدید Queue می‌شود.

برای بسیاری از Capabilityهای این پروژه، مدل ساده زیر ترجیح داده می‌شود:

```text
One active operation
        │
        ▼
New request while BUSY
        │
        ▼
REJECTED / QUEUED
```

انتخاب نهایی در Contract همان Management انجام می‌شود.

---

## 16. Operation Completion

پس از پایان Operation، Management باید نتیجه را در Contract قابل مشاهده کند.

مثلاً:

```text
Operation Status
    │
    ├── SUCCESS
    └── FAILED + Error Code
```

در صورت نیاز، Completion می‌تواند Event نیز تولید کند.

اما:

> **Operation Result و Event دو مفهوم متفاوت هستند.**

---

## 17. رابطه با Event

مثال:

```text
Operation
   ↓
SMS_SEND
   ↓
SUCCESS
   ↓
SMS_SENT Event
```

Event بیان‌کننده وقوع یک اتفاق است.

Operation Status بیان‌کننده وضعیت اجرای یک درخواست است.

---

## 18. رابطه با Fault

Failure یک Operation الزاماً System Fault نیست.

```text
Operation Failed
      │
      ├── Local Retry
      │
      ├── Local Recovery
      │
      └── Fault
             │
             └── اگر لازم بود
                    ↓
                Reset Request
```

بنابراین هر `FAILED` نباید به Reset یا Server Notification تبدیل شود.

---

## 19. Power Mode

Operation باید در صورت امکان با Power Mode سازگار باشد.

در `LOW_POWER`:

* Operation جدید ممکن است با سیاست متفاوت پذیرفته شود.
* Operation جاری در صورت امکان ادامه پیدا کند.
* Polling/Retry می‌تواند کاهش یابد.
* Capabilityهای ضروری نباید بدون Policy متوقف شوند.

Power Management فقط Mode را اعلام می‌کند؛ Management مالک رفتار Operation خود است.

---

## 20. Reset در حین Operation

در صورت Reset:

```text
Operation RUNNING
       ↓
System / Module Reset
       ↓
Operation Context Lost
       ↓
Restart
       ↓
Reinitialize
```

به‌صورت پیش‌فرض، Operation پس از Reset **Resume نمی‌شود** مگر اینکه Contract همان Capability صراحتاً Persistence/Resume را تعریف کرده باشد.

این اصل با تصمیم معماری فعلی پروژه سازگار است که Startup پس از Reset از ابتدا انجام شود.

---

## 21. Contract حداقلی پیشنهادی

هر Management در صورت داشتن Operation می‌تواند Contract خود را بر پایه این الگو تعریف کند:

```c
typedef struct
{
    OperationId   id;
    OperationState state;
    uint32_t      request_id;
    uint32_t      timestamp;
    uint32_t      error_code;
} OperationStatus;

OperationResult Xxx_Request(
    const XxxRequest* request
);

OperationResult Xxx_GetOperationStatus(
    uint32_t request_id,
    OperationStatus* status
);

XxxState Xxx_GetState(void);

void Xxx_Process(void);
```

---

## 22. اصول نهایی قرارداد

1. **State = وضعیت فعلی Capability/Management**
2. **Operation = کاری که باید انجام شود یا در حال انجام است**
3. **Operation State با Management State یکی نیست.**
4. `Process()` عملیات را Step-by-Step پیش می‌برد.
5. هیچ Operation نباید Super Loop را Block کند.
6. Request Accepted به معنی Operation Success نیست.
7. Timeout و Retry متعلق به Operation Policy همان Management است.
8. Stateهای داخلی FSM جزئی از Contract نیستند.
9. Failure یک Operation الزاماً System Fault نیست.
10. Reset در حین Operation به‌صورت پیش‌فرض Context عملیات را از بین می‌برد.
11. Event و Operation Result دو مفهوم مستقل هستند.
12. هر Management فقط Capabilityهایی را که واقعاً نیاز دارد در Contract خود exposed می‌کند.

### اصل نهایی

> **State می‌گوید «الان در چه وضعیتی هستیم».**

> **Operation می‌گوید «چه کاری در حال انجام است یا باید انجام شود».**

> **Process فقط Operation را در هر فراخوانی یک گام جلو می‌برد و هیچ‌گاه منتظر پایان آن نمی‌ماند.**
