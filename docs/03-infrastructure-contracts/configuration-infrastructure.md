# پیوست فنی — Configuration Infrastructure

## 0. یادداشت طبقه‌بندی (قبل از هر چیز)

این پیوست در نسخه اولیه با عنوان «Configuration **Management**» نوشته شده بود. طبق بررسی، این یک **Infrastructure** است، نه یک Management — دقیقاً به همان معیارهایی که در `docs/03-infrastructure-contracts/تعریف زیر ساخت های مشترک.md` برای تفکیک این دو تعریف شده:

| معیار | نتیجه در این مورد |
|---|---|
| مالک Capability محصول است؟ | خیر — خودش صراحتاً می‌گوید «مالک معنای داده نیست» |
| تصمیم‌گیری Product دارد؟ | خیر — تصمیم درباره اعتبار/مجوز تغییر با Management مالک داده است |
| FSM دارد؟ | خیر |
| Process مستقل در Super Loop نیاز دارد؟ | خیر — فقط Load در Startup و Get/Update برحسب نیاز |

**نتیجه: لیست ۱۰تایی Managementهای رسمی پروژه با این پیوست تغییر نمی‌کند.** این یک زیرساخت مشترک است، در ردیف Time Base، Queue، Memory/Storage و Driver Layer.

---

## 1. هدف

ایجاد یک محل مشخص برای نگهداری، بارگذاری و به‌روزرسانی Configuration دستگاه؛ به‌گونه‌ای که:

* مقادیر مورد نیاز در Startup در دسترس باشند.
* اطلاعات در حافظه پایدار نگهداری شوند.
* در زمان اجرا از یک ساختار مشترک (Runtime Configuration) قابل دسترسی باشند.
* Configuration Infrastructure وارد منطق کسب‌وکار Managementهای دیگر نشود.

---

## 2. محدوده

نمونه‌های اولیه Configuration:

* Serial Number
* Hardware Version
* Device Identifier
* Batch / Production Information
* Server Address / Base URL (هر سه محیط: Sandbox / Deployment / Operation — طبق پیوست Backend Contract)
* Communication Protocol / Parameters
* Allowed Phone Numbers

فهرست نهایی فیلدها در مرحله تعریف Data Model مشخص می‌شود. یک فهرست **Provisional v1** (دقیقاً همین نمونه‌ها + Allowed Phone Numbers که مصرف‌کننده واقعی داشت) در `firmware/include/infrastructure/configuration.h` پیاده شده - ساختار `DeviceConfig` عمداً طوری چیده شده (هر Field = یک Struct Member + یک مقدار Enum + یک ردیف Layout) که اضافه/تغییر یک Field وقتی Data Model نهایی بسته شد، کد Consumer بالادست را عوض نکند.

**پیوند با زیرساخت موجود:** این پیوست از `StorageObject.STORAGE_CONFIG` که از قبل در `docs/03-infrastructure-contracts/memory-storage-infrastructure.md` تعریف شده استفاده می‌کند؛ زیرساخت فیزیکی ذخیره‌سازی از قبل آماده است، این پیوست فقط لایه Runtime روی آن اضافه می‌کند.

---

## 3. خارج از محدوده

مدیریت فایل، Log، GPS Data، Firmware Update Image، NOR خارجی، اجرای رفتار Managementها، اعتبارسنجی تخصصی مقدار (مثلاً معتبر بودن یک شماره تلفن — مسئولیت Call Management است)، Timingهای داخلی Firmware (مگر صراحتاً تنظیم محصول باشند)، FSM/منطق داخلی Managementها.

---

## 4. مدل داده Runtime

```text
Device Configuration
├── Device Identity
├── Hardware / Production Information
├── Communication Configuration
├── Access Configuration
└── Product Configuration
```

Managementها برای خواندن مقدار، مستقیم به حافظه پایدار دسترسی ندارند؛ فقط از این ساختار Runtime می‌خوانند.

---

## 5. راه‌اندازی (Startup)

```text
Startup
   ↓
Read Configuration
   ↓
Validate Storage Integrity
   ↓
Load Runtime Configuration
   ↓
Configuration Ready
```

جزئیات Integrity/Versioning/Recovery در قرارداد Memory/Storage تعیین می‌شود، نه اینجا.

---

## 6. اصل Single Runtime Configuration

```text
Persistent Memory → DeviceConfig → Runtime Usage
```

هیچ Management نباید Configuration را مستقیم از EEPROM/NOR بخواند؛ فقط از طریق ساختار Runtime مشترک.

---

## 7. مسیرهای تغییر

هر فیلد بسته به کاربردش می‌تواند از مسیر متفاوتی تغییر کند:

