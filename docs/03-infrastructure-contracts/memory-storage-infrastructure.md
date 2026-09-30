# پیوست فنی — تعریف و قرارداد Memory / Storage Infrastructure

## ۱. هدف

**Memory / Storage Infrastructure** زیرساخت مشترک ذخیره‌سازی داده در Firmware است.

این لایه امکان ذخیره و بازیابی داده‌های:

* Configuration
* Persistent State
* Event / Telemetry در صورت نیاز Policy
* Firmware Image
* اطلاعات Recovery

را فراهم می‌کند.

Memory / Storage Infrastructure مالک منطق محصول نیست و نباید درباره اینکه **چه چیزی، چه زمانی و چرا** ذخیره شود تصمیم بگیرد.

---

# ۲. جایگاه در معماری

```text
Management
    ↓
Storage Contract
    ↓
Memory / Storage Infrastructure
    ↓
Driver
    ↓
EEPROM / NOR / Flash
```

مثال:

```text
Firmware Update Management
      ↓
Firmware Storage
      ↓
External NOR
```

یا:

```text
System Configuration
      ↓
Persistent Storage
      ↓
EEPROM
```

---

# ۳. مسئولیت‌های اصلی

این زیرساخت مسئول موارد زیر است:

```text
Read
Write
Erase
Storage Status
Integrity Check
Persistent Data Access
Storage Partitioning
```

در صورت نیاز:

```text
Version Management
Atomic Update
Dual Bank
Recovery
```

نیز می‌تواند در Contractهای تخصصی Storage ارائه شود.

---

# ۴. انواع Memory

در معماری فعلی حداقل این منابع قابل تصور هستند:

```text
MCU Internal Flash
EEPROM
External NOR
RAM
```

اما همه این منابع الزاماً از یک API واحد استفاده نمی‌کنند.

اصل مهم:

> **نوع فیزیکی Memory نباید به Management نشت کند، مگر در مواردی که ماهیت Capability واقعاً به آن وابسته باشد.**

---

# ۵. تفکیک RAM و Persistent Storage

RAM:

```text
Temporary
Fast
Lost after Reset
```

Persistent Storage:

```text
Survives Reset
Survives Power Loss
Requires Integrity Protection
```

Queue معمولاً از RAM استفاده می‌کند.

داده‌ای که باید پس از Reset باقی بماند باید از Storage Infrastructure استفاده کند.

---

# ۶. Configuration

Configurationهای Firmware می‌توانند در EEPROM یا Storage مناسب قرار گیرند.

نمونه:

```text
Configuration
├── Device Configuration
├── Communication Configuration
├── Product Configuration
└── Calibration Data
```

Management مالک **معنای Configuration** است.

Storage فقط آن را ذخیره و بازیابی می‌کند.

---

# ۷. Persistent Data

اگر داده‌ای باید بعد از Reset باقی بماند:

```text
Management
    ↓
Persistent Data
    ↓
Storage
```

مثلاً:

```text
Firmware Update State
Device Configuration
Important Event
Recovery Information
```

اما Storage نباید خودش تصمیم بگیرد چه چیزی Persistent باشد.

---

# ۸. Event Persistence

Event Queue ذاتاً موقتی است.

اگر Policy تصمیم بگیرد Event مهم باید بعد از Reset نیز باقی بماند:

```text
Event
 ↓
Policy
 ↓
Persistence Request
 ↓
Storage
```

بنابراین:

> **Persistence یک Policy است؛ Storage فقط سرویس آن Policy را اجرا می‌کند.**

---

# ۹. Firmware Storage

Firmware Update برای ذخیره Firmware از Storage Infrastructure استفاده می‌کند:

```text
Server
   ↓
Firmware Update Management
   ↓
Storage
   ↓
External NOR
```

Firmware Update نباید مستقیماً با NOR Driver کار کند.

---

# ۱۰. Non-Blocking

Storage عملیات مهمی مانند Write و Erase دارد که ممکن است زمان‌بر باشند.

بنابراین API Storage نباید باعث Blocking شدن Super Loop شود.

مدل پیشنهادی:

```text
Storage_Write()
      ↓
Start Operation
      ↓
Return
      ↓
Super Loop
      ↓
Storage_Process()
      ↓
Check Operation
      ↓
Complete
```

بنابراین برای Storageهای واقعی، وجود یک `Process()` داخلی یا مکانیزم State-based مشابه می‌تواند لازم باشد.

---

# ۱۱. Storage Process

اگر عملیات Storage زمان‌بر باشد:

```c
void Storage_Process(void);
```

می‌تواند در Super Loop سرویس شود.

مدل:

```text
Management
   ↓
Storage_Request
   ↓
Storage
   ↓
Start
   ↓
Return

Super Loop
   ↓
Storage_Process()
   ↓
Continue / Complete
```

