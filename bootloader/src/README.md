# src/ — منطق تصمیم‌گیری (مستقل از MCU)

این پوشه بر اساس **بخش‌های ۴ تا ۱۵ و ۱۸ سند `bootloader.md`** پر شده است.

**وضعیت: `bootloader.c` نوشته و تست شده است** (۸ تست در `../test/unit/test_bootloader.c`، همه PASS). فقط از طریق `../include/boot_hal.h` با «سخت‌افزار» صحبت می‌کند - هیچ HAL/CMSIS/Register مستقیم اینجا نیست، طبق قانون سخت‌گیرانه پروژه.

## توابع اصلی (نام واقعی در `bootloader.c`)

```c
void Bootloader_Run(void);          // نقطه ورود اصلی - نمودار بخش ۱۸ سند، در include/bootloader.h

/* داخلی - static: */
static bool bank_image_is_valid(BankId bank);                    // بخش ۴ (Active Bank) و بخش ۸ (Verify بعد از Programming) - همان منطق، دو محل استفاده، پس یک تابع مشترک به‌جای دو اسم تکراری README قبلی
static bool program_inactive_bank(BankId inactive_bank);         // بخش ۵-۷
static void switch_active_bank(BankId new_active_bank);          // بخش ۱۱ - فقط بعد از bank_image_is_valid() موفق
static void keep_active_bank_and_return_to_boot(BankId still_active_bank); // بخش ۹/۱۰ - Fail-Safe fallback
static void record_boot_error(uint32_t error_type, uint32_t error_code);   // بخش ۱۲
static void run_program_flow(BankId active_bank);                // بخش ۹ - حلقه Retry محدود دور program_inactive_bank+bank_image_is_valid
```

## دو Placeholder موقتی که `bootloader.c` واقعاً استفاده می‌کند (نه حدس معماری، طبق درخواست صریح کاربر)

- **`../include/boot_image_header.h` (`BootImageHeader`):** یک هدر کوچک (`magic`, `image_size`, `image_crc`) در ابتدای هر Bank، چون سند اصلی فرمت دقیق Image/Signature را باز گذاشته (بخش ۱۶/۱۷) ولی بدون همچین چیزی "Calculate CRC → Compare" (بخش ۴/۸) قابل اجرا/تست نیست.
- **`../include/boot_hal.h`'s `NewImage_Read`/`NewImage_GetSize`:** جایگزین موقت محل واقعی Firmware Update/Inactive-Bank (یادداشت باز #۱، کاملاً باز مانده). `program_inactive_bank()` فقط از این دو تابع می‌خواند، هیچ فرضی درباره NOR/Staging واقعی نکرده.

  **به‌روزرسانی:** `docs/03-infrastructure-contracts/nor-flash-infrastructure.md` (بخش ۴-۳) حالا به این ناحیه اسم داده - **PROGRAM Staging** روی NOR خارجی (W25Q128)، Raw، فقط Bootloader می‌خواند - که با شکل این دو تابع همخوانی دارد. اما همان سند بخش ۴-۱ (ظرفیت ۱۶MB احتمالاً کافی نیست - بلاک‌کننده) و بخش ۴-۲ (آیا اصلاً این کپی واسط لازم است) را باز گذاشته، پس هنوز Placeholder است، نه تصمیم بسته. یک ناهماهنگی عددی هم ثبت شده: آن سند تخمین می‌زند Firmware Image واقعی می‌تواند تا ~۲MB باشد، در حالی که `BOOT_BANK_SIZE_BYTES` در `../include/boot_config.h` فقط ۲۵۶KB است (Placeholder صرفاً برای راحتی تست) - این دو باید با هم بازبینی شوند.

  **به‌روزرسانی دوم:** `docs/03-infrastructure-contracts/file-transfer-infrastructure.md` (بخش ۱۴) کل مسیر بالادست `NewImage_*` را روشن‌تر کرده: `Server → File Transfer Infrastructure (Download/Resume/Chunk، معنای فایل را نمی‌داند) → Firmware Staging (LittleFS) → Firmware Update Management (Validate/Version) → نوشتن Raw در PROGRAM Staging`. این سند صریحاً تکرار می‌کند «Firmware Update Management کاملاً دست‌نخورده باقی می‌ماند» - یعنی `NewImage_Read`/`GetSize` همچنان معادل همان مرز «Bootloader فقط از PROGRAM Staging می‌خواند» می‌ماند، چیزی در این پوشه عوض نمی‌شود.

