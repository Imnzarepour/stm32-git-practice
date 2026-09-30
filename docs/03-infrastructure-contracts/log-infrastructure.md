# پیوست فنی — زیرساخت ثبت و ذخیره‌سازی Log

## ۰. یادداشت هماهنگی با اسناد/کد موجود

این سند از ابتدا به‌درستی به‌عنوان **Infrastructure** طبقه‌بندی شده (نه Management) — مالک معنای Log همیشه Management تولیدکننده است، نه خود این زیرساخت. لیست ۱۰تایی Managementهای رسمی با این سند تغییر نمی‌کند.

**دو پیامد مستقیم روی کد موجود که باید اعمال شوند:**

1. **صف هفتم اضافه می‌شود.** `firmware/include/shared/queues.h` و `queues.c` باید `Log Queue` را کنار ۶ صف موجود (Event, SMS Request, Call Request, Server Event, Telemetry, Fault) اضافه کنند.
2. **رفتار `persist` در Event Policy باید اصلاح شود.** `event_policy.c` فعلاً وقتی `persist == true` مستقیم `Storage_Write(STORAGE_EVENT, ...)` را صدا می‌زند — این با مدل این سند هماهنگ نیست. رفتار درست: ساخت یک `Log Record` (`Source=EVENT_POLICY, Type=event.event_type, Timestamp, Payload=event`) و قرار دادن آن در **Log Queue**.

این سند خودش این دو مورد را فرض نمی‌کند؛ فقط اینجا به‌عنوان یادداشت هماهنگی ثبت شده‌اند.

---

## ۱. هدف

فراهم کردن یک مسیر غیرهمزمان و Non-Blocking برای دریافت و ذخیره Logهایی که توسط Managementهای مختلف Firmware تولید می‌شوند. Log Infrastructure تصمیم نمی‌گیرد **چه چیزی** باید Log شود؛ هر Management در صورت نیاز، Log Record خودش را تولید و در Log Queue قرار می‌دهد.

```text
Management → Log Record → Log Queue → Log Infrastructure → Storage / File API → LOG Partition
```

---

## ۲. جایگاه Log در معماری

> **Management تصمیم می‌گیرد چه چیزی Log شود؛ Log Infrastructure مسئول ذخیره کردن آن است.**

Log Infrastructure درباره این موارد تصمیم نمی‌گیرد: کدام Event مهم است، کدام Fault باید Log شود، Severity رخداد، معنای محصولی Log، یا Action مناسب در واکنش به یک Log.

---

## ۳. تولید Log

هر Management در صورت نیاز یک Log Record می‌سازد، مثلاً:

```text
Diagnostic Management → Diagnostic Failure → Log Record → Log Queue
Actuator Management    → Command Rejected   → Log Record → Log Queue
System Management      → Boot Error         → Log Record → Log Queue
```

Management پس از قرار دادن Record در Queue، منتظر نوشته شدن روی NOR نمی‌ماند.

---

## ۴. Log Queue

```text
Management A ─┐
Management B ─┤
Management C ─┼→ Log Queue → Log Infrastructure
Management D ─┘
```

Log Queue فقط ذخیره موقت Record است؛ مسئول File Management، Append، Rotation، NOR Access، LittleFS یا تصمیم‌گیری محتوایی نیست.

---

## ۵. پردازش Log Queue

```text
Super Loop → Log_Process() → Check Log Queue → Take Log Record → Append to Current File
```

Non-Blocking؛ اگر عملیات Storage زمان‌بر باشد:

```text
Log Queue → Start Append → Return → Storage_Process() → Continue → Complete
```

---

## ۶. Log Record

حداقل Contract منطقی:

```text
Log Record
├── Source
├── Type
├── Timestamp
└── Payload
```

معنای `Type` و `Payload` توسط Management تولیدکننده تعیین می‌شود؛ Log Infrastructure برای هر نوع Log منطق اختصاصی ندارد.

---

## ۷. Timestamp

هر Record در صورت وجود Time Base معتبر، Timestamp دارد. Log Infrastructure آن را تفسیر نمی‌کند، فقط همراه Record ذخیره می‌کند.

---

## ۸. ذخیره‌سازی فایل

```text
LOG Partition
├── Log File
├── Log File
├── Log File
└── Log File
```

Record جدید به انتهای فایل جاری Append می‌شود؛ برای هر Record فایل جدید ساخته نمی‌شود.

---

## ۹. محدودیت حجم فایل

```text
Current File = FULL → Close / Rotate → Select Next File
```

---

## ۱۰. File Rotation (Rolling / Circular)

```text
Before:  log_001(oldest) log_002 log_003 log_004(current)
After:   log_002 log_003 log_004 log_001(reused as new)
```

> **Logهای جدید جایگزین قدیمی‌ترین Logها می‌شوند و مصرف Storage از ظرفیت تعیین‌شده عبور نمی‌کند.**

---

## ۱۱. حذف و استفاده مجدد از فایل قدیمی

```text
LOG Partition Full → Find Oldest File → Delete/Reinitialize → Use as New Log File
```

