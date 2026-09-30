# mcu_f4_boot/ — پروژه Cube مجزا برای Bootloader روی F4

اینجا پروژه CubeIDE مستقل بسازید (نه همون mcu_f4 اصلی که برای Application
استفاده می‌کنید). این پروژه:

1. Linker Script جدا دارد (Bootloader روی آدرس ثابت اول Flash می‌شیند،
   جدا از Bank 1 / Bank 2 که Application در آن‌ها اجرا می‌شود).
2. کد `bootloader/src/` و `bootloader/include/` را به‌عنوان Source/Include
   خارجی اضافه می‌کند (دقیقاً همون روشی که در `mcu/README.md` پروژه اصلی
   firmware/ برای اتصال Driverهای Application توضیح داده شده بود).
3. فقط باید امضای دقیق `bootloader/include/boot_hal.h` را برای F4 واقعی پیاده کند (این فایل از قبل نوشته و در تست‌های Host با CMock وصل شده - امضاها را از همان‌جا بگیرید، حدس نزنید):
   ```
   Flash_Read / Flash_Program / Flash_Erase / Flash_JumpToApplication   (HAL Flash واقعی F4)
   CRC_Calculate                                                          (CRC Peripheral یا نرم‌افزاری F4)
   EEPROM_Read / EEPROM_Write                                             (شبیه‌سازی EEPROM روی Internal Flash یا I2C EEPROM واقعی - بسته به سخت‌افزار)
   NewImage_Read / NewImage_GetSize                                       (⚠️ TEMPORARY - نگاه کنید bootloader/src/README.md و docs/03-infrastructure-contracts/nor-flash-infrastructure.md بخش ۴ - جایگزین موقت تصمیم هنوز-باز Firmware Update/Inactive-Bank (احتمالاً «PROGRAM Staging»)، این دو تابع را زودتر از آن تصمیم پیاده نکنید)
   ```
   فایل واقعی باید مثل الگوی `firmware/src/hardware_proxy/*_stm32f4.c` نام‌گذاری شود: `boot_hal_stm32f4.c`.

   نکته مرجع: سمت `firmware/` برای `STORAGE_CONFIG` دقیقاً همین سؤال را قبلاً
   جواب داده - ST EEPROM Emulation روی Flash داخلی F4 (نگاه کنید
   `firmware/src/hardware_proxy/hardware_proxy_storage_stm32f4.c` و
   `docs/03-infrastructure-contracts/hardware-proxy.md`). این تصمیم فقط مال
   `STORAGE_CONFIG` است و اینجا خودکار تکرار نمی‌شود، اما به‌عنوان یک نمونه
   کارکرده روی همین تراشه ارزش مرور دارد.

## هشدار

هیچ منطق تصمیم‌گیری (BOOT/PROGRAM/Retry/CRC Compare) نباید داخل این پوشه
نوشته شود — آن منطق در `bootloader/src/` است و مشترک بین F1/F4 می‌ماند.
اینجا فقط "چطور با سخت‌افزار F4 صحبت کنیم" پیاده می‌شود.
