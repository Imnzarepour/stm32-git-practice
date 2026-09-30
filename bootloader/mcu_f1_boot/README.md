# mcu_f1_boot/ — پروژه Cube مجزا برای Bootloader روی F1

مشابه `mcu_f4_boot/README.md`، ولی برای تراشه F1. امضای دقیق توابعی که باید
پیاده شوند همان `bootloader/include/boot_hal.h` است (`Flash_Read/Program/
Erase/JumpToApplication`، `CRC_Calculate`، `EEPROM_Read/Write`، و
`NewImage_Read/GetSize` که ⚠️ TEMPORARY است - نگاه کنید `bootloader/src/README.md` و `docs/03-infrastructure-contracts/nor-flash-infrastructure.md` بخش ۴)
- فایل واقعی باید `boot_hal_stm32f1.c` نام‌گذاری شود، هم‌سبک
`firmware/src/hardware_proxy/*_stm32f1.c` (که خودش هم هنوز باز مانده، دلیلش
پایین همین صفحه).

## نکته مهم مخصوص این گذار (F4 → F1)

طبق تصمیم‌های قبلی پروژه درباره گذار F4 به F1: نت‌ها یکسان می‌مانند اما
منابع سخت‌افزاری کمتر است. برای Bootloader این نکته خصوصاً مهم است چون:

- **حافظه محدودتر F1** ممکن است روی امکان Dual-Bank واقعی اثر بگذارد —
  باید ظرفیت Flash واقعی F1 با حداقل فضای لازم برای دو بانک کامل + ناحیه
  Bootloader مقایسه شود، قبل از فرض این‌که همان معماری Dual-Bank بدون تغییر
  قابل استفاده است.
- CRC Peripheral سخت‌افزاری در F1 ممکن است متفاوت از F4 باشد یا اصلاً وجود
  نداشته باشد (نیاز به CRC نرم‌افزاری).

این تصمیم‌ها باید قبل از شروع پیاده‌سازی این پوشه با بررسی Datasheet واقعی
F1 گرفته شوند، نه فرض شوند.

نکته مرجع: سمت `firmware/` هم دقیقاً به همین دلیل نسخه F1 لایه Storage
(`hardware_proxy_storage_stm32f1.c`) را هنوز باز گذاشته - چون `MCU_F1/` هنوز
پروژه CubeIDE واقعی ندارد (فقط `pins name.txt`). یعنی برخلاف `mcu_f4_boot/`
که یک نمونه کارکرده از EEPROM Emulation دارد، اینجا هیچ نمونه‌ای برای مرور
وجود ندارد - تصمیم باید از صفر گرفته شود.