جزئیات فیزیکی Delete/Erase/Reuse متعلق به Storage Infrastructure است؛ Log Infrastructure فقط از File Contract استفاده می‌کند.

---

## ۱۲. مرز Log و Storage

```text
Log Infrastructure → Logical File API → Storage Infrastructure → LittleFS → NOR Driver → W25Q128
```

Log Infrastructure هرگز مستقیم با SPI، Register NOR، Page، Sector، Erase، آدرس فیزیکی یا LittleFS API کار نمی‌کند.

---

## ۱۳. عدم Blocking

```text
غلط:  Management → Write NOR → Wait → Continue
صحیح: Management → Put Log Record → Continue   (بعداً: Log Infrastructure → Queue → Storage)
```

---

## ۱۴. رفتار در صورت پر شدن Log Queue

سیاست عمومی Queue سیستم اعمال می‌شود: `Queue Full → Drop Oldest → Insert Newest`. این سیاست متعلق به خود Queue Infrastructure است؛ Log Infrastructure آن را اختصاصی مدیریت نمی‌کند.

---

## ۱۵. رفتار در صورت خطای Storage

خطای ذخیره‌سازی Log نباید Super Loop را متوقف کند یا باعث Reset سیستم شود. ثبت Fault مربوط به Log (در صورت نیاز) از مسیر Fault Contract سیستم انجام می‌شود و نباید زنجیره‌ای از Logهای نامحدود ایجاد کند.

---

## ۱۶. رفتار در Power Mode

مالک Power Policy نیست. در `LOW_POWER` می‌تواند فعالیت (دفعات دسترسی به Storage) کاهش یابد، بدون نقض Contractهای بالادستی. تصمیم درباره اینکه در Low Power چه چیزی Log شود، با Management تولیدکننده است.

---

## ۱۷. Process

```c
void Log_Process(void);
```

Non-Blocking؛ Queue را بررسی می‌کند؛ ذخیره‌سازی را پیش می‌برد؛ وضعیت عملیات جاری را مدیریت می‌کند؛ در هر فراخوانی فقط به اندازه لازم پیش می‌رود؛ منتظر Completion سخت‌افزار/Storage نمی‌ماند.

---

## ۱۸. Testability

بدون NOR واقعی قابل تست: `Log Infrastructure → Fake Storage/File API`. موارد قابل تست: دریافت از Queue، Append، File Full، Rotation، انتخاب قدیمی‌ترین فایل، Reuse، Storage Error، Queue Full، ترتیب Logها، رفتار Non-Blocking، رفتار پس از Reset.

---

## ۱۹. Fake / Stub

```text
Real: Log Infrastructure → Storage → LittleFS → NOR
Test: Log Infrastructure → Fake Storage → RAM
```

منطق Rotation و Queue بدون W25Q128 واقعی قابل تست است.

---

## ۲۰. مرز مسئولیت

| موضوع | Management تولیدکننده | Log Infrastructure | Storage |
|---|:-:|:-:|:-:|
| تصمیم برای Log کردن | ✓ | | |
| ساخت Log Record | ✓ | | |
| تعیین معنای Log | ✓ | | |
| قرار دادن در Queue | ✓ | | |
| مصرف Log Queue | | ✓ | |
| Append | | ✓ | ✓ |
| File Rotation | | ✓ | |
| انتخاب فایل قدیمی | | ✓ | |
| File API | | ✓ | ✓ |
| LittleFS / NOR / SPI / Erase-Program | | | ✓ |
| Power Policy | ✓ | | |

---

## ۲۱. Contract سطح معماری

```text
Log Infrastructure

Responsibility: Consume Log Queue, Append Log Records, Manage Current Log File,
                 Perform File Rotation, Reuse Oldest File, Handle Storage/File Errors
Input:          Log Queue, Time/Storage Status
Output:         Persistent Log Files
Storage:        LOG Partition
Behavior:       Append, Rolling/Circular File Storage
Constraints:    Non-Blocking · Super Loop Compatible · No direct NOR/SPI Access
                No Product Logic · No Decision on What to Log · No Event Policy Ownership
                No Reset · No Dynamic Allocation in Normal Path
```

---

## ۲۲. اصل نهایی

```text
Management → "Log this" → Log Record → Log Queue → Log Infrastructure
    → Append / File Rotation / Reuse Oldest → Storage/File API → LOG Partition
```

> **Management مالک تصمیم و معنای Log است؛ Log Queue واسط موقت است؛ Log Infrastructure مالک فرآیند ثبت و چرخش فایل‌هاست؛ Storage مالک مکانیزم فیزیکی ذخیره‌سازی است.**

---

## ۲۳. موارد باز برای OPEN-ITEMS.md

| # | مورد |
|---|---|
| 1 | صف Log باید به `queues.h`/`queues.c` اضافه شود (کد موجود فقط ۶ صف دارد) |
| 2 | `event_policy.c` باید از `Storage_Write` مستقیم به الگوی `Log Record → Log Queue` تغییر کند |
| 3 | ظرفیت LOG Partition همچنان بخشی از مسئله بلاک‌کننده ظرفیت کلی NOR است (`nor-flash-infrastructure.md` #۱) |
