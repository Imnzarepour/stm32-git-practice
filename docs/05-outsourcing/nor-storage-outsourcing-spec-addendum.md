# الحاقیه — هماهنگی `nor-storage-outsourcing-spec.md` با تصمیمات بعدی معماری

## ۰. جایگاه این سند

این یک **الحاقیه (Addendum)** است، نه بازنویسی. `nor-storage-outsourcing-spec.md` اصلی **دست‌نخورده** می‌ماند — چون احتمالاً همین الان دست پیمانکار NOR/Memory است و ویرایش مستقیمش بدون هماهنگی ریسک واقعی دارد (نسخه‌های ناهماهنگ دست دو طرف). این سند فقط نقشه راهی است برای این‌که **قبل از فینال‌کردن هر قرارداد یا کدنویسی با پیمانکار**، این چند نکته دیده و هماهنگ شوند.

سند اصلی در `docs/03-infrastructure-contracts/nor-flash-infrastructure.md` (بخش ۴-۱) هم دقیقاً به همین مسئله اشاره کرده بود؛ این الحاقیه همان یادآوری را با جزئیات دقیق‌تر (شماره خط، بخش) مستند می‌کند.

---

## ۱. تغییر نام‌گذاری: FOTA → Firmware Update

طبق تصمیم صریح کاربر (بعد از نوشتن سند اصلی)، عبارت «FOTA» دیگر در هیچ کجای پروژه استفاده نمی‌شود — نه برای نام Management (اکنون **Firmware Update Management**)، نه برای مکانیزم انتقال (اکنون **File Transfer Infrastructure**، بخش ۲ را ببینید).

سند اصلی (`nor-storage-outsourcing-spec.md`) **۲۶ بار** «FOTA» دارد، از جمله:

| محل در سند اصلی | باید این‌طور خوانده/گفته شود |
|---|---|
| بخش ۶ «FOTA Data»، بخش ۲۷ «FOTA Storage Boundary» | «Firmware Update Data» / «Firmware Update Storage Boundary» |
| «FOTA Management» (خط ۲۰۶، ۸۳۹، ۱۲۱۱) | «Firmware Update Management» |
| «FOTA Partition» / «FOTA Storage» (خط ۲۴۱، ۱۹۸، ۳۴۲) | نام Partition می‌تواند همان بماند (یک برچسب فیزیکی است، نه شناسه کد) اما در مکالمه با پیمانکار باید گفته شود این Partition برای **Firmware Image** است، نه یک ماژول به اسم «FOTA» |
| «D9 — FOTA Storage» (خط ۱۲۹۶، در فهرست نکات بحث با پیمانکار) | همان بحث باقی است، فقط عنوانش باید «D9 — Firmware Update Storage» خوانده شود |

**نکته عملی:** لازم نیست الان خودِ فایل اصلی را Search-Replace کرد؛ اما هر مکاتبه/جلسه جدید با پیمانکار باید از نام جدید استفاده کند، و اگر این سند روزی رسماً بازنویسی شد، این جدول همان چک‌لیست است.

---

## ۲. مفاهیمی که بعد از نوشتن سند اصلی اضافه شدند

سند اصلی (تاریخ نگارش، قبل از جلسات اخیر) این سه مفهوم را نمی‌شناسد چون بعداً به معماری اضافه شدند:

### ۲-۱. File Transfer Infrastructure
مکانیزم عمومی Download/Upload/Resume/Chunk که قبلاً بخشی از «FOTA» فرض می‌شد، حالا یک Infrastructure جدا و عمومی است (`docs/03-infrastructure-contracts/file-transfer-infrastructure.md`، پیاده‌سازی واقعی در `firmware/include|src/infrastructure/file_transfer.h|.c`). **اثر روی این سند:** بخش‌های ۶ («FOTA Data») و ۲۷ («FOTA Storage Boundary») سند اصلی که مسئولیت Download/Chunk را به «FOTA Management» نسبت می‌دهند، اکنون دقیق‌تر این‌طور تقسیم می‌شوند:
- **File Transfer Infrastructure:** مکانیزم عمومی Download/Chunk/Resume (نمی‌داند فایل چیست).
- **Firmware Update Management:** فقط Validate/Version/Install Decision/Boot Action/Reset Request.
- **NOR Component (کار پیمانکار):** صرفاً یک File API زیر هر دو - همچنان معنای فایل را نمی‌داند.

### ۲-۲. Log Infrastructure
سند اصلی (بخش ۹ Discussion Point D7 و مثال‌های LOG Partition) به یک زیرساخت Log مستقل اشاره می‌کند. این حالا رسماً بسته و پیاده‌سازی شده: `docs/03-infrastructure-contracts/log-infrastructure.md` + کد واقعی `firmware/include|src/infrastructure/log.h|.c` (یک صف Log مستقل + Append). **اثر روی این سند:** بخش‌های مربوط به LOG Partition سند اصلی (خط ۲۴۳، ۲۶۸، ۳۳۷ و ...) با این طراحی هماهنگ‌اند، فقط باید بدانند سمت Firmware از قبل یک Log Record Contract دارد (`Source, Type, Timestamp, Payload`) که باید نهایتاً از طریق File API همین پیمانکار Append شود.

### ۲-۳. PROGRAM Staging (نام‌گذاری دقیق‌تر Raw Partition)
سند اصلی صرفاً «FOTA Partition» را یک ناحیه می‌داند. سند به‌روزشده (`nor-flash-infrastructure.md` بخش ۴) این ناحیه را به دو زیر-بخش تفکیک کرده:
- **Firmware Staging** (LittleFS-based، دانلود تدریجی) — همان چیزی که سند اصلی توصیف کرده.
- **PROGRAM Staging** (Raw، بدون LittleFS، فقط Bootloader می‌خواند) — یک ناحیه **جداگانه** که سند اصلی اصلاً از هم تفکیکش نکرده بود.

