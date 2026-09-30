# پیوست فنی — زیرساخت حافظه خارجی NOR Flash

## 0. یادداشت مهم — سه حافظه فیزیکی کاملاً مجزا

| حافظه فیزیکی | محتوا | مدل دسترسی | چه کسی می‌شناسدش |
|---|---|---|---|
| **MCU Internal Flash** | Bank 1 / Bank 2 (خود Firmware قابل‌اجرا) | Raw — Driver اختصاصی Internal Flash | فقط Bootloader |
| **EEPROM** (واقعی یا Emulated) | `STORAGE_RECOVERY`, `STORAGE_CONFIG` | Raw Object (`storage.h` فعلی) | Bootloader + Configuration Infrastructure |
| **External NOR — W25Q128 (SPI, 16MB)** | بخش File (Firmware Staging/DIAG/LOG) + بخش Raw (PROGRAM Staging) | ترکیبی — بخش ۴ | Memory Infrastructure (File) + Bootloader (فقط Raw Read) |

**Bootloader هیچ‌وقت LittleFS را نمی‌شناسد یا Mount نمی‌کند.**

---

## 1. هدف

فراهم کردن یک لایه پایدار، غیرهمزمان و قابل تست برای NOR (W25Q128)، بدون درگیر کردن Managementها با SPI، Register، Page Program، Sector Erase، آدرس فیزیکی یا LittleFS.

---

## 2. سخت‌افزار هدف

```text
Manufacturer: Winbond
Device:       W25Q128
Capacity:     128 Mbit ≈ 16 MB
Interface:    SPI
```

---

## 3. اصل معماری

> **Managementها مالک معنای داده هستند؛ Memory Infrastructure مالک روش نگهداری داده است.**

---

## 4. مدل ترکیبی NOR — اصلاح‌شده

External NOR دیگر «فقط File Storage» نیست؛ به دو بخش تقسیم می‌شود:

```text
External NOR (W25Q128)
│
├── File Storage (LittleFS)
│   ├── Firmware Staging   ← دانلود تدریجی/Resume از سرور
│   ├── DIAG
│   └── LOG
│
└── Raw Storage
    └── PROGRAM Staging  ← نسخه نهایی Firmware، فقط برای Bootloader
```

## 4-1. ⚠️ ظرفیت — با تخمین بخش ۴-۱-۱ دیگر بلاک‌کننده نیست، اما همچنان Placeholder

محاسبه اولیه نشان می‌دهد نقشه فعلی احتمالاً **در ۱۶ مگابایت جا نمی‌شود**:

```text
Firmware Staging ≈ 5MB + DIAG ≈ 6MB + LOG ≈ 2-3MB = 14MB (خوش‌بینانه)

باقیمانده برای PROGRAM Staging + Reserved ≈ 2MB
```

اگر Firmware Image واقعی بزرگ‌تر از ~۲ مگابایت باشد، این نقشه عملاً امکان‌پذیر نیست.

**این عدد نباید با فرض حل شود.** قبل از قطعی‌شدن Partition Map باید مشخص شود:
- اندازه واقعی/تخمینی Firmware Image (بر اساس حجم کد فعلی + حاشیه رشد)
- آیا Firmware Staging و PROGRAM Staging واقعاً باید هم‌زمان دو کپی کامل نگه دارند (بخش ۴-۲)
- در صورت عدم جا شدن: کوچک کردن DIAG/LOG، یا تجدیدنظر در انتخاب تراشه NOR بزرگ‌تر

### ۴-۱-۱. تخمین تکمیلی — ⚠️ Placeholder، نه تصمیم نهایی (باید با داده واقعی تغییر کند)

فرض «~۲ مگابایت» بالا با سخت‌افزار واقعی همین پروژه جور در نمی‌آید: بورد انتخاب‌شده (`MCU_F4/`, STM32F407VGTx) فقط **۱۰۲۴ کیلوبایت Flash داخلی** دارد (`MCU_F4/STM32F407VGTX_FLASH.ld`) — همان جایی که Bootloader + هر دو Bank (Dual-Bank) + Metadata باید جا شوند. یعنی یک Image واقعی هرگز نمی‌تواند به ۲MB نزدیک شود؛ سقف واقعی خیلی کمتر است. تخمین زیر بر همین مبنا (نقشه Sector واقعی STM32F407: ۴×۱۶KB + ۱×۶۴KB + ۷×۱۲۸KB) محاسبه شده:

**Flash داخلی (۱۰۲۴KB جمع):**

| ناحیه | اندازه | Sectorها |
|---|---:|---|
| Bootloader | ۶۴KB | 0-3 |
| Bank A | ۳۸۴KB | ۳ سکتور ۱۲۸KB |
| Bank B | ۳۸۴KB | ۳ سکتور ۱۲۸KB |
| Reserved (`STORAGE_CONFIG` EEPROM Emulation + `STORAGE_RECOVERY` + حاشیه) | ۱۹۲KB | سکتور ۴ (۶۴KB) + ۱ سکتور ۱۲۸KB باقیمانده |