هر دو در کامنت بالای خودشان و بالای `bootloader.c` با `TODO(open-item)` علامت‌گذاری شده‌اند - وقتی آن دو تصمیم بسته شدند، این دو فایل (و هر جای `bootloader.c` که صدایشان می‌زند) باید بازنویسی شوند.

## قانون‌های غیرقابل‌مذاکره (از سند اصلی) - همه در `bootloader.c` رعایت شده‌اند

1. `switch_active_bank()` **فقط و فقط** بعد از موفقیت `bank_image_is_valid()` روی بانک تازه‌Program‌شده صدا زده می‌شود — هیچ مسیر دیگری وجود ندارد (بخش ۱۱).
2. شکست Programming تا `BOOT_MAX_PROGRAM_RETRIES` (`../include/boot_config.h`) بار Retry می‌شود، سپس بازگشت امن به Bank فعال قبلی (بخش ۹) - `keep_active_bank_and_return_to_boot()`.
3. اگر CRC بانک فعال هم نامعتبر بود، اجرا نمی‌شود — در `record_boot_error()` می‌ماند، هیچ Jump ای انجام نمی‌شود (بخش ۴).
4. این منطق کاملاً مستقل از HAL/Cube نوشته شده؛ فقط از طریق `../include/boot_hal.h` (`Flash_*`, `CRC_Calculate`, `EEPROM_*`, `NewImage_*`) که پیاده‌سازی واقعی‌شان در `mcu_f4_boot/` و `mcu_f1_boot/` جداگانه نوشته خواهد شد — دقیقاً همان الگوی Driver Layer که در `firmware/` استفاده شد. امروز تنها پیاده‌سازی این هدر، Mock تولیدشده توسط CMock داخل `../test/unit/test_bootloader.c` است.

## چیزی که هنوز در سند اصلی باز است و نباید اینجا حدس زده شود

- **کاملاً باز:** رابطه محل دانلود Firmware Update با Inactive Bank (یادداشت باز #۱ در `bootloader.md`، بخش ۶). `NewImage_Read`/`NewImage_GetSize` (بالا را ببینید) فقط یک Placeholder موقت‌اند تا تست ممکن شود - وقتی این تصمیم واقعی گرفته شد، این دو تابع (و صدازننده‌شان `program_inactive_bank()`) باید بازبینی شوند. **یک دوپهلویی مرتبط هم ثبت شده در OPEN-ITEMS (بخش 🟡 #۴):** `nor-flash-infrastructure.md` بخش ۴-۲ از عبارت «Firmware Update مستقیماً... به یک Programming Interface تحویل دهد» استفاده کرده که می‌تواند با اصل صریح `bootloader.md` («Firmware Update Management نباید مستقیماً MCU را برای اجرای Firmware جدید دستکاری کند») در تناقض باشد یا نباشد - این پوشه هیچ‌وقت نباید فرض کند Firmware Update مستقیماً Flash را برنامه‌ریزی می‌کند؛ `program_inactive_bank()` همیشه تنها نقطه Programming می‌ماند تا این ابهام رسماً حل شود.
- **جهتش مشخص شده، فقط جزئیات را از `../include/` بگیرید:** مقصد نهایی Boot Error (یادداشت باز #۲ در `bootloader.md`، بخش ۱۳) دیگر کاملاً باز نیست - سمت Application (`firmware/src/managements/system_management.c`) از قبل تصمیم گرفته که این مسیر Fault باشد، نه Log. `record_boot_error()` دقیقاً طبق Layout تعریف‌شده در `../include/boot_error.h` (`STORAGE_RECOVERY_BOOT_ERROR_OFFSET`) می‌نویسد - اگر لازم شد این Layout تغییر کند، هر دو طرف (اینجا و `system_management.c`) باید با هم عوض شوند.