این دقیقاً همان چیزی است که بند ۳ پایین (ظرفیت) و بند D9 سند اصلی را مستقیماً تحت تأثیر قرار می‌دهد — چون یعنی به‌جای یک Partition، شاید دو ناحیه (یکی LittleFS، یکی Raw) لازم باشد.

---

## ۳. یک ناهماهنگی عددی که باید قبل از مذاکره با پیمانکار حل شود

سند اصلی (بخش ۹، خط ۲۶۳-۲۷۲) یک نمونه تقسیم **صرفاً برای بحث** آورده:

```text
FOTA       8 MB
GPS        2 MB
LOG        2 MB
DIAG       4 MB
----------------
TOTAL     16 MB
```

اما `nor-flash-infrastructure.md` (بخش ۴-۱، نوشته‌شده بعداً) یک تخمین دیگر می‌دهد:

```text
Firmware Staging ≈ 5MB + DIAG ≈ 6MB + LOG ≈ 2-3MB ≈ 14MB (خوش‌بینانه)
باقیمانده برای PROGRAM Staging + Reserved ≈ 2MB
```

این دو نمونه با هم همخوانی ندارند (DIAG از ۴MB به ~۶MB رسیده؛ **Partition جدید GPS در نسخه دوم اصلاً وجود ندارد**؛ PROGRAM Staging به‌عنوان یک نیاز جدا اصلاً در نسخه اول دیده نشده بود).

### تخمین تکمیلی (`nor-flash-infrastructure.md` بخش ۴-۱-۱) — پاسخ موقت به هر سه سؤال

فرض ~۲MB برای Firmware Image (که نسخه دوم بالا را تنگ کرده بود) با سخت‌افزار واقعی این پروژه (STM32F407VGTx، فقط ۱۰۲۴KB Flash **داخلی**) اصلاً جور نبود - سقف واقعی هر Image حدود **~۳۰۰KB** است (محاسبه دقیق: بخش ۴-۱-۱ سند). با این عدد واقعی، نسخه تخمینی جدید این‌طور شد:

```text
Firmware Staging  1MB      (دانلود تدریجی + یک نسخه پشتیبان)
PROGRAM Staging   512KB    (کمی بیشتر از یک Image)
DIAG              4MB      (همان تخمین نسخه اول، بدون داده بهتر)
LOG               2MB      (ده‌ها هزار LogRecord)
GPS Partition     0        (حذف - نگاه کنید پایین)
Reserved          ~8.5MB
----------------------------
TOTAL             16MB
```

**پاسخ سه سؤال بالا (هر سه به‌صورت تخمین/Placeholder، نه تصمیم نهایی):**
1. **GPS Partition:** تخمین = لازم نیست. هیچ Contract فعلی (`gps-management.md`) تاریخچه محلی GPS نمی‌خواهد؛ موقعیت فقط زنده از طریق Telemetry می‌رود. **اگر تاریخچه محلی واقعاً لازم است، این باید صریحاً از کاربر پرسیده شود** - این‌جا فقط یک تخمین ثبت شده، نه تصمیم قطعی.
2. **اندازه Firmware Image:** تخمین ~۳۰۰KB (نه ۸MB نسخه اول، نه ۲MB فرض نسخه دوم) - بر مبنای ظرفیت واقعی Flash داخلی، نه اندازه‌گیری کد واقعی (که هنوز نوشته نشده).
3. **DIAG:** تخمین همان ۴MB نسخه اول ماند (داده بهتری برای تغییرش نبود).

**⚠️ هر سه عدد باید عوض شوند وقتی:** اندازه واقعی Firmware بعد از نوشتن کد اندازه‌گیری شود، کاربر صریحاً درباره GPS Partition تصمیم بگیرد، و نرخ واقعی تولید DIAG/LOG معلوم شود. جزئیات کامل محاسبه در `nor-flash-infrastructure.md` بخش ۴-۱-۱ و `bootloader/include/boot_config.h` (`BOOT_BANK_SIZE_BYTES` همین تخمین را می‌سازد، ۳۸۴KB به‌جای ۲۵۶KB قبلی).

این‌ها همان بندهای ⚠️ #۷ (دیگر بلاک‌کننده رسمی نیست، فقط Placeholder) و #۹ در `OPEN-ITEMS.md` هستند.

---

## ۴. جمع‌بندی برای هر کسی که این سند را قبل از مذاکره/تحویل به پیمانکار می‌خواند

- تخمین بند ۳ (`Firmware Staging 1MB + PROGRAM Staging 512KB + DIAG 4MB + LOG 2MB + Reserved 8.5MB`) را می‌توانید به‌عنوان **نقطه شروع مذاکره** به پیمانکار بدهید - اما صریحاً بگویید Placeholder است و با داده واقعی (بند ۳) عوض می‌شود، نه یک Requirement قطعی.
- در مکاتبات جدید، «FOTA» را با «Firmware Update» جایگزین کنید (بند ۱).
- GPS Partition در این تخمین **حذف شده** (نه چون تصمیم قطعی کاربر است، بلکه چون هیچ Contract فعلی آن را ایجاب نمی‌کند) - قبل از تحویل نهایی به پیمانکار، این را صریحاً از کاربر بپرسید.