→ **سقف تخمینی هر Image: ~۳۰۰KB واقعی + ~۸۴KB حاشیه رشد داخل هر Bank ۳۸۴KB.** این تخمین `bootloader/include/boot_config.h`'s `BOOT_BANK_SIZE_BYTES` را هم به‌روز کرد (از ۲۵۶KB دلبخواه قبلی - که حتی از همین ~۳۰۰KB تخمینی هم کوچک‌تر بود - به ۳۸۴KB).

**NOR خارجی (۱۶MB جمع) — با Image ~۳۰۰KB، نه ~۲MB:**

| Partition | اندازه تخمینی | دلیل |
|---|---:|---|
| Firmware Staging (LittleFS) | ۱MB | دانلود تدریجی + جای یک نسخه پشتیبان قبلی |
| PROGRAM Staging (Raw) | ۵۱۲KB | کمی بیشتر از یک Image کامل |
| DIAG | ۴MB | همان تخمین اولیه `nor-storage-outsourcing-spec.md` (بدون داده بهتر) |
| LOG | ۲MB | ده‌ها هزار `LogRecord` (~۴۸ بایت هرکدام) |
| GPS Partition | **۰ (حذف‌شده در این تخمین)** | هیچ Contract فعلی (`gps-management.md`) نیاز به تاریخچه GPS روی NOR ندارد - موقعیت فقط زنده از طریق Telemetry می‌رود. اگر تاریخچه محلی GPS واقعاً لازم است، این باید صریحاً از کاربر پرسیده شود، نه اینجا حذف بماند. |
| Reserved/حاشیه | **~۸.۵MB** | باقیمانده |

**نتیجه:** با این تخمین، بلاک‌کننده ظرفیت بالا در واقع **رفع می‌شود** — فرض غلط ~۲MB بود که همه‌چیز را تنگ می‌کرد؛ با سقف واقعی سخت‌افزار (~۳۰۰-۴۰۰KB)، ۱۶MB بسیار راحت جواب می‌دهد.

**⚠️ همه این اعداد باید تغییر کنند وقتی:**
1. اندازه واقعی Firmware (بعد از نوشتن کد واقعی همه Managementها) اندازه‌گیری شود - نه فقط تخمین زده.
2. کاربر صریحاً بگوید GPS Partition لازم است یا نه.
3. حجم واقعی DIAG/LOG بر اساس نرخ تولید واقعی بازبینی شود.
4. تصمیم بخش ۴-۲ (یک‌کپی/دوکپی) گرفته شود - روی اندازه Firmware Staging/PROGRAM Staging اثر مستقیم دارد.

## 4-2. 🔴 آیا دو کپی همزمان واقعاً لازم است؟ — باز

مدل فعلی (Firmware Update Management فایل را از LittleFS می‌خواند و یک کپی Raw در PROGRAM Staging می‌نویسد) **یک گزینه است، نه یک تصمیم قطعی**. جایگزین محتمل: Firmware Update Management بعد از Validation، مستقیماً از فایل LittleFS بخواند و بایت‌به‌بایت به یک Programming Interface تحویل دهد، بدون کپی خام واسط — که مصرف NOR را تقریباً نصف می‌کند.

این تصمیم به Contract مشترک Firmware Update Management/Bootloader واگذار می‌شود و در این سند فرض نمی‌شود.

## 4-3. مرز PROGRAM Staging (محدود به همین سند)

```text
PROGRAM Staging
    Writer: Firmware Update Management (سمت Application)
    Reader: Bootloader
    Access: Raw (بدون LittleFS)
```

جزئیات Decrypt/Program/Verify/CRC و این‌که آیا اصلاً به این ناحیه واسط نیاز است یا نه، در Contract Bootloader/Firmware Update تعیین می‌شود، نه اینجا.

---

## 5. مرز مسئولیت Partition

Memory Infrastructure معنای محتوای فایل را نمی‌داند. ساختار فایل، Lifecycle و Policy نگهداری خارج از مسئولیت این زیرساخت است.

---

## 6. File Contract (فقط Firmware Staging/DIAG/LOG)

```text
Open, Create, Read, Write, Append, Close, Delete, Rename, Get Size, Get Status
```

مستقل از LittleFS؛ Consumer با مفاهیم منطقی (`Partition, File, Offset, Length`) کار می‌کند، نه آدرس فیزیکی.

---

## 7. رفتار Non-Blocking

```text
Request → Start Operation → Return → Storage_Process() → Advance → Check Status → Complete
```

وضعیت حداقل: `IDLE, BUSY, SUCCESS, ERROR`.

---

