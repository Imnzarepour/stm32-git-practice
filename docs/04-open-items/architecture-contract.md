# قرارداد معماری Firmware — مسیرهای مرجع و مرزهای ممنوع

## هدف این سند

این سند برای هر توسعه‌دهنده‌ای که قرار است روی این Firmware تغییری اعمال کند نوشته شده. دو کار می‌کند:

1. **۴۰ مسیر مرجع** را فهرست می‌کند — جریان‌های داده/رخداد/عملیات/خطا که باید همیشه طبق همین الگو کار کنند.
2. **مرزهای ممنوع** را فهرست می‌کند — قوانینی که در **هیچ تغییری**، حتی کوچک، نباید نقض شوند.

> **قانون استفاده:** قبل از هر تغییر در کد، توسعه‌دهنده باید مشخص کند این تغییر کدام‌یک از ۴۰ مسیر زیر را لمس می‌کند، و بعد از تغییر، آن مسیر(ها) را دوباره traceable نگه دارد. جدا از این، مرزهای ممنوع در بخش پایانی، مستقل از این‌که تغییر مربوط به کدام مسیر است، همیشه باید برقرار بمانند.

برای جزئیات کامل هر Contract، به `docs/02-management-contracts/` و `docs/03-infrastructure-contracts/` مراجعه شود؛ این سند فقط نقشه مسیرهاست، نه خود Contract.

---

## معماری در یک نگاه

Firmware یک **Cooperative Non-Blocking Super Loop** است. ۱۰ Management (System, Power, Hardware Input, Diagnostic, GPS, Server, SMS, Call, FOTA, Actuator) و چند Infrastructure (Time Base, Queue, Memory/Storage, Driver Layer, Configuration, File Transfer, Log) دارد. ارتباط بین Managementها فقط از طریق پنج مسیر انجام می‌شود: **Data** (Provider→Latest Value→Consumer)، **Event** (Detection→Event→Policy)، **Action** (Request→Queue→Owner→Execution)، **Fault** (Management→Fault→Recovery→[Reset Request])، **Reset** (Reset Request→System Management→Reset). جزئیات کامل در `CLAUDE.md`.

---

## ۴۰ مسیر مرجع

### Data (۱–۵)

1. وضعیت ACC از Hardware Input تا تعیین Power Mode
2. Power Mode از Power Management تا رفتار هر ۹ Management دیگر
3. ولتاژ باتری از ADC تا Telemetry روی سرور
4. موقعیت GPS از GNSS تا Telemetry روی سرور
5. وضعیت شبکه/SIM از EC200U Manager تا LED روی System Management

### Event (۶–۱۲)

6. رخداد Shock از Hardware Input تا SMS + Call + Server + Log هم‌زمان
7. رخداد ACC_ON از Hardware Input تا Server تنها (بدون SMS/Call/Log)
8. رخداد Low Battery از آستانه ولتاژ تا Persist شدن در Log
9. رخداد GPS_FIX_LOST از GPS Management تا Server Event Queue
10. رخداد FOTA_COMPLETED از FOTA Management تا اعلام به سرور
11. رخداد Actuator Rejected از Actuator Management تا Log + Server
12. یک Event تعریف‌نشده در جدول Policy از Event Queue تا Fault Queue، بدون توقف Super Loop

### Action (۱۳–۱۸)

13. درخواست قطع موتور از Server تا اجرای واقعی روی Actuator
14. درخواست باز کردن درب از SMS تا اجرای واقعی روی Actuator
15. درخواست Diagnostic از Server تا شروع RUNNING در Diagnostic Management
16. درخواست FOTA از Server تا شروع دانلود در FOTA Management
17. ارسال SMS از یک Event (مثلاً Low Battery) تا خروج واقعی از EC200U
18. برقراری تماس از یک Event (مثلاً Shock) تا خروج واقعی از EC200U

### Fault → Reset (۱۹–۲۳)

19. خطای Diagnostic (Timeout روی CAN) از Local Recovery تا Reset Request به System Management
20. خطای ارتباط سرور (Connection Failure) از Server Management تا Module Reset درخواستی برای EC200U
21. خطای GPS (GNSS Unavailable) از GPS Management تا تصمیم نهایی System Management برای اجرا یا رد Reset
22. یک ResetRequest نامعتبر (مثلاً SYSTEM_RESET با Target=EC200U) از ارسال تا رد شدن توسط System Management
23. خطای Boot/Programming از Bootloader تا EEPROM تا خواندن توسط Application در Startup

### Log (۲۴–۲۸)

