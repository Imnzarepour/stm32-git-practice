# پیوست فنی — تعریف پروژه برون‌سپاری طراحی و پیاده‌سازی زیرساخت NOR Flash و مدیریت حافظه

## 1. هدف پروژه

هدف این پروژه، **طراحی، پیاده‌سازی، تست و تحویل کامل زیرساخت ذخیره‌سازی مبتنی بر NOR Flash خارجی** برای Firmware محصول است؛ به‌گونه‌ای که Managementهای مختلف Firmware بدون وابستگی به جزئیات فیزیکی حافظه بتوانند داده‌های خود را ذخیره، بازیابی، ثبت، حذف و مدیریت کنند.

این پروژه باید یک زیرساخت مستقل و قابل استفاده توسط Application Firmware تحویل دهد و جزئیات زیر را از Managementهای محصول پنهان کند:

* نوع و مدل NOR Flash
* آدرس‌های فیزیکی
* Sector / Block / Page
* عملیات Erase / Program
* مدیریت Power Loss
* مدیریت فایل و فضای آزاد
* Wear Management
* Recovery
* خطاهای سخت‌افزاری حافظه
* جزئیات LittleFS یا هر مکانیزم Storage داخلی انتخاب‌شده

معماری مورد انتظار:

```text
Application Management
        │
        ▼
Specialized Storage Service
        │
        ▼
Storage Infrastructure
        │
        ├── Partition Manager
        ├── File System / Storage Engine
        ├── Integrity / Recovery
        └── NOR Driver
                │
                ▼
             NOR Flash
```

اصل اصلی:

> **Management باید مفهوم داده را بشناسد، ولی نباید هیچ شناختی از نحوه فیزیکی ذخیره شدن آن داده داشته باشد.**

---

# 2. محدوده پروژه

پروژه شامل موارد زیر است:

1. تحلیل و انتخاب معماری ذخیره‌سازی NOR
2. تعریف Memory Map
3. طراحی Partitionها
4. طراحی و پیاده‌سازی NOR Driver
5. انتخاب و Integration فایل‌سیستم مناسب، ترجیحاً LittleFS در صورت تأیید فنی
6. پیاده‌سازی Storage Infrastructure
7. مدیریت فایل و Directory
8. مدیریت Partition
9. مدیریت Integrity
10. Recovery پس از قطع برق
11. مدیریت خطا
12. مدیریت فضای آزاد
13. مدیریت عملیات غیرهمزمان
14. تست Unit
15. تست Integration
16. تست Power Loss
17. تست ظرفیت و Performance
18. تست طول عمر / Write-Erase
19. مستندسازی کامل
20. تحویل Source Code، Test و Documentation

---

# 3. داده‌های مورد نیاز سیستم

NOR Flash در این محصول برای چند نوع داده مستقل استفاده خواهد شد.

## 3.1 GPS Data

GPS داده‌ها را به صورت Record تولید می‌کند.

نمونه:

```text
Timestamp
Latitude
Longitude
Altitude
Speed
...
```

ویژگی‌ها:

* Append-oriented
* نرخ نوشتن نسبتاً بالا
* حجم محدود
* نگهداری موقت تا زمان ارسال به Server
* پس از ارسال موفق، داده قابل حذف است
* در صورت پر شدن فضای اختصاص‌یافته، Policy حذف داده قدیمی باید اجرا شود

نمونه Policy:

```text
GPS Partition
    ↓
File 1
File 2
...
    ↓
Network Available
    ↓
Upload Oldest
    ↓
Successful ACK
    ↓
Delete File
```

---

# 4. Log Data

Log برای ثبت اطلاعات نرم‌افزار و خطاها استفاده می‌شود.

ویژگی‌ها:

* Append-oriented
* حجم کل از قبل مشخص
* نباید بتواند فضای سایر داده‌ها را مصرف کند
* داده جدید جایگزین داده قدیمی می‌شود
* Circular / Rolling Storage مورد انتظار است

یک نمونه معماری:

```text
LOG Partition = 2 MB

┌────────────────────┐
│ Log File A = 1 MB  │
├────────────────────┤
│ Log File B = 1 MB  │
└────────────────────┘
```

رفتار:

```text
A → Fill
B → Fill
A → Erase / Reuse
B → Erase / Reuse
...
```