این `Process()` با `Management Process()` متفاوت است؛ Storage یک Infrastructure Stateful است که در صورت نیاز برای تکمیل عملیات سخت‌افزاری سرویس می‌شود.

---

# ۱۲. Read

Readهای کوچک و سریع می‌توانند Synchronous باشند، مشروط به اینکه Hardware واقعاً سریع و Non-Blocking باشد.

مثلاً:

```c
Storage_Read(address, buffer, length);
```

اما Readهای بزرگ یا عملیات‌هایی که می‌توانند Super Loop را متوقف کنند باید به مدل Asynchronous / Stateful منتقل شوند.

اصل:

> **Blocking بودن بر اساس رفتار واقعی Storage تعیین می‌شود، نه صرفاً نام Read یا Write.**

---

# ۱۳. Write

Write باید وضعیت عملیات را مشخص کند.

مثلاً:

```text
WRITE_STARTED
WRITE_BUSY
WRITE_COMPLETED
WRITE_FAILED
```

Management نباید در انتظار Completion حلقه بزند.

---

# ۱۴. Erase

Erase معمولاً می‌تواند زمان‌بر باشد.

بنابراین:

```text
Erase Request
    ↓
Start
    ↓
Return
    ↓
Storage_Process()
    ↓
Complete
```

نه:

```c
while (!erase_done)
{
}
```

---

# ۱۵. Integrity

داده Persistent نباید بدون کنترل Integrity مصرف شود.

حداقل در موارد لازم:

```text
Data
 ↓
CRC / Integrity
 ↓
Valid / Invalid
```

مثلاً:

```c
typedef struct
{
    uint32_t version;
    uint32_t length;
    uint32_t crc;
    uint8_t  data[];
} PersistentRecord;
```

ساختار دقیق بر اساس نوع داده تعیین می‌شود.

---

# ۱۶. Atomicity

برای Configuration و داده‌های حساس، Write نباید باعث شود پس از قطع برق یک داده نصفه و نامعتبر به‌عنوان داده معتبر شناخته شود.

مدل پیشنهادی:

```text
Old Valid Data
      ↓
Write New Data
      ↓
Verify
      ↓
Mark New Valid
      ↓
Invalidate Old
```

در صورت Power Loss:

```text
Boot
 ↓
Find Valid Record
 ↓
Use Last Valid Data
```

---

# ۱۷. Dual Bank

برای Firmware، Dual-Bank ترجیح معماری است.

```text
Bank A → Current Valid Firmware
Bank B → New Firmware
```

فرآیند:

```text
Download
   ↓
Bank B
   ↓
Validate
   ↓
Mark Valid
   ↓
Reset
   ↓
Bootloader
   ↓
Boot Bank B
```

اگر Bank B نامعتبر باشد:

```text
Bootloader
   ↓
Bank A
   ↓
Valid Firmware
```

جزئیات انتخاب Bank متعلق به Bootloader است، نه Storage عمومی.

---

# ۱۸. Version

برای داده‌های Persistent در صورت نیاز Version باید ذخیره شود.

```text
Record
├── Version
├── Length
├── Data
└── Integrity
```

در صورت تغییر ساختار داده:

```text
Old Version
    ↓
Migration / Reject
```

تصمیم درباره Migration متعلق به صاحب داده است، نه Storage عمومی.

---

# ۱۹. Ownership

Management مالک داده است.

Storage مالک محل فیزیکی ذخیره‌سازی است.

```text
Firmware Update Management
     │
     │ owns Firmware semantics
     ↓
Storage
     │
     │ owns storage mechanism
     ↓
NOR Driver
```

این تفکیک برای جلوگیری از وابستگی شدید اهمیت دارد.

---

# ۲۰. Memory Allocation

Storage نباید برای عملیات عادی به Dynamic Allocation وابسته باشد.

ترجیح:

```text
Static Buffer
Preallocated Buffer
Caller-Owned Buffer
```

این موضوع باعث می‌شود رفتار Firmware قابل پیش‌بینی‌تر باشد.

---

# ۲۱. Storage Address Abstraction

Management نباید به Address فیزیکی Memory وابسته باشد.

غلط:

```c
EEPROM_Read(0x3A20, ...);
```

در Management.

صحیح:

```c
ConfigStorage_Read(...);
```

یا:

```c
Storage_Read(StorageObject_Config, ...);
```

به این ترتیب تغییر Layout حافظه باعث تغییر Management نمی‌شود.

---

# ۲۲. Storage Object

برای داده‌های مختلف می‌توان Object یا Region منطقی تعریف کرد:

```text
STORAGE_CONFIG
STORAGE_EVENT
STORAGE_TELEMETRY
STORAGE_FIRMWARE_UPDATE
STORAGE_RECOVERY
```

این نام‌ها Contract منطقی هستند و به Address فیزیکی وابسته نیستند.

---

# ۲۳. Fault

نمونه Faultهای Storage:

```text
Read Failure
Write Failure
Erase Failure
Integrity Failure
Invalid Record
Storage Corruption
No Space
Device Not Ready
```

Storage باید Failure را به Caller اعلام کند.

اما تصمیم درباره Recovery متعلق به Management یا Policy صاحب داده است.

---

# ۲۴. Reset

Storage در صورت Failure نباید خودسرانه MCU را Reset کند.

مدل:

```text
Storage
   ↓
Failure
   ↓
Owner Management
   ↓
Recovery Decision
   ↓
Reset Request
   ↓
System Management
```

هر Storage Failure الزاماً به Reset منجر نمی‌شود.

---

# ۲۵. Power Mode

در `LOW_POWER` عملیات Storage باید با توجه به مصرف و زمان‌بندی مدیریت شود.

اگر عملیات حیاتی در حال انجام باشد، Management صاحب عملیات باید Policy مناسب را اعمال کند.

Storage خودش تصمیم نمی‌گیرد که سیستم وارد `NORMAL` یا `LOW_POWER` شود.

---

# ۲۶. Fake / Stub

Storage باید قابل Fake شدن باشد.

در Unit Test:

```text
Real:
Management
   ↓
Real Storage
   ↓
NOR / EEPROM


Test:
Management
   ↓
Fake Storage
   ↓
RAM Buffer
```

این امکان اجازه می‌دهد Firmware Update، Configuration و سایر Managementها بدون Hardware واقعی تست شوند.

---

# ۲۷. Contract عمومی

Contract عمومی پیشنهادی:

```c
typedef enum
{
    STORAGE_OK,
    STORAGE_BUSY,
    STORAGE_INVALID,
    STORAGE_ERROR
} StorageResult;
```

API پایه:

```c
StorageResult Storage_Read(
    StorageObject object,
    uint32_t offset,
    void* buffer,
    uint32_t length
);

StorageResult Storage_Write(
    StorageObject object,
    uint32_t offset,
    const void* data,
    uint32_t length
);

StorageResult Storage_Erase(
    StorageObject object
);

StorageResult Storage_GetStatus(
    StorageObject object
);

void Storage_Process(void);
```

Contract واقعی می‌تواند برای عملیات بزرگ، APIهای Asynchronous اختصاصی داشته باشد.

---

# ۲۸. Contractهای تخصصی

برای جلوگیری از Generic شدن بیش از حد، همه داده‌ها نباید الزاماً از یک API عمومی عبور کنند.

مثلاً:

```text
Config Storage Contract
        ↓
Firmware Update Storage Contract
        ↓
Event Persistence Contract
```

همگی می‌توانند روی Storage Infrastructure مشترک قرار بگیرند.

اصل:

> **Infrastructure مشترک است؛ Contract مصرف‌کننده می‌تواند تخصصی باشد.**

---

# ۲۹. مرز با Driver

```text
Management
     ↓
Storage Contract
     ↓
Storage Infrastructure
     ↓
Memory Driver
     ↓
Hardware
```

Storage Infrastructure مسئول منطق ذخیره‌سازی است.

Driver فقط عملیات سخت‌افزاری را انجام می‌دهد.

مثلاً:

```text
NOR Driver
    → Read Page
    → Program Page
    → Erase Sector
```

اما:

```text
Storage
    → Record
    → Integrity
    → Validity
    → Atomic Update
```

را مدیریت می‌کند.

---

# ۳۰. مرز مسئولیت

| موضوع                  | Storage | Management |
| ---------------------- | ------: | ---------: |
| Read/Write فیزیکی      |       ✓ |            |
| Erase                  |       ✓ |            |
| Storage State          |       ✓ |            |
| Integrity مکانیزم      |       ✓ |            |
| انتخاب داده قابل ذخیره |         |          ✓ |
| Persistence Policy     |         |          ✓ |
| Business Meaning       |         |          ✓ |
| Recovery Decision      |         |          ✓ |
| Reset                  |         |          ✓ |
| Firmware Update Policy |         |          ✓ |
| Bank Selection در Boot |         | Bootloader |

---

# ۳۱. اصل نهایی

Memory / Storage Infrastructure باید یک **سرویس ذخیره‌سازی قابل اعتماد و قابل تعویض** باشد:

```text
Management
    ↓
Logical Storage Contract
    ↓
Storage Infrastructure
    ↓
Driver
    ↓
Memory
```

و سه اصل اصلی آن:

> **Storage نمی‌داند داده چه معنایی دارد.**

> **Storage تصمیم نمی‌گیرد چه چیزی باید Persistent باشد.**

> **Storage هیچ‌گاه برای تکمیل عملیات، Super Loop را متوقف نمی‌کند.**

در نتیجه، Managementها می‌توانند بدون وابستگی به EEPROM، NOR، آدرس‌های فیزیکی یا جزئیات Driver، داده مورد نیاز خود را ذخیره و بازیابی کنند.