24. خطا در Diagnostic Management تا ذخیره‌شدن به‌صورت Log در NOR Flash
25. رد شدن یک فرمان در Actuator Management تا ثبت در Log Queue و سپس LOG Partition
26. Boot Error از Bootloader تا مسیر دوگانه‌اش (هم Fault Queue، هم Log Queue)
27. پر شدن Log Queue تا رفتار Drop-Oldest روی Recordهای قدیمی‌تر
28. پر شدن LOG Partition روی NOR تا استفاده مجدد از قدیمی‌ترین فایل (Rotation)

### FOTA کامل (۲۹–۳۴)

29. دانلود Firmware از Server تا نشستن در FOTA Staging (LittleFS)
30. اعتبارسنجی بسته دانلودشده در FOTA Management تا نوشتن Raw در PROGRAM Staging
31. تصمیم نصب از FOTA Management (Boot Action=PROGRAM) تا ارسال Reset Request
32. بعد از Reset، از Bootloader تا خواندن Raw از PROGRAM Staging و شروع Programming
33. موفقیت Programming از CRC تأییدشده تا تعویض واقعی Active Bank
34. شکست Programming از Retry ناموفق تا بازگشت امن به اجرای Bank قبلی

### Diagnostic State Machine (۳۵–۳۸)

35. یک درخواست وقتی Current آزاد است تا شروع فوری RUNNING
36. یک درخواست دوم وقتی Current مشغول است تا نشستن در Pending
37. یک درخواست سوم وقتی هم Current هم Pending پرند تا رد صریح با DIAG_BUSY
38. یک نتیجه SUCCESS مصرف‌نشده تا جلوگیری از شروع Pending بعدی (تا زمان GetResponse)

### Configuration (۳۹–۴۰)

39. تغییر Server Address (مثلاً از Sandbox به Operation) از منبع تغییر تا بارگذاری در Runtime Configuration
40. یک Write ناموفق Configuration تا باقی‌ماندن Runtime Configuration بدون تغییر (عدم Corruption)

---

## مرزهای ممنوع — در هیچ تغییری نباید نقض شوند

این‌ها مستقل از این‌که کدام مسیر بالا لمس می‌شود، **همیشه** باید برقرار بمانند. قبل از ثبت هر تغییر، توسعه‌دهنده باید صریح تأیید کند که هیچ‌کدام نقض نشده:

**۴۱. بدون فراخوانی مستقیم Management به Management.**
هیچ فایل `src/managements/*.c` نباید مستقیماً تابعی از یک `src/managements/*.c` دیگر را صدا بزند. ارتباط فقط از طریق Queue، Event، یا Shared State مجاز است.

**۴۲. بدون دسترسی مستقیم به سخت‌افزار خام از داخل Management.**
هیچ Managementی (به‌جز Hardware Input Management) نباید مستقیم `GPIO_Read/Write`, `ADC_Read`, `CAN_Send/Receive` را صدا بزند. Diagnostic Management استثنای مصوب برای CAN است (طبق `diagnostic-management.md` بخش ۱۹)، ولی حتی آن هم فقط از طریق Hardware Proxy Contract.

**۴۳. Bootloader هرگز LittleFS را نمی‌شناسد.**
هیچ فایل در پوشه `bootloader/` نباید به `littlefs`، File API، یا Memory Infrastructure عمومی وابسته باشد. تعامل آن با External NOR محدود به Raw Read از PROGRAM Staging است.

**۴۴. Reset فقط از طریق System Management.**
هیچ Managementی (از جمله Bootloader) نباید مستقیماً MCU یا یک Module را Reset کند؛ فقط `System_SubmitResetRequest()`.

**۴۵. Queue تصمیم نمی‌گیرد.**
هیچ صف (`Queue_Push/Pop`) نباید منطق Policy، Priority داخلی، یا Retry داشته باشد — این‌ها همیشه مسئولیت Consumer است.

**۴۶. بدون Command Safety Policy برای فرمان‌های حساس.**
تا زمانی که تصمیم معماری برای Command Safety Policy (بررسی ایمنی قبل از اجرای Engine Cut-off/Door Lock-Unlock) گرفته نشده (طبق `OPEN-ITEMS.md`)، هیچ‌کس نباید مسیر ۱۳ یا ۱۴ را به‌گونه‌ای کامل کند که Actuator بدون این بررسی واقعاً Command را روی سخت‌افزار اجرا کند.

---

## نحوه استفاده از این سند

1. قبل از شروع یک تغییر: کدام شماره(های) از ۴۰ مسیر بالا را لمس می‌کند؟
2. بعد از تغییر: همان مسیر(ها) دوباره Trace شود — هنوز طبق همین الگوست؟
3. مستقل از مورد ۱ و ۲: هیچ‌کدام از ۶ مرز ممنوع (۴۱–۴۶) نقض نشده؟
4. اگر تغییر یک مسیر جدید (خارج از این ۴۰ تا) ایجاد می‌کند، این سند باید به‌روزرسانی شود — نه این‌که مسیر جدید بدون ثبت باقی بماند.