هدف این است که تقریباً همیشه نزدیک به ظرفیت تعریف‌شده، جدیدترین Logها حفظ شوند.

پیاده‌سازی دقیق این مکانیزم باید توسط پیمانکار پیشنهاد و با تیم اصلی تأیید شود.

---

# 5. Diagnostic / Operational Files

سیستم Diagnostic ممکن است نیازمند ذخیره فایل‌های عملیاتی باشد.

این داده‌ها برخلاف GPS و Log الزاماً Append-only نیستند.

ممکن است عملیات‌هایی مانند:

```text
Create
Write
Read
Update
Delete
```

نیاز باشند.

بنابراین برای این Partition استفاده از File System مانند LittleFS می‌تواند مناسب باشد.

---

# 6. FOTA Data

FOTA نیازمند فضای ذخیره‌سازی برای Firmware Image است.

FOTA Image ممکن است شامل:

* Firmware Binary
* Metadata
* Version
* Size
* Integrity Information
* Signature Information

باشد.

FOTA Storage باید از سایر داده‌ها مستقل باشد.

اصل مهم:

> GPS یا Log نباید بتواند فضای مورد نیاز FOTA را مصرف کند.

Bootloader مالک نهایی فرآیند انتخاب و اجرای Firmware است.

FOTA Management مسئول دریافت و آماده‌سازی Image است.

Storage Infrastructure مسئول ذخیره‌سازی فیزیکی Image است.

---

# 7. Configuration

Configuration در معماری فعلی عمدتاً در EEPROM داخلی MCU قرار می‌گیرد و جزو مصرف‌کنندگان اصلی NOR محسوب نمی‌شود.

بنابراین NOR Storage Architecture نباید Configuration را به صورت پیش‌فرض مالک شود، مگر اینکه در ادامه پروژه تصمیم دیگری اتخاذ شود.

---

# 8. Event Data

Eventهای Runtime در معماری فعلی ابتدا در RAM / Queue نگهداری می‌شوند.

در صورت نیاز به Persistence، امکان انتقال Event به NOR باید توسط Storage Contract پشتیبانی شود، ولی Persistence Event نباید به صورت پیش‌فرض الزامی فرض شود.

---

# 9. پیشنهاد اولیه Memory Map

NOR Flash فعلی:

```text
Total Capacity = 16 MB
```

تقسیم اولیه پیشنهادی:

```text
NOR Flash
│
├── FOTA Partition
├── GPS Partition
├── LOG Partition
└── Diagnostic Partition
```

تقسیم دقیق ظرفیت‌ها در شروع پروژه باید بر اساس موارد زیر نهایی شود:

* ظرفیت واقعی Firmware
* اندازه رکورد GPS
* نرخ تولید GPS
* مدت نگهداری GPS
* نرخ تولید Log
* اندازه مورد نیاز Diagnostic
* Reserved Space
* Flash Geometry
* Erase Block Size
* Alignment Requirements
* File System Overhead

تقسیم 16MB صرفاً باید پس از تحلیل واقعی ظرفیت نهایی شود.

نمونه اولیه صرفاً برای بحث:

```text
FOTA       8 MB
GPS        2 MB
LOG        2 MB
DIAG       4 MB
----------------
TOTAL     16 MB
```

این مقادیر **Requirement نهایی نیستند**.

---

# 10. Partition Isolation

هر Partition باید از نظر فضای قابل استفاده مستقل باشد.

به عنوان مثال:

```text
GPS FULL
```

نباید باعث شود:

```text
LOG
FOTA
DIAG
```

فضای خود را از دست بدهند.

همچنین:

```text
LOG FULL
```

نباید باعث شکست FOTA شود.

این Isolation یکی از اهداف اصلی Partitioning است.

---

# 11. File System

پیشنهاد اولیه پروژه استفاده از **LittleFS** است.

انتخاب LittleFS باید در ابتدای پروژه از نظر موارد زیر بررسی و تأیید شود:

* NOR Flash Compatibility
* RAM Usage
* Flash Overhead
* Performance
* Power Loss Recovery
* Wear Behavior
* File Size
* Number of Files
* Number of Partitions
* Concurrent Access
* Long-term Reliability

در صورت استفاده از LittleFS، هر Partition می‌تواند Filesystem مستقل داشته باشد:

```text
NOR
│
├── GPS Partition
│      └── LittleFS
│
├── LOG Partition
│      └── LittleFS
│
├── DIAG Partition
│      └── LittleFS
│
└── FOTA Partition
       └── LittleFS
```

اما استفاده از Raw Storage برای برخی Partitionها نیز می‌تواند توسط پیمانکار پیشنهاد شود.

**انتخاب نهایی باید بر اساس Requirement و Test باشد، نه صرفاً ترجیح تکنولوژی.**

---

# 12. اصل مهم: Partition با File System یکی نیست

Partition مسئول ایجاد یک محدوده مستقل از حافظه است.

File System مسئول مدیریت داده داخل آن محدوده است.

بنابراین:

```text
Partition
    ≠
File System
```

و:

```text
NOR Flash
    ↓
Partition
    ↓
LittleFS / Storage Engine
    ↓
Files / Records
```

---

# 13. NOR Driver

پیمانکار باید Driver مناسب NOR Flash را پیاده‌سازی یا Integration کند.

Driver مسئول:

* Initialization
* Read
* Page Program
* Sector Erase
* Block Erase در صورت وجود
* Busy Status
* Ready Status
* JEDEC ID / Device Identification
* Hardware Error
* SPI/QSPI/OSPI Interface در صورت استفاده
* DMA در صورت نیاز
* Reset / Recovery در صورت پشتیبانی
* Hardware-specific timing

Driver نباید شامل:

* GPS Logic
* Log Policy
* FOTA Policy
* Diagnostic Logic
* File Naming Policy
* Business Logic

باشد.

معماری:

```text
Storage
   ↓
NOR Driver
   ↓
SPI/QSPI Driver
   ↓
NOR Hardware
```

---

# 14. Flash Geometry

پیمانکار باید مشخصات فیزیکی Flash را به صورت رسمی مستند کند:

* Total Capacity
* Page Size
* Sector Size
* Block Size
* Erase Time
* Program Time
* Read Time
* Maximum Clock
* Interface Mode
* Endurance
* Data Retention
* Power Requirements
* Alignment Constraints

این اطلاعات باید در یک سند Hardware/Storage Configuration ثبت شوند.

---

# 15. Storage API

Managementها نباید مستقیماً LittleFS یا NOR Driver را فراخوانی کنند.

پیشنهاد می‌شود Storage Contract حداقل قابلیت‌های زیر را ارائه دهد:

```c
StorageResult Storage_Open(...);

StorageResult Storage_Read(...);

StorageResult Storage_Write(...);

StorageResult Storage_Append(...);

StorageResult Storage_Delete(...);

StorageResult Storage_Close(...);

StorageResult Storage_GetStatus(...);

StorageResult Storage_GetFreeSpace(...);

void Storage_Process(void);
```

اما API نهایی باید بر اساس مدل نهایی Storage طراحی شود.

برای GPS و Log احتمالاً API تخصصی‌تر از File API عمومی مناسب‌تر است.

مثلاً:

```c
GPSStorage_Append(...);
GPSStorage_GetNextPending(...);
GPSStorage_Delete(...);
```

و:

```c
LogStorage_Append(...);
LogStorage_GetOldest(...);
```

این APIها باید روی Storage Infrastructure قرار بگیرند.

---

# 16. Non-Blocking Requirement

Firmware از Cooperative Non-Blocking Super Loop استفاده می‌کند.

بنابراین Storage نباید Super Loop را برای مدت نامعلوم متوقف کند.

عملیات‌هایی مانند:

* Erase
* Large Write
* Large Read
* File Operation
* Recovery

باید در صورت طولانی بودن، به شکل Stateful / Stepwise اجرا شوند.

نمونه:

```text
Storage_Process()
       ↓
Start Operation
       ↓
Check Status
       ↓
Small Step
       ↓
Return
       ↓
Next Super Loop
```

نباید چنین الگویی وجود داشته باشد:

```c
while (!flash_ready)
{
    ;
}
```

یا:

```c
delay(...)
```

---

# 17. Power Loss

Power Loss یکی از الزامات مهم این پروژه است.

حداقل سناریوهای زیر باید تست شوند:

### هنگام Write

```text
Write
 ↓
Power Loss
```

### هنگام Append

```text
Append Record
 ↓
Power Loss
```

### هنگام File Delete

```text
Delete
 ↓
Power Loss
```

