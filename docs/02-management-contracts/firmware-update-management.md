# پیوست فنی — تعریف مسئولیت، ورودی و خروجی Firmware Update Management

> **یادداشت (بعد از اضافه‌شدن `docs/03-infrastructure-contracts/file-transfer-infrastructure.md`):** این سند عمداً بازنویسی نشده - طبق تصریح همان سند جدید، «Firmware Update Management کاملاً دست‌نخورده باقی می‌ماند». آنچه عوض شده این است: مکانیزم عمومی Download/Upload/Resume/Chunk که بخش‌های ۷ و ۸ همین سند توصیف می‌کنند، اکنون یک Infrastructure جداگانه (File Transfer) است، نه پیاده‌سازی داخلی Firmware Update. تقسیم دقیق: **File Transfer Infrastructure** فقط Byte جابه‌جا می‌کند (نمی‌داند فایل چیست)؛ **Firmware Update Management** (همین سند) همچنان مالک Validation، Version، Compatibility، Install Decision، Boot Action، و Reset Request است - دقیقاً همان چیزی که در بخش‌های ۶، ۱۰، ۱۱، ۱۴ پایین آمده. جزئیات کامل مسیر جدید (شامل مرز `PROGRAM Staging` با Bootloader) در `file-transfer-infrastructure.md` بخش ۱۴ است.

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **Firmware Update Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

هدف، مدیریت چرخه به‌روزرسانی Firmware از دریافت بسته تا اعتبارسنجی، نصب و راه‌اندازی نسخه جدید، بدون وابستگی سایر Managementها به جزئیات Storage، ارتباط شبکه و Bootloader است.

---

# ۲. جایگاه Firmware Update Management

Firmware Update Management مالک Capability به‌روزرسانی Firmware است.

```text
Server
   ↓
Server Management
   ↓
Firmware Update Request
   ↓
Firmware Update Management
   ├── Download
   ├── Validate
   ├── Store
   └── Install Request
          ↓
      Bootloader
          ↓
      New Firmware
```

---

# ۳. مسئولیت‌ها

Firmware Update Management مسئول موارد زیر است:

```text
Firmware Update Request Management
Firmware Package Download
Package Validation
Version Validation
Update Storage Management
Update Progress Management
Update Installation
Update Failure Detection
Firmware Update Fault Management
```

جزئیات Boot، انتخاب Bank و اجرای نهایی Firmware در اختیار Bootloader است.

---

# ۴. دریافت درخواست Firmware Update

درخواست Firmware Update معمولاً از Server دریافت می‌شود:

```text
Server
  ↓
Server Management
  ↓
Firmware Update Request
  ↓
Firmware Update Management
```

Firmware Update Management درخواست را بررسی می‌کند و در صورت معتبر بودن، فرآیند Update را آغاز می‌کند.

---

# ۵. Firmware Package

بسته Firmware باید حداقل اطلاعات لازم برای اعتبارسنجی Update را داشته باشد.

نمونه مفهومی:

```text
Firmware Package
├── Version
├── Size
├── Image
└── Integrity / Signature Data
```

جزئیات Format بسته در Contract اختصاصی Firmware Update تعریف می‌شود.

---

# ۶. اعتبارسنجی

قبل از نصب Firmware، Firmware Update Management باید بسته را اعتبارسنجی کند.

نمونه کنترل‌ها:

```text
Package Format
Version
Size
Integrity
Signature
Compatibility
```

اصل مهم:

> **Firmware نامعتبر نباید برای اجرا در اختیار Bootloader قرار گیرد.**

---

# ۷. دریافت Firmware

دریافت Firmware نباید Blocking باشد.

مدل صحیح:

```text
Process()
   ↓
Request Download
   ↓
Receive Chunk
   ↓
Store Chunk
   ↓
Return
```

در فراخوانی‌های بعدی:

```text
Process()
   ↓
Receive / Store Next Chunk
```

بنابراین دانلود یک Firmware بزرگ نباید Super Loop را متوقف کند.

---

# ۸. Storage

Firmware Update Management از Storage Infrastructure برای نگهداری Firmware استفاده می‌کند.

```text
Server
  ↓
Firmware Update Management
  ↓
External NOR / Update Storage
```

Firmware Update Management مالک Driver یا جزئیات فیزیکی NOR نیست.

در صورت نیاز، Storage Infrastructure سرویس لازم را ارائه می‌کند.

---

# ۹. Resume

در صورت قطع ارتباط در هنگام Download، Firmware Update Management باید در صورت پشتیبانی Product بتواند Download را از نقطه مناسب ادامه دهد.

```text
Download
   ↓
Connection Lost
   ↓
Save Progress
   ↓
Reconnect
   ↓
Resume
```

جزئیات Resume بخشی از Implementation داخلی Firmware Update Management است.

---

# ۱۰. Installation

پس از دریافت کامل و اعتبارسنجی Firmware:

```text
Firmware Downloaded
       ↓
Validation
       ↓
Ready for Installation
       ↓
Installation Request
       ↓
Bootloader
```

Firmware Update Management نباید مستقیماً منطق Bootloader را اجرا کند.

---

# ۱۱. Reset

اجرای Firmware جدید معمولاً نیازمند Reset است.

مسیر صحیح:

```text
Firmware Update Management
      ↓
Reset Request
      ↓
System Management
      ↓
MCU Reset
      ↓
Bootloader
```

Firmware Update Management مستقیماً MCU را Reset نمی‌کند.

