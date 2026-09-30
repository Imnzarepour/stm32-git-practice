# پیوست فنی — File Transfer Infrastructure

## ۰. یادداشت طبقه‌بندی

این پیوست در نسخه اولیه «File Transfer **Management**» نام داشت. طبق همان سه معیاری که برای Configuration و Hardware Input استفاده شد:

| معیار | نتیجه |
|---|---|
| مالک معنای فایل است؟ | خیر |
| تصمیم محصولی می‌گیرد؟ | خیر |
| State آن صرفاً State مکانیزم انتقال است؟ | بله |

**نتیجه: این یک Infrastructure است.** لیست ۱۰تایی Managementهای رسمی دست‌نخورده می‌ماند؛ Infrastructureها یک مورد جدید می‌گیرند:

```text
Managements (۱۰ مورد، بدون تغییر)
├── System, Power, Hardware Input, Diagnostic, GPS,
└── Server, SMS, Call, Firmware Update, Actuator

Infrastructure
├── Time Base, Queue, Memory/Storage, Driver Layer,
├── Configuration
└── File Transfer   🆕
```

**نکته کلیدی:** حذف نام «FOTA» از این سند به معنی حذف Capability بروزرسانی Firmware نیست. **Management دهم کاملاً دست‌نخورده باقی می‌ماند** و مالک تمام تصمیم‌های محصولی است: *این فایل Firmware است؟ معتبر است؟ نسخه‌اش مناسب است؟ الان نصب شود؟ Boot Action تغییر کند؟ Reset درخواست شود؟* — این‌ها هیچ‌کدام Infrastructure نیستند.

**به‌روزرسانی (تصمیم صریح کاربر، بعد از این سند):** نام «FOTA» فقط از این سند/مکانیزم انتقال حذف نشد - خودِ Management دهم هم دیگر «FOTA Management» نامیده نمی‌شود، بلکه **`Firmware Update Management`** (فایل `docs/02-management-contracts/firmware-update-management.md`، کد `firmware/include|src/managements/firmware_update_management.{h,c}`, `FirmwareUpdate_Init/Process`). دلیل کاربر: کلاً هیچ‌جای پروژه نباید عبارت «FOTA» به‌کار برود - سناریوها به‌جایش با کاربردشان توصیف می‌شوند (نصب Firmware روی MCU، آپلود Log به سرور، دانلود Dump برای ECU، ...).

```text
Infrastructure                          Management
File Transfer                    Firmware Update Management
   │                                       │
   │ مکانیزم عمومی انتقال                  │ معنا و تصمیم محصولی
   ▼                                       ▼
Download / Upload / Resume         Validation, Version,
   │                                Compatibility, Install
   ▼                                Decision, Boot Action,
Storage (File API)                  Reset Request
```

---

## ۱. هدف

فراهم کردن یک مسیر استاندارد و عمومی برای **انتقال فایل بین Server و File Storage دستگاه** — بدون دانستن معنای فایل. مهم‌ترین مصرف‌کننده فعلی، فرآیند Firmware Update (توسط `Firmware Update Management`) است؛ اما معماری طوری است که Diagnostic Dump، Log Upload یا Configuration Package هم بدون Infrastructure جدید از همین مسیر استفاده کنند.

---

## ۲. جایگاه

```text
                 Server
                   │
           File Transfer Infrastructure
                   │
                   ▼
              File Storage (File API)
                   │
          External NOR / Memory Infrastructure
```

مثال — بدون این‌که File Transfer بداند فایل چیست:

```text
Server → File Transfer → Storage → Firmware Update Management         (فایل Firmware)
Server → File Transfer → Storage → Diagnostic Management    (فایل Diagnostic)
Storage → File Transfer → Server                             (Diagnostic Dump آپلود می‌شود)
```

---

## ۳. اصل معماری

> **File Transfer Infrastructure مسئول انتقال فایل است، نه معنای فایل.**

Firmware بودن فایل → متعلق به Firmware Update Management. Diagnostic Dump بودن → متعلق به Diagnostic Management. Log بودن → متعلق به مالک LOG. Configuration Package بودن → متعلق به Configuration Infrastructure.

---

## ۴. مسئولیت‌ها

**دریافت فایل از Server:** انتقال تدریجی و غیرهمزمان به Storage، بدون اشغال RAM بزرگ.

**ارسال فایل به Server:** خواندن از Storage و ارسال، بدون دانستن معنای فایل.