```text
Factory / Provisioning Tool → Configuration
Backend API → Server Management → Configuration
```

مثال: Serial Number فقط در تولید؛ Server Address از ابزار Provisioning؛ Allowed Phone Numbers از طریق API در زمان بهره‌برداری.

**Configuration Infrastructure تصمیم نمی‌گیرد چه کسی مجاز به تغییر است** — این بخشی از Contract مسیر تغییر و Management مالک داده است.

---

## 8. ذخیره و به‌روزرسانی

```text
New Value → (اعتبارسنجی توسط Management مالک) → Configuration Storage
   → Write to Persistent Memory → Write Successful → Update Runtime Configuration
```

> **Runtime Configuration فقط پس از موفقیت ذخیره‌سازی پایدار به‌روزرسانی می‌شود.**

در صورت شکست Write، Runtime Configuration بدون تغییر باقی می‌ماند — تا اختلاف بین RAM و مقدار پایدار ایجاد نشود.

**بسته شد (تصمیم صریح کاربر):** Management مالک ابتدا خودش مقدار را Validate می‌کند و فقط مقدار تأییدشده را به `Configuration_Update()` می‌دهد. Configuration Infrastructure هیچ Callback/Registry برای Validation ندارد و ندارد — فقط طول Byte مقدار ورودی را با اندازه شناخته‌شده همان Field مقایسه می‌کند (ایمنی حافظه، نه Validation محصولی). پیاده‌سازی: `firmware/src/infrastructure/configuration.c`.

---

## 9. مالکیت معنایی داده

Configuration Infrastructure مالک **معنای** داده نیست؛ هر فیلد یک Owner معنایی مشخص دارد (مثلاً `Allowed Phone Numbers` ← `Call Management`). Infrastructure فقط مقدار را نگهداری می‌کند؛ تصمیم درباره اعتبار مقدار با Management مالک است.

---

## 10. Configuration در مقابل Firmware Parameter

معیار تفکیک:

> **Configuration شامل داده‌ای است که برای محصول، مشتری، تولید یا Provisioning معنا دارد و نیاز به نگهداری مستقل از اجرای Firmware دارد.**

پارامترهایی مثل `Internal FSM Timeout`, `Internal Poll Interval`, `Retry Count`, `Queue Size`, `Protocol Parser Timeout` تا زمانی که نیاز محصولی برای تغییرشان نباشد، Configuration محسوب نمی‌شوند و می‌توانند در کد ثابت بمانند (مثال موجود در پروژه: `DEBOUNCE_THRESHOLD_SAMPLES` در Hardware Input Management، `BATTERY_LOW_THRESHOLD_MV`).

---

## 11. دسترسی در زمان اجرا

```text
Server Management  → DeviceConfig.server
Call Management    → DeviceConfig.allowed_numbers
```

دسترسی مستقیم هیچ Managementی به حافظه پایدار (`Call Management → EEPROM` و مشابه) ممنوع است.

---

## 12. رفتار در Reset

```text
Reset → Startup → Load Persistent Configuration → Runtime Configuration
```

Reset نباید باعث از‌دست‌رفتن Configuration پایدار شود.

---

## 13. حداقل Contract

```text
Configuration
├── Init / Load
├── Get Runtime Configuration
├── Update Configuration
└── Persist Configuration
```

API نهایی نباید Managementهای دیگر را به نوع حافظه یا آدرس فیزیکی وابسته کند.

---

## 14. مرز با Memory / Storage

```text
Managements
     ↓
Configuration Infrastructure
     ↓
DeviceConfig
     ↓
Configuration Storage (STORAGE_CONFIG)
     ↓
Persistent Memory
```

نوع حافظه، Driver، آدرس فیزیکی و Wear Leveling در لایه Memory/Storage تعریف می‌شوند؛ Configuration Infrastructure از نوع حافظه بی‌اطلاع است.

---

## 15. جمع‌بندی

مسئولیت: بارگذاری Configuration در Startup، نگهداری Runtime Configuration مشترک، ذخیره مقادیر جدید در حافظه پایدار، به‌روزرسانی Runtime پس از ذخیره موفق، ارائه Configuration جاری به مصرف‌کنندگان.

خارج از مسئولیت: منطق محصول، اعتبارسنجی تخصصی، مدیریت فایل یا حافظه فیزیکی.

> **Configuration Infrastructure مالک نگهداری Configuration است، نه مالک معنای آن. Runtime Configuration تصویر جاری دستگاه است و فقط پس از موفقیت در ذخیره‌سازی پایدار به‌روزرسانی می‌شود.**
