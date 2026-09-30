# include/ — Contract و تعریف نوع‌ها

این پوشه بر اساس **بخش‌های ۳، ۱۲ و ۱۶ سند `bootloader.md`** پر شده است.

## وضعیت: کامل - ۸ فایل هدر، همه استفاده‌شده در `../src/bootloader.c` و تست‌شده

علاوه بر ۴ تایپ پایه زیر، این‌ها هم اضافه شدند (برای رساندن `../src/` به یک پیاده‌سازی واقعی و تست‌پذیر، طبق درخواست صریح کاربر):

- **`boot_hal.h`** — تنها مرز مجاز بین `../src/bootloader.c` و سخت‌افزار (معادل Hardware Proxy برای این پروژه مستقل): `Flash_Read/Program/Erase/JumpToApplication`، `CRC_Calculate`، `EEPROM_Read/Write`، و یک جفت تابع موقت `NewImage_Read/GetSize` (بعد از اضافه‌شدن `docs/03-infrastructure-contracts/nor-flash-infrastructure.md`، این دو احتمالاً معادل «PROGRAM Staging» بخش ۴-۳ همان سند هستند - هنوز Placeholder، نه تصمیم بسته، چون بخش‌های ۴-۱/۴-۲ همان سند باز مانده‌اند).
- **`boot_image_header.h`** — فرمت موقت هدر هر Image (`magic`/`image_size`/`image_crc`) - صراحتاً **نه** یک Contract رسمی، فقط چیزی که «Calculate CRC → Compare» را قابل اجرا/تست می‌کند تا فرمت واقعی Image/Signature تعیین شود.
- **`boot_config.h`** — ثابت‌های قابل‌تنظیم (`BOOT_BANK_SIZE_BYTES` Placeholder، `BOOT_MAX_PROGRAM_RETRIES`، `BOOT_COPY_CHUNK_BYTES`) - هم‌سبک `firmware/include/managements/hardware_input_management_config.h`.
- **`bootloader.h`** — فقط `void Bootloader_Run(void);`.

## چهار تایپ پایه

```c
// bank_id.h  (بخش ۲ و ۳ سند)
typedef enum { BANK_1, BANK_2 } BankId;

// boot_action.h  (بخش ۳ سند)
typedef enum { BOOT_ACTION_BOOT, BOOT_ACTION_PROGRAM } BootAction;

// boot_control.h  (بخش ۳ سند - این‌ها در STORAGE_RECOVERY نگهداری می‌شوند)
typedef struct {
    BankId     active_bank;
    BootAction action;
} BootControl;

// boot_error.h  (بخش ۱۲ سند)
typedef struct {
    uint32_t error_type;
    uint32_t error_code;
    /* دقیقاً همین دو فیلد - سند اصلی می‌گوید "+ اطلاعات لازم برای Diagnosis"
     * بدون جزئیات، پس چیزی اضافه بر این حدس زده نشده. */
} BootErrorRecord;
```

`test/unit/test_smoke.c` این چهار فایل را Compile/Link می‌کند و مقادیر enum + Offsetها را تثبیت می‌کند؛ `test/unit/test_bootloader.c` هر ۸ فایل هدر بالا را از طریق `../src/bootloader.c` واقعی تست می‌کند (۸ سناریو، همه PASS) - `ceedling test:all` را از همین پوشه اجرا کنید.

## Layout بایتی STORAGE_RECOVERY — نکته حیاتی هماهنگی با Application

این تنها بخشی از این پوشه است که با کد بیرون از `bootloader/` (یعنی `firmware/`)
باید هماهنگ بماند، چون هر دو طرف روی همین چند بایت EEPROM/NOR کار می‌کنند:

| Offset | فیلد | تعریف در |
|---|---|---|
| 0 | `BootControl` (Active Bank + Boot Action) | `boot_control.h` - فقط Bootloader می‌نویسد |
| 8 | `BootErrorRecord` (Error Type + Error Code) | `boot_error.h` - Bootloader می‌نویسد، Application (`firmware/src/managements/system_management.c` → `check_boot_error()`) می‌خواند و پاک می‌کند |

این Layout رسماً در هیچ سند معماری بسته نشده - فقط چون سمت Application (`system_management.c`، `BOOT_ERROR_RECORD_OFFSET`) از قبل با همین دو عدد نوشته و تست شده، اینجا هم همان اعداد تکرار شدند تا از روز اول دو طرف سازگار باشند. **اگر این Layout در `src/` تغییر کرد، `system_management.c` باید همزمان عوض شود** (و برعکس).

## نکات مهم قبل از ادامه این پوشه

- این تایپ‌ها **نباید** به `firmware/include/shared/event.h` یا هیچ Contract
  دیگر Application وابسته باشند. Bootloader دنیای خودش را دارد (تنها استثنا
  همین هماهنگی Offset بالا، که یک قرارداد داده است، نه وابستگی کد).
- `BootErrorRecord` از قبل طوری طراحی شده که سمت Application به یک `Fault`
  استاندارد (طبق مسیر Fault در `system-management.md`) تبدیلش می‌کند — یادداشت
  باز شماره ۲ در `bootloader.md` از این نظر دیگر باز نیست، فقط Wire-Layout آن
  موقتی مانده (بالا را ببینید).
- CRC Contract (نوع الگوریتم، اندازه خروجی) هنوز اینجا مشخص نشده - `boot_hal.h`'s
  `CRC_Calculate()` فقط امضای تابع (Bank/Offset/Length → `uint32_t`) را ثابت
  می‌کند، نه الگوریتم را؛ باید طبق قابلیت واقعی Hardware (نرم‌افزاری یا CRC
  Peripheral سخت‌افزاری F1/F4) در `mcu_f4_boot/`/`mcu_f1_boot/` تعیین شود -
  عوض‌شدن الگوریتم هیچ‌وقت به `../src/bootloader.c` نشت نمی‌کند.