## 8. خطاها

```text
NOT_READY, BUSY, INVALID, NOT_FOUND, FULL, NO_SPACE,
READ_ERROR, WRITE_ERROR, ERASE_ERROR, CORRUPTED, TIMEOUT, FILESYSTEM_ERROR
```

بدون تصمیم محصولی خودکار (Reset/SMS/Call/Retry Policy) بر اساس این خطاها.

---

## 9. Power Loss

قطع توان یک حالت واقعی و مورد انتظار است. سناریوهای حداقلی: Write/Erase/Create/Update/Delete + Power Loss، و Mount بعد از Power Loss — شامل PROGRAM Staging هم می‌شود (Integrity آن با CRC خودش تشخیص داده می‌شود، نه با LittleFS).

---

## 10. Wear و Integrity — اصلاح‌شده

**نسخه قبلی:** «Wear Leveling به LittleFS سپرده می‌شود.» — این جمله بیش از حد قطعی بود.

**نسخه اصلاح‌شده:**
> برای Partitionهای مبتنی بر LittleFS، توزیع Write/Erase و مدیریت Wear مطابق قابلیت‌ها و Configuration انتخاب‌شده LittleFS انجام می‌شود؛ این رفتار باید بعد از انتخاب Configuration نهایی، روی W25Q128 واقعی اعتبارسنجی شود، نه صرفاً فرض شود.

Integrity مکانیزمی (آیا Write موفق بود؟) مسئولیت این زیرساخت است؛ Integrity معنایی (آیا Firmware Image معتبر است؟) مسئولیت Consumer است.

---

## 11. رفتار در LOW_POWER

Memory Infrastructure مالک Power Policy نیست؛ فقط Power Mode را مشاهده می‌کند.

---

## 12. Testability

Fake NOR، Mock NOR Driver، Fake Filesystem/Storage Backend برای تست بدون Hardware. بعد از آماده شدن سخت‌افزار: تست‌های Init، Read/Program/Erase، Format/Mount/CRUD فایل، Isolation/Capacity/Boundary Partition، Power Loss/Recovery، Timing روی W25Q128 واقعی.

---

## 13. محدودیت‌های اجرایی

Non-Blocking · سازگار با Super Loop · بدون Dynamic Allocation در مسیر عادی · بدون ISR برای Write/Erase · بدون System Reset مستقیم · بدون منطق محصولی Consumerها · LittleFS و جزئیات SPI/NOR از Consumer پنهان · قابل Fake/Mock.

---

## 14. مسئولیت‌ها

| جزء | مسئولیت |
|---|---|
| Firmware Update Management | معنای Firmware Package، Policy دانلود، نوشتن در PROGRAM Staging (جزئیات در Contract مشترک با Bootloader) |
| DIAG Management | معنای فایل Diagnostic (Contract جداگانه — هنوز نوشته نشده) |
| LOG (مالک 🔴 باز) | معنای Log و Policy نگهداری |
| Bootloader | Raw Read از PROGRAM Staging (جزئیات Program/Verify در `bootloader.md`) |
| Memory Infrastructure | Filesystem، Partition، File API |
| LittleFS | Filesystem (فقط Firmware Staging/DIAG/LOG) |
| NOR Driver | ارتباط فیزیکی با W25Q128 |

---

## 15. اصل نهایی

> **W25Q128 محل نگهداری فایل Firmware است؛ Bank (روی MCU Internal Flash) محل اجرای Firmware است.**

> **Bootloader هرگز LittleFS را نمی‌شناسد — حتی برای PROGRAM Staging، فقط یک آدرس خام و ثابت می‌خواند.**

> **جزئیات نحوه انتقال از Firmware Staging به PROGRAM Staging (و این‌که آیا اصلاً دو کپی لازم است) در این سند تصمیم گرفته نمی‌شود.**

---

## 16. موارد باز برای OPEN-ITEMS.md — به‌روزشده

| # | مورد | اولویت |
|---|---|---|
| 1 | 🔴 **بلاک‌کننده:** ظرفیت ۱۶MB با Partition Map فعلی احتمالاً کافی نیست؛ نیاز به اندازه واقعی Firmware Image | بالا |
| 2 | آیا Firmware Staging و PROGRAM Staging باید هم‌زمان دو کپی کامل باشند، یا مسیر Direct Read/Program جایگزین شود | بالا (مستقیماً روی #۱ اثر دارد) |
| 3 | مالک معنایی `STORAGE_LOG` | متوسط |
| 4 | Contract مستقل DIAG Management برای فایل‌های Diagnostic | متوسط |
| 5 | Contract مستقل LOG | متوسط |
| 6 | Configuration نهایی LittleFS (Block Size, Cache, Lookahead) + اعتبارسنجی Wear روی سخت‌افزار واقعی | پایین (بعد از سخت‌افزار) |
