# Bootloader — پوشه مستقل

این پوشه کاملاً جدا از `firmware/` است و **نباید** به هیچ‌کدام از Contractهای
Application (Queue, Time Base, Event, Managementها) وابسته شود.

## مرجع کامل

قبل از نوشتن هر خط کد، این سه سند را کامل بخوانید:
```
docs/03-infrastructure-contracts/bootloader.md
docs/03-infrastructure-contracts/nor-flash-infrastructure.md      ← منبع «PROGRAM Staging» (بخش ۴-۳) - محل احتمالی Firmware جدید پیش از Programming
docs/03-infrastructure-contracts/file-transfer-infrastructure.md  ← کل مسیر بالادست: Server → File Transfer → Firmware Staging → Firmware Update Management → PROGRAM Staging (بخش ۱۴)
```
این پوشه فقط اسکلت است؛ محتوای واقعی هر فایل باید **دقیقاً بر اساس همین سه سند**
نوشته شود، نه بر اساس فرض یا الگوی معمول Bootloaderهای دیگر. دو مورد آخر هنوز
بندهای 🔴 بلاک‌کننده باز دارند (ظرفیت ۱۶MB، ضرورت کپی واسط) - نگاه کنید OPEN-ITEMS.md.

## قانون طلایی این پوشه

> **Active Bank فقط بعد از Programming کامل و تأیید CRC تغییر می‌کند.**
> **هیچ مسیری نباید این قانون را دور بزند، حتی برای ساده‌سازی کد یا تست سریع‌تر.**

## ساختار

```
bootloader/
├── include/          Contract و تعریف نوع‌ها (BankId, BootAction, BootControl, BootErrorRecord)
├── src/               منطق تصمیم‌گیری - مستقل از MCU، قابل تست روی Host
├── test/              تست‌های Host (Ceedling) - سناریوهای Fail-Safe
├── mcu_f4_boot/       پروژه Cube جدا، مخصوص Bootloader روی F4
├── mcu_f1_boot/       پروژه Cube جدا، مخصوص Bootloader روی F1
├── project.yml        تنظیمات Ceedling این پروژه - کاملاً جدا از firmware/project.yml
└── .gitignore         مثل firmware/.gitignore - فقط build/ خروجی Ceedling را نادیده می‌گیرد
```

اجرای تست‌های این پوشه (کاملاً مستقل از `firmware/`):

```
cd bootloader
ceedling test:all
```

## وضعیت فعلی این پوشه

`include/` (۸ فایل هدر: ۴ تایپ پایه + `boot_hal.h` + `boot_image_header.h` + `boot_config.h` + `bootloader.h`) و `src/bootloader.c` (منطق تصمیم‌گیری کامل: BOOT/PROGRAM، Retry، Fail-Safe) نوشته و تست شده‌اند - ۱۳ تست در `test/unit/` (`test_smoke.c` + `test_bootloader.c`)، همه PASS، Compile با `gcc -Wall -Wextra` بدون Warning. فقط `mcu_f4_boot/` و `mcu_f1_boot/` هنوز خالی‌اند (پروژه CubeIDE واقعی هیچ‌کدام ساخته نشده)، پس تنها پیاده‌سازی امروز `boot_hal.h` یک Mock تست است، نه سخت‌افزار واقعی.

**دو Placeholder موقتی صریحاً علامت‌گذاری‌شده** (طبق درخواست کاربر - برای پیش‌بردن تست/دیباگ، نه بستن یک تصمیم معماری): فرمت `BootImageHeader` (`include/boot_image_header.h`) و منبع «New Image» (`boot_hal.h`'s `NewImage_Read`/`GetSize`، جایگزین موقت رابطه هنوز-باز Firmware Update/Inactive-Bank - از بعد از اضافه‌شدن `nor-flash-infrastructure.md` می‌دانیم اسمش احتمالاً «PROGRAM Staging» است، اما همچنان Placeholder). جزئیات در `src/README.md`.

## نکات مهم قبل از شروع (خلاصه از bootloader.md - جزئیات کامل آنجاست)

1. **Dual-Bank:** همیشه Programming روی بانک غیرفعال؛ بانک فعال تا تأیید کامل CRC دست‌نخورده می‌ماند.
2. **یک یادداشت باز کاملاً حل‌نشده مانده، یکی جهتش مشخص شده اما Wire-Layout هنوز موقتی است** (جزئیات در بخش ۶ و ۱۳ سند اصلی):
   - **کاملاً باز:** رابطه محل دانلود Firmware Update Management با «بانک غیرفعال» (مستقیم یا از طریق Staging؟) — روی ظرفیت NOR اثر مستقیم دارد. `nor-flash-infrastructure.md` این ناحیه را «PROGRAM Staging» نام‌گذاری کرده اما ضرورت وجودش (بخش ۴-۲) و ظرفیت ۱۶MB (بخش ۴-۱، بلاک‌کننده) هنوز باز است. `src/` نباید این را حدس بزند.
   - **جهت مشخص شده، جزئیات موقت:** مقصد «Boot Error» مسیر Fault استاندارد `System Management` است، نه یک مکانیزم Log جداگانه — این سمت Application (`firmware/src/managements/system_management.c` → `check_boot_error()`) از قبل پیاده و تست شده. `include/boot_error.h` همین‌جا دقیقاً همان Layout را (`STORAGE_RECOVERY_BOOT_ERROR_OFFSET = 8`) آینه می‌کند تا دو طرف هماهنگ بمانند. اگر `src/` این Layout را عوض کرد، باید همزمان `system_management.c` هم عوض شود.
3. **F1 و F4 دو پیاده‌سازی سخت‌افزاری جدا دارند** اما منطق تصمیم (`src/`) باید کاملاً مشترک بماند؛ فقط رابط با Flash/EEPROM/CRC Hardware در هر Cube جدا پیاده می‌شود.
4. **Retry محدود است** — تعداد دقیق، پارامتر پیاده‌سازی است (نه چیزی که در Contract معماری ثابت شده)؛ اما «بی‌نهایت Retry» یا «Retry بدون شمارش» مجاز نیست.
5. **هیچ Firmware نامعتبری نباید اجرا شود** — اگر CRC بانک فعال هم نامعتبر بود (بدترین حالت)، Bootloader باید در حالت Bootloader بماند، نه یک حدس بزند.
