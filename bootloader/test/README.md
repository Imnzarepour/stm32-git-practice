# test/ — تست‌های Host (مستقل از سخت‌افزار)

طبق همان فلسفه Fake/Stub که در کل پروژه استفاده شده (نگاه کنید به
`firmware/test/`)، منطق `src/` بدون نیاز به سخت‌افزار واقعی، با یک Fake
Flash/EEPROM در حافظه (آرایه ساده در RAM) قابل تست کامل است.

**وضعیت: کامل.** این پوشه یک `project.yml` مستقل دارد (Ceedling، کاملاً جدا
از `firmware/project.yml`) - از همین پوشه (`bootloader/`) با `ceedling test:all`
اجرا می‌شود (۱۳ تست، همه PASS: ۵ در `test_smoke.c` + ۸ در `test_bootloader.c`).

## تصمیم معماری تست: CMock روی `boot_hal.h`، نه فایل‌های Fake دستی

نسخه قبلی این فایل فایل‌های `test/fakes/fake_flash.c` و مشابه را پیشنهاد داده
بود (طبق الگوی ظاهری `firmware/test/fakes/`). بعد از بررسی دقیق‌تر معلوم شد
آن فایل‌ها در `firmware/` عملاً در Build وارد نمی‌شوند (`project.yml`'s
`:source:` هرگز `test/fakes/` را نمی‌بیند) - Management‌های واقعی آنجا
همه‌شان از CMock روی Contract مربوطه استفاده می‌کنند (مثلاً
`test_storage.c` → `Mockhardware_proxy_storage.h`، `test_system_management.c`
→ `Mockstorage.h`). این پوشه هم همان الگوی **واقعاً کارکرده** را دنبال کرد:

`test/unit/test_bootloader.c` مستقیماً `Mockboot_hal.h` (تولیدشده خودکار
توسط CMock از `../include/boot_hal.h`) را با `_StubWithCallback()` به یک
Fake کامل در RAM وصل می‌کند - دو Bank فرضی، یک EEPROM فرضی (روی همان
Offsetهای `boot_control.h`/`boot_error.h`)، و یک بافر «New Image» فرضی.
هیچ فایل جدا در `test/fakes/` یا `test/support/` لازم نشد؛ `test/support/`
همچنان فقط `.gitkeep` دارد (ساختار Ceedling این پوشه را می‌خواهد، محتوایی
لازم نبود).

## سناریوهای پوشش داده‌شده (`test_bootloader.c`)

| سناریو | بخش مرجع | نتیجه تست‌شده |
|---|---|---|
| CRC بانک فعال معتبر است | بخش ۴ | `test_valid_active_bank_jumps_to_application` |
| CRC بانک فعال نامعتبر است | بخش ۴ | `test_invalid_active_bank_stays_in_bootloader_and_records_an_error` |
| Programming موفق + CRC جدید معتبر | بخش ۱۱ | `test_successful_programming_switches_active_bank_and_then_boots_it` |
| نوشتن ناموفق وسط یک تلاش («Power Loss»)، ولی Retry بعدی موفق می‌شود و از صفر (بعد از Erase دوباره) می‌نویسد | بخش ۹/۱۰ | `test_a_failed_write_is_safely_retried_and_can_still_succeed` |
| Programming در همه تلاش‌ها شکست می‌خورد تا سقف Retry | بخش ۹ | `test_retries_exhausted_falls_back_to_the_previous_active_bank` (Bank فعال دست‌نخورده، Error ثبت‌شده، Boot Action=BOOT) |
| هیچ New Image ای موجود نیست | بخش ۶/۹ | `test_no_new_image_available_is_treated_as_a_failed_attempt` (بدون Erase) |
| New Image بزرگ‌تر از یک Bank | بخش ۵ | `test_oversized_new_image_is_rejected_without_erasing_flash` |
| EEPROM قابل‌خواندن نیست (اولین Boot/EEPROM خام) | - (بدون Contract رسمی، نگاه کنید کامنت `bootloader.c`) | `test_unreadable_eeprom_defaults_to_bank_1_boot` |

سناریوی «بعد از Boot Error، پاک‌سازی خطا» عمداً اینجا تست نشده: پاک‌سازی
Boot Error Record مسئولیت Application است، نه Bootloader (بخش ۱۳ سند -
Bootloader فقط می‌نویسد؛ Application می‌خواند و پاک می‌کند) - همین رفتار در
`firmware/test/unit/test_system_management.c` (`..._and_then_cleared`) از
قبل تست شده است.

## نکته حیاتی این پوشه

سناریوی «نوشتن ناموفق وسط یک تلاش» مهم‌ترین تستیه که باید وجود داشته باشه —
دقیقاً همون چیزیه که اصل Fail-Safe کل این Bootloader روش بنا شده:
`test_a_failed_write_is_safely_retried_and_can_still_succeed` عمداً یک
Payload دو-Chunk‌ای می‌سازد و فقط Chunk دوم تلاش اول را خراب می‌کند، تا هم
نوشتن ناقص واقعاً اتفاق بیفتد و هم ثابت شود Retry بعدی (با یک `Flash_Erase`
تازه) کاملاً تمیز از نو می‌نویسد - بدون اینکه در کل این مسیر Bank فعال
(`BANK_1`) حتی یک بایت دست بخورد.