**انتقال تدریجی (Chunk-based):** حجم فایل نباید وابسته به ظرفیت RAM باشد.

**Resume:** طبق Contract ارتباط Server، ادامه انتقال از نقطه قطع‌شده؛ جزئیات Offset/Session/Protocol در Contract Server تعریف می‌شود.

**کنترل کامل بودن فایل:** `File Identifier, File Size, Received Size, Transfer Status, Integrity Information` — اعتبارسنجی معنایی برعهده Consumer است.

---

## ۵. مالکیت Storage

مالک Storage نیست:

```text
File Transfer Infrastructure → File API → Memory Infrastructure → NOR Flash
```

هرگز مستقیم با SPI، NOR Driver، LittleFS، آدرس فیزیکی، Erase Block یا Page Program کار نمی‌کند.

---

## ۶. ورودی‌ها

Download Request (`File ID, Destination, Expected Size, Transfer Parameters`) · Upload Request (`File ID, Source File`) · Server Data (تدریجی، حین Download) · Transfer Control (`Start, Pause, Resume, Cancel`).

---

## ۷. خروجی‌ها

**Transfer Status:** `IDLE, PENDING, TRANSFERRING, COMPLETED, FAILED, CANCELLED`

**Transfer Progress:** `Total Size, Transferred Size, Remaining Size`

**Transfer Completion Event:** فقط اعلام «انتقال کامل شد»؛ تصمیم بعدی با Consumer/Policy است.

**Transfer Fault:** `Connection Failure, Storage Failure, Timeout, Integrity Failure, Cancelled` — Recovery داخلی فرآیند انتقال با خودش است؛ تصمیمات محصولی (SMS, Call, System Reset) خارج از مسئولیتش.

---

## ۸. File State در مقابل Transfer State

```text
File State:     CREATED, PARTIAL, COMPLETE, INVALID
Transfer State: PENDING, TRANSFERRING, COMPLETED, FAILED, CANCELLED
```

ممکن است `Transfer = COMPLETED` ولی `File = INVALID` باشد (مثلاً فایل کامل دریافت شده ولی Validation مصرف‌کننده آن را رد کرده).

---

## ۹. مرز Validation

مسئول **Integrity انتقال** است، نه اعتبار معنایی فایل:

```text
Download → Transfer Integrity → Complete File → [Firmware Validation | Diagnostic Validation] → Accept/Reject
```

Firmware Header، Diagnostic Format یا سایر ساختارهای کاربردی را نمی‌شناسد، مگر بخشی از Contract عمومی انتقال باشد.

---

## ۱۰. رفتار Non-Blocking

```text
Request → Start → Return → FileTransfer_Process() → Advance Transfer → Return → ... → Complete
```

`Process()` در هر فراخوانی فقط مقدار محدودی کار انجام می‌دهد.

---

## ۱۱. Power Mode

مالک Power Policy نیست؛ رفتار Download در `LOW_POWER` (توقف/ادامه با نرخ پایین‌تر/طبق Policy ارتباط) بخشی از Contract انتقال و Power Policy است، نه تصمیم خودش.

---

## ۱۲. ارتباط با Server Management

```text
File Transfer Infrastructure → Server Management → EC200U Manager → Network → Server
```

Server Management مسئول ارتباط با Backend است؛ File Transfer مسئول معنای Transfer Session. جزئیات Transport (MQTT/HTTP/...) در Contract Server تعیین می‌شود.

---

## ۱۳. ارتباط با Memory Infrastructure

```text
File Transfer Infrastructure → File API → Memory Infrastructure → External NOR
```

نه به Sector/Block/Page/آدرس فیزیکی، نه به LittleFS وابسته است — LittleFS جزئی از Implementation داخلی Memory Infrastructure است.

---

## ۱۴. مثال Firmware Update — اصلاح‌شده با مرز PROGRAM Staging

**نسخه قبلی این بخش** مستقیم می‌گفت `External NOR → Bootloader → Inactive Bank`، که با تفکیک دقیق‌تری که در `nor-flash-infrastructure.md` و نسخه به‌روزشده `bootloader.md` بسته شده، هماهنگ نبود. مسیر صحیح:

```text
Server
   ↓
File Transfer Infrastructure  (Download + Resume + Chunk)
   ↓
Firmware Staging (LittleFS, از طریق Memory Infrastructure File API)
   ↓
Firmware Update Management
   ├── Validate (Format, Size, Integrity, Signature, Compatibility)
   ├── Version / Compatibility Check
   └── نوشتن Raw در PROGRAM Staging (External NOR — بدون LittleFS)
   ↓
Firmware Update Management: Boot Action = PROGRAM → Reset Request → System Management → Reset
   ↓
Bootloader (بعد از Reset)
   ├── Raw Read از PROGRAM Staging (بدون LittleFS)
   ├── Decrypt → Program Inactive Bank (MCU Internal Flash)
   └── CRC → موفق: Active Bank عوض می‌شود
```

**تقسیم مسئولیت در این مسیر:**

| مرحله | مسئول |
|---|---|
| Download تدریجی + Resume | File Transfer Infrastructure |
| ذخیره در Firmware Staging (LittleFS) | Memory Infrastructure (از طریق File Transfer) |
| Validation، Version، تصمیم نصب | Firmware Update Management |
| نوشتن Raw در PROGRAM Staging | Firmware Update Management |
| Decrypt/Program/CRC/Bank Switch | Bootloader |

🔴 **یادداشت باز (بدون تغییر نسبت به `nor-flash-infrastructure.md`):** این‌که آیا واقعاً به دو کپی (Firmware Staging + PROGRAM Staging) نیاز است یا Firmware Update Management می‌تواند مستقیم از فایل بخواند و بدون کپی واسط Program کند، هنوز باز است و به ظرفیت NOR گره خورده.

---

## ۱۵. مثال Diagnostic Dump (آینده)

```text
ECU → Diagnostic Management → Storage → File → File Transfer Infrastructure → Server
```

File Transfer هیچ تفاوت معماری با Firmware نمی‌بیند — فقط یک فایل دیگر است.

---

## ۱۶. مثال Download فایل برای ECU (آینده)

```text
Server → File Transfer Infrastructure → Storage → Diagnostic Management → ECU
```

File Transfer مسئول دریافت و ذخیره؛ Diagnostic Management مسئول تشخیص قالب و کاربرد فایل.

---

## ۱۷. Contract سطح معماری

```text
File Transfer Infrastructure

Responsibility:
    File Download, File Upload, Chunked Transfer,
    Transfer Progress, Resume, Transfer Status,
    Transfer Completion, Transfer Fault

Inputs:
    Download Request, Upload Request, Server Data,
    Transfer Control, Power Mode

Outputs:
    Transfer Status, Transfer Progress,
    Transfer Completion Event, Transfer Fault

Constraints:
    Non-Blocking · Super Loop Compatible
    No direct NOR/SPI access · No LittleFS dependency
    No Product Meaning ownership · No Firmware Programming
    No Diagnostic Execution · No direct Management-to-Management control
```

---

## ۱۸. مرز با سایر اجزا

| جزء | مسئولیت |
|---|---|
| File Transfer Infrastructure | انتقال فایل بین Server و Storage (مکانیزم عمومی) |
| **Firmware Update Management** | معنای Firmware Update: Validation، Version، Install Decision، Boot Action، Reset Request |
| Server Management | ارتباط با Backend و Transport |
| Memory Infrastructure | نگهداری فایل (LittleFS/File API) |
| Bootloader | Raw Read از PROGRAM Staging، Decrypt، Programming، Activation |
| Diagnostic Management | معنای Diagnostic File و استفاده از آن |
| Log Owner (🔴 هنوز باز) | معنای Log و Policy نگهداری |
| EC200U Manager | Cellular/Data ارتباطی |
| Event Policy | تصمیم‌گیری بر اساس Event |
| Power Management | تعیین Power Mode |

---

## ۱۹. اصل نهایی

> **File Transfer Infrastructure مکانیزم انتقال را فراهم می‌کند؛ Management مصرف‌کننده (مثل Firmware Update Management) مالک معنا و تصمیم است؛ Memory Infrastructure مالک روش نگهداری فایل است.**

```text
             Infrastructure                    Management
             File Transfer              Firmware Update Management
             /           \                          │
        Download          Upload            Validation, Version,
           ↓                 ↑              Install Decision,
        Storage           Storage           Boot Action, Reset
```

در آینده همین Infrastructure می‌تواند برای Diagnostic Dump، Log Upload، Configuration Package یا سایر فایل‌ها بدون ایجاد هیچ Infrastructure یا Management جدید استفاده شود — فقط یک Management مصرف‌کننده جدید (یا یکی از Managementهای موجود) مالک معنای آن فایل می‌شود.