---

# ۱۲. نقش Bootloader

پس از Reset:

```text
Reset
 ↓
Bootloader
 ↓
Validate Firmware
 ↓
Select Valid Image
 ↓
Application
```

Bootloader مسئول تصمیم نهایی درباره Firmware قابل اجرا است.

در صورت شکست Installation یا Validation، Bootloader باید از Firmware معتبر قبلی استفاده کند؛ در معماری Dual-Bank این موضوع باید با ساختار Bankها پشتیبانی شود.

---

# ۱۳. Update Failure

اگر Download یا Validation شکست بخورد:

```text
Firmware Update Failure
     ↓
Firmware Update Management
     ↓
Retry / Abort
```

در صورت شکست Installation:

```text
Installation Failure
       ↓
Reset
       ↓
Bootloader
       ↓
Previous Valid Firmware
```

Firmware فعلی نباید در اثر یک Update ناموفق از بین برود.

---

# ۱۴. Version Management

Firmware Update Management باید Version Firmware را مدیریت و بررسی کند.

نمونه:

```text
Current Version
       ↓
Target Version
       ↓
Compatibility / Update Policy
```

به‌روزرسانی نسخه باید مطابق Policy سیستم انجام شود.

از نصب نسخه نامعتبر، ناسازگار یا ممنوع باید جلوگیری شود.

---

# ۱۵. Firmware Update State

Firmware Update Management وضعیت جاری Update را نگهداری می‌کند.

نمونه مفهومی:

```text
IDLE
REQUESTED
DOWNLOADING
VALIDATING
READY
INSTALLING
FAILED
COMPLETED
```

این Stateها نمونه هستند و FSM داخلی Firmware Update را الزام نمی‌کنند.

---

# ۱۶. Fault و Recovery

Firmware Update Management مسئول Faultهای مربوط به Update است.

نمونه:

```text
Download Failure
Storage Failure
Invalid Package
Integrity Failure
Signature Failure
Version Incompatibility
Installation Failure
```

Recovery ابتدا در داخل Firmware Update Management انجام می‌شود.

در صورت نیاز:

```text
Firmware Update Management
      ↓
Reset Request
      ↓
System Management
```

---

# ۱۷. ارتباط با Server Management

Firmware Update Management مسئول ارتباط مستقیم با Server نیست.

```text
Server
  ↓
Server Management
  ↓
Firmware Update Request
  ↓
Firmware Update Management
```

برای Download نیز:

```text
Firmware Update Management
      ↓
Server Communication Request
      ↓
Server Management / EC200U
```

بنابراین Firmware Update نباید مستقیماً Cellular یا Socket را کنترل کند.

---

# ۱۸. Power Mode

Firmware Update یک عملیات پرمصرف و حساس است.

در `LOW_POWER`، Firmware Update Management نباید بدون بررسی شرایط سیستم Update را به‌صورت عادی اجرا کند.

رفتار پیشنهادی:

```text
Firmware Update Request
     ↓
Check Power / Preconditions
     ↓
Start or Defer
```

در صورت شروع Update، Firmware Update Management باید شرایط لازم برای حفظ پایداری فرآیند را مدیریت کند.

Power Management فقط Mode را اعلام می‌کند.

---

# ۱۹. Contract سطح معماری

```text
Firmware Update Management

Responsibility:
    Firmware Update Request Management
    Firmware Download
    Package Validation
    Version Validation
    Update Storage
    Update Progress
    Installation
    Firmware Update Fault Detection
    Local Recovery

Inputs:
    Firmware Update Request
    Firmware Data
    System Time
    Power Mode
    Current Firmware Information

Outputs:
    Firmware Update Status
    Update Progress
    Firmware Update Fault
    Reset Request
    Bootloader Installation Request

Dependencies:
    Server Management
    Memory / Storage Infrastructure
    System Management
    Bootloader
    Time Base

Constraints:
    Non-Blocking
    Super Loop Compatible
    Stateful
    No Direct Cellular Access
    No Direct Hardware Storage Access
    No Direct MCU Reset
    Firmware Must Be Validated Before Installation
```

---

# ۲۰. مرز با سایر Managementها

| جزء               | رابطه با Firmware Update Management           |
| ----------------- | ---------------------------------- |
| Server Management | دریافت/انتقال داده و Firmware Update Request  |
| Memory / Storage  | ذخیره Firmware                     |
| System Management | اجرای Reset Request                |
| Bootloader        | اعتبارسنجی نهایی و انتخاب Firmware |
| Power Management  | اعلام Power Mode                   |
| EC200U Manager    | ارائه Cellular Data Service        |
| سایر Managementها | مستقل                              |

---

# ۲۱. اصل نهایی

Firmware Update Management مالک **فرآیند Update** است:

```text
Request
   ↓
Download
   ↓
Validate
   ↓
Store
   ↓
Install Request
   ↓
Reset
   ↓
Bootloader
```

اما:

> **Firmware Update Management مسئول دریافت، اعتبارسنجی و آماده‌سازی Firmware است؛ Bootloader مالک اجرای Firmware پس از Reset است؛ System Management مالک اجرای Reset است؛ و Server Management مالک ارتباط شبکه‌ای است.**

اصل کلیدی:

> **هیچ Firmware جدیدی نباید بدون اعتبارسنجی کامل در معرض اجرا قرار گیرد و شکست Update نباید Firmware معتبر فعلی را از بین ببرد.**