### هنگام File Rename

```text
Rename
 ↓
Power Loss
```

### هنگام Filesystem Metadata Update

```text
Metadata Update
 ↓
Power Loss
```

### هنگام FOTA

```text
FOTA Write
 ↓
Power Loss
```

پس از Boot باید سیستم بتواند:

* Filesystem را Recover کند
* فایل‌های معتبر را حفظ کند
* داده ناقص را تشخیص دهد
* Storage را در وضعیت قابل استفاده قرار دهد

---

# 18. GPS Reliability Model

برای GPS، هر Record باید حداقل قابلیت تشخیص معتبر/نامعتبر بودن را داشته باشد.

نمونه مفهومی:

```text
GPS Record
├── Sequence
├── Timestamp
├── Position
├── Data
└── Integrity Check
```

در صورت قطع برق هنگام نوشتن یک Record:

```text
Complete Record
    → Valid

Incomplete Record
    → Invalid / Ignored
```

یک Record ناقص نباید باعث از بین رفتن کل History شود.

---

# 19. GPS File Lifecycle

مدل پیشنهادی:

```text
ACTIVE
  ↓
FULL
  ↓
PENDING_UPLOAD
  ↓
UPLOADING
  ↓
UPLOAD_SUCCESS
  ↓
DELETE
```

در صورت قطع ارتباط:

```text
UPLOADING
   ↓
Communication Lost
   ↓
PENDING_UPLOAD
```

و فایل نباید حذف شود.

---

# 20. At-Least-Once Delivery

برای داده‌هایی که از NOR به Server ارسال می‌شوند، حذف داده باید بعد از اطمینان از تحویل انجام شود.

اصل:

```text
Read
 ↓
Send
 ↓
Server ACK
 ↓
Delete
```

نه:

```text
Read
 ↓
Delete
 ↓
Send
```

در صورت Reset یا قطع ارتباط، داده باید قابلیت ارسال مجدد داشته باشد.

---

# 21. Log Storage

Log Storage باید دارای ظرفیت مشخص و قابل تضمین باشد.

مثال:

```text
LOG_PARTITION = 2 MB
```

Storage نباید اجازه دهد Log بیش از این ظرفیت مصرف کند.

Policy پیشنهادی:

```text
Append
 ↓
Segment Full
 ↓
Next Segment
 ↓
Oldest Segment Reused
```

پیمانکار باید تعداد مناسب Segmentها را با توجه به:

* Erase Block
* Page Size
* Log Rate
* Endurance

پیشنهاد کند.

---

# 22. Wear / Endurance

پیمانکار باید تحلیل Endurance ارائه دهد.

تحلیل باید شامل:

```text
Data Rate
×
Retention
×
Erase Frequency
×
Expected Product Lifetime
```

باشد.

برای GPS و Log باید مشخص شود هر Physical Erase Block در طول عمر محصول چند بار Erase خواهد شد.

صرفاً تعداد Writeها ملاک نیست.

---

# 23. File Deletion

Delete یک فایل باید به عنوان عملیات منطقی در نظر گرفته شود.

Filesystem مسئول مدیریت فضای آزاد پس از Delete خواهد بود.

Management نباید درگیر موارد زیر شود:

* Physical Block
* Sector Erase
* Garbage Collection
* Allocation
* Wear Leveling

---

# 24. Space Management

Storage باید قابلیت گزارش موارد زیر را داشته باشد:

```text
Total Space
Used Space
Free Space
```

و در صورت نیاز:

```text
Partition Status
Filesystem Status
Storage Error
```

مثلاً:

```c
Storage_GetFreeSpace(PARTITION_GPS);
```

اما Policy تصمیم‌گیری درباره Full شدن باید متعلق به Storage Service / Management مربوطه باشد.

---

# 25. Recovery

Storage باید بتواند وضعیت خود را پس از:

* MCU Reset
* Watchdog Reset
* Power Loss
* Brownout
* Interrupted Write
* Interrupted Erase

بازیابی کند.

Recovery نباید نیازمند دخالت Application Management باشد، مگر در مواردی که داده از نظر معنایی نیاز به تصمیم Application داشته باشد.

---

# 26. Integrity

برای داده‌های مهم باید مکانیزم Integrity وجود داشته باشد.

بسته به نوع داده می‌توان از:

* CRC
* Sequence Number
* Length
* Magic
* Version

استفاده کرد.

برای FOTA علاوه بر Integrity، در صورت Requirement محصول، Authenticity / Signature نیز باید بررسی شود.

---

# 27. FOTA Storage Boundary

FOTA Management:

```text
Download
Validate
Prepare
Request Installation
```

Storage:

```text
Store
Read
Write
Verify Storage Integrity
```

Bootloader:

```text
Validate Image
Select Image
Program / Activate
Rollback / Recovery
```

هیچ‌کدام نباید مسئولیت دیگری را تصاحب کنند.

---

# 28. Diagnostic Storage

Diagnostic باید بتواند فایل‌های عملیاتی را ایجاد و مدیریت کند.

حداقل قابلیت‌ها:

```text
Create
Write
Read
Close
Delete
Get Size
Get Status
```

در صورت نیاز:

```text
Rename
List
```

استفاده از LittleFS برای این Partition باید بررسی شود.

---

# 29. Concurrency

Firmware فاقد RTOS است.

بنابراین Concurrency عمدتاً بین:

```text
ISR / DMA
        ↕
Super Loop
```

است.

Storage باید مشخص کند:

* آیا API در ISR مجاز است؟
* آیا API ISR-safe است؟
* آیا Write از ISR ممنوع است؟
* Buffer Ownership چگونه است؟
* آیا همزمان فقط یک Operation مجاز است؟

به صورت پیش‌فرض:

> Storage Write / Erase از ISR مجاز نیست.

ISR صرفاً باید Event یا Flag لازم برای پردازش Super Loop را ثبت کند.

---

# 30. Memory Allocation

استفاده از Dynamic Allocation در مسیرهای عادی Storage ترجیح داده نمی‌شود.

Bufferها ترجیحاً:

* Static
* Preallocated
* Caller-owned

باشند.

پیمانکار باید RAM مصرفی را اندازه‌گیری و مستند کند.

---

# 31. Configuration قابل تغییر

این موارد باید تا حد امکان Configuration باشند:

```text
Partition Size
File Size
Maximum Files
GPS Record Size
Log Segment Size
Queue Size
Retry Count
Storage Timeout
```

اما Configuration Runtime نباید باعث تغییر ناایمن Memory Map فیزیکی شود.

Memory Map باید Compile-Time / Manufacturing Configuration کنترل‌شده داشته باشد.

---

# 32. تست‌های الزامی

پیمانکار باید Unit Test برای حداقل موارد زیر ارائه دهد:

### NOR Driver

* Read
* Program
* Erase
* Busy
* Error
* Timeout
* Invalid Address

### Storage

* Open
* Read
* Write
* Append
* Delete
* Full
* Empty
* Corruption
* Recovery

### GPS

* Append
* File Full
* New File
* Upload
* Delete after ACK
* Re-upload after failure

### Log

* Append
* Segment Full
* Rotation
* Oldest overwrite
* Recovery

### FOTA

* Partial Download
* Complete Image
* Corrupt Image
* Power Loss
* Resume if supported

---

# 33. Power Loss Test

یکی از Deliverableهای مهم باید تست واقعی Power Loss باشد.

حداقل:

```text
Write N
 ↓
قطع تغذیه در نقاط مختلف
 ↓
Power On
 ↓
Mount / Recovery
 ↓
Validate Data
```

این تست باید در نقاط مختلف عملیات تکرار شود.

---

# 34. Performance Test

پیمانکار باید موارد زیر را اندازه‌گیری کند:

* Read Throughput
* Write Throughput
* Append Latency
* Erase Time
* File Open Time
* File Delete Time
* Mount Time
* Recovery Time
* RAM Usage
* CPU Usage

تست‌ها باید با Hardware واقعی انجام شوند.

---

# 35. Endurance Test

برای GPS و Log یک تست طولانی‌مدت باید طراحی شود.

مثلاً:

```text
Generate Data
      ↓
Append
      ↓
Fill
      ↓
Rotate
      ↓
Erase
      ↓
Reuse
      ↓
Repeat
```

هدف، بررسی رفتار واقعی Storage در تعداد زیادی Cycle است.

---

# 36. Fault Injection

پیمانکار باید امکان شبیه‌سازی موارد زیر را فراهم کند:

```text
NOR Read Error
NOR Write Error
NOR Erase Error
Timeout
Corrupted Record
Corrupted Metadata
Power Loss
Unexpected Reset
Filesystem Corruption
```

این قابلیت باید تا حد امکان در Unit/Integration Test قابل استفاده باشد.

---

# 37. Fake / Mock Storage

Storage باید از ابتدا قابل Fake شدن باشد.

مثلاً Management GPS در Unit Test نباید نیازمند NOR واقعی باشد.

```text
GPS Management
      ↓
GPS Storage Contract
      ↓
Fake Storage
```

و در محصول:

```text
GPS Management
      ↓
GPS Storage Contract
      ↓
Real Storage
      ↓
LittleFS
      ↓
NOR
```

این موضوع برای تست و توسعه موازی الزامی است.

---

# 38. Dependency Injection

ساختار باید امکان جایگزینی Dependencyها را داشته باشد.

مثلاً:

```text
Real NOR Driver
Fake NOR Driver
```

و:

```text
Real Storage
Fake Storage
```

بدون تغییر در منطق GPS / Log / FOTA / Diagnostic.

---

# 39. Error Model

حداقل خطاهای زیر باید قابل تشخیص باشند:

```text
STORAGE_OK
STORAGE_BUSY
STORAGE_NOT_READY
STORAGE_FULL
STORAGE_NOT_FOUND
STORAGE_INVALID
STORAGE_READ_ERROR
STORAGE_WRITE_ERROR
STORAGE_ERASE_ERROR
STORAGE_CORRUPTED
STORAGE_TIMEOUT
STORAGE_NO_SPACE
```

Error Codeهای نهایی باید در پروژه تعریف و مستند شوند.

Storage نباید خودش بدون Contract تصمیم به System Reset بگیرد.

در صورت Failure جدی:

```text
Storage
   ↓
Fault
   ↓
Owner Management
   ↓
Recovery Decision
   ↓
System Management
```

---

# 40. Ownership

اصل مالکیت داده:

```text
GPS Management
    → مالک معنای GPS

Log
    → مالک معنای Log

Diagnostic Management
    → مالک معنای Diagnostic

FOTA Management
    → مالک معنای Firmware Update

Storage
    → مالک مکانیزم ذخیره‌سازی
```

Storage نباید بداند:

> این داده GPS است یا DTC یا Firmware.

مگر در Storage Service تخصصی که Contract آن مشخصاً چنین معنایی را تعریف کرده باشد.

---

# 41. خروجی‌های مورد انتظار پروژه

پروژه برون‌سپاری باید حداقل Deliverableهای زیر را تحویل دهد:

### D1 — Storage Architecture

شامل:

* Architecture
* Layering
* Dependency
* Storage Model
* Partition Model
* File System Decision

### D2 — NOR Driver

* Source
* Header
* Configuration
* Hardware Integration
* Test

### D3 — Partition Manager

* Partition Definition
* Address Mapping
* Size Management
* Validation

### D4 — Storage Infrastructure

* Storage API
* State Machine
* Error Handling
* Recovery
* Integrity

### D5 — LittleFS Integration

در صورت انتخاب LittleFS:

* Integration
* Configuration
* Port
* Block Device Adapter
* Test

### D6 — GPS Storage

* File/Record Model
* Append
* Rotation
* Pending Data
* Delete
* Recovery

### D7 — Log Storage

* Fixed Capacity
* Circular/Rolling Storage
* Rotation
* Recovery

### D8 — Diagnostic Storage

* File Management
* Lifecycle
* Capacity Handling

### D9 — FOTA Storage

* Image Storage
* Metadata
* Integrity
* Recovery
* Bootloader Interface

### D10 — Test Suite

* Unit Tests
* Integration Tests
* Power Loss Tests
* Fault Injection
* Endurance Tests
* Performance Tests

### D11 — Documentation

* Architecture Document
* API Specification
* Memory Map
* Configuration Guide
* Integration Guide
* Test Report
* Known Limitations
* Maintenance Guide

---

# 42. Definition of Done

این پروژه زمانی کامل محسوب می‌شود که:

1. NOR Flash روی Hardware واقعی شناسایی و راه‌اندازی شده باشد.
2. Partitionها تعریف و تست شده باشند.
3. GPS بتواند داده را Append کند.
4. GPS بتواند فایل‌های Pending را نگهداری کند.
5. پس از ارسال موفق، فایل GPS حذف شود.
6. قطع ارتباط باعث از بین رفتن داده Pending نشود.
7. Log ظرفیت ثابت داشته باشد.
8. Log بتواند به صورت Circular/Rotational کار کند.
9. Diagnostic بتواند فایل عملیاتی ایجاد و مدیریت کند.
10. FOTA بتواند Image را ذخیره کند.
11. هیچ Partition نتواند فضای Partition دیگری را مصرف کند.
12. Power Loss Recovery تست و تأیید شده باشد.
13. Integrity داده‌ها قابل بررسی باشد.
14. Storage در Super Loop Blocking ایجاد نکند.
15. RAM مصرفی اندازه‌گیری و مستند شده باشد.
16. Performance اندازه‌گیری شده باشد.
17. Endurance تحلیل و تست شده باشد.
18. Unit Test برای Storage ارائه شده باشد.
19. Fake Storage / Fake NOR برای تست وجود داشته باشد.
20. Source Code و Documentation کامل تحویل شود.

---

# 43. اصول غیرقابل مذاکره معماری

### اصل 1

```text
Management
    ≠
Storage Implementation
```

### اصل 2

هیچ Management نباید مستقیماً با NOR Driver کار کند.

### اصل 3

هیچ Management نباید آدرس فیزیکی Flash را بداند.

### اصل 4

هیچ Management نباید LittleFS API را مستقیماً مصرف کند.

### اصل 5

Partitionها باید Resource Isolation ایجاد کنند.

### اصل 6

GPS و Log باید ظرفیت قابل پیش‌بینی داشته باشند.

### اصل 7

FOTA باید فضای مستقل و قابل تضمین داشته باشد.

### اصل 8

Power Loss باید به عنوان Failure عادی سیستم در نظر گرفته شود، نه شرایط استثنایی.

### اصل 9

Storage نباید Super Loop را به شکل نامحدود Block کند.

### اصل 10

تمام Storage Services باید Fake/Mock قابل تست داشته باشند.

---

# 44. پیشنهاد معماری نهایی اولیه

```text
                    ┌─────────────────────┐
                    │ Application         │
                    │ Managements         │
                    └──────────┬──────────┘
                               │
                  ┌────────────┼────────────┐
                  │            │            │
                 GPS          LOG          FOTA
               Storage      Storage       Storage
                  │            │            │
                  └────────────┼────────────┘
                               │
                       Storage Infrastructure
                               │
                    ┌──────────┴──────────┐
                    │                     │
              Partition Manager       File System
                                          │
                                       LittleFS
                                          │
                                    NOR Block Device
                                          │
                                      NOR Driver
                                          │
                                    SPI/QSPI Driver
                                          │
                                      NOR Flash
```

---

# 45. نکته نهایی درباره طراحی

هدف پروژه صرفاً این نیست که:

> «یک NOR Flash را راه‌اندازی کنیم.»

هدف این است که یک **Storage Subsystem قابل اتکا، قابل تست، قابل نگهداری و مستقل از Managementهای محصول** تحویل شود.

از دید Application، ترجیحاً سیستم باید به شکل زیر دیده شود:

```text
GPS → Store GPS Data
LOG → Store Log
FOTA → Store Firmware
DIAG → Store File
```

و تمام پیچیدگی‌های زیر در لایه پایین مخفی باشند:

```text
NOR
Erase
Program
Page
Sector
Wear
LittleFS
Metadata
Recovery
Power Loss
Allocation
File System
```

این پروژه باید به نحوی تحویل شود که در آینده بتوان NOR Flash، ظرفیت Flash، Interface یا حتی File System را تغییر داد، بدون اینکه منطق GPS، Log، FOTA و Diagnostic مجبور به بازنویسی شود.

**معیار موفقیت اصلی:**

> Managementها باید «چه چیزی را ذخیره کنند» را بدانند؛ Storage باید «چگونه آن را ذخیره کند» را بداند.

---

## رابطه با سایر اسناد

این سند اسپک/SOW برون‌سپاری برای پیمانکار NOR/Memory است (خروجی مورد انتظار از پیمانکار). Contract معماری سطح Application که Managementهای محصول با آن کار می‌کنند در `docs/03-infrastructure-contracts/memory-storage-infrastructure.md` تعریف شده است.
