# پیوست فنی — طراحی و پیاده‌سازی EC200U Component (اسپک برون‌سپاری)

## ۱. مقدمه

در محصول فعلی، ماژول Quectel EC200U بخش اصلی ارتباطات سلولار سیستم را فراهم می‌کند و قابلیت‌هایی نظیر ارتباط داده، MQTT/HTTP، پیامک، تماس صوتی، GNSS و پایش وضعیت شبکه را در اختیار Firmware قرار می‌دهد.

با توجه به تنوع قابلیت‌ها، رفتارهای غیرهمزمان ماژول، شرایط متغیر شبکه، محدودیت مصرف توان و ضرورت عملکرد پایدار در محیط واقعی خودرو، ارتباط با EC200U نباید به مجموعه‌ای از AT Commandهای پراکنده در بخش‌های مختلف Firmware تبدیل شود.

هدف این پروژه ایجاد یک EC200U Component مستقل، قابل‌اتکا، تست‌پذیر و قابل‌استفاده توسط سایر بخش‌های Firmware است؛ به‌گونه‌ای که پیچیدگی‌های مربوط به ماژول از Managementهای محصول جدا شود.

---

## ۲. بیان مسئله

Firmware محصول نیاز دارد به‌صورت همزمان یا متناوب از قابلیت‌های مختلف EC200U استفاده کند:

* ارتباط داده با Server (MQTT و در صورت نیاز HTTP/HTTPS)
* ارسال و دریافت SMS
* تماس صوتی
* GNSS و دریافت موقعیت
* بررسی وضعیت و کیفیت شبکه
* مدیریت SIM
* مدیریت اتصال داده
* مدیریت مصرف توان
* تشخیص خرابی و عدم پاسخگویی ماژول
* بازیابی و Reset ماژول

این قابلیت‌ها در شرایط واقعی می‌توانند به‌صورت غیرقابل‌پیش‌بینی با یکدیگر تداخل داشته باشند؛ برای مثال در حالی که سیستم در وضعیت کم‌مصرف قرار دارد، ممکن است نیاز به دریافت SMS، پاسخ به تماس، گرفتن موقعیت یا ارسال یک داده به Server ایجاد شود. بنابراین Component باید بتواند بین قابلیت، زمان‌بندی، وضعیت اتصال، مصرف توان و بازیابی خطا هماهنگی ایجاد کند.

مسئله اصلی پروژه، صرفاً پیاده‌سازی دستورات EC200U نیست؛ بلکه ایجاد یک لایه نرم‌افزاری است که بتواند قابلیت‌های EC200U را به‌صورت پایدار، غیرمسدودکننده و قابل‌کنترل در اختیار Firmware قرار دهد.

---

## ۳. خطوط قرمز پروژه

### ۳.۱ عدم وابستگی Managementهای محصول به AT Command

هیچ Management محصول نباید مستقیماً AT Command، Response یا URC را مدیریت کند. مسیر ارتباط باید به شکل زیر باشد:

```text
Management → EC200U Contract → EC200U Component → UART Driver → EC200U
```

### ۳.۲ عدم Blocking

هیچ عملیات طولانی مانند: برقراری شبکه، دریافت Fix، اتصال MQTT، ارسال داده، ارسال SMS، تماس، Recovery نباید باعث توقف Super Loop شود. تمام عملیات طولانی باید Stateful و Non-Blocking باشند.

### ۳.۳ عدم ورود منطق محصول به EC200U Component

Component نباید تصمیم بگیرد:

* چه زمانی GPS گرفته شود
* چه زمانی داده به Server ارسال شود
* چه SMSای مجاز است
* چه زمانی تماس گرفته شود
* چه Eventای به Server ارسال شود
* چه داده‌ای در NOR ذخیره شود

این تصمیمات متعلق به Managementهای محصول هستند.

### ۳.۴ عدم دسترسی مستقیم به سخت‌افزار از Managementها

Managementهای محصول نباید مستقیماً با UART، GPIO مربوط به EC200U، PWRKEY، Reset یا رجیسترها کار کنند.

### ۳.۵ عدم فرض اتصال دائمی

Component نباید طراحی خود را بر اساس «همیشه اینترنت داریم» بنا کند. قطع شبکه، ضعف سیگنال، تغییر اپراتور، قطع Data Connection، عدم دسترسی Server و Restart ماژول باید شرایط عادی قابل‌مدیریت باشند.

### ۳.۶ مصرف توان بخشی از طراحی است

Power Management نباید در انتهای پروژه به Component اضافه شود. برای هر قابلیت باید مشخص باشد که در چه سطحی از مصرف توان قابل ارائه است و چگونه از حالت کم‌مصرف به حالت فعال منتقل شده و مجدداً به حالت کم‌مصرف بازمی‌گردد.

### ۳.۷ عدم وابستگی به سخت‌افزار خاص

تا حد امکان Component باید از طریق Driver Layer با سخت‌افزار ارتباط داشته باشد تا تغییر UART، پایه‌ها یا حتی جایگزینی ماژول تا حد امکان باعث تغییر در منطق Component نشود.

### ۳.۸ قابلیت تست بدون EC200U واقعی

Component باید از ابتدا با Fake/Mock قابل تست باشد و اجرای Unit Test نباید به حضور فیزیکی EC200U وابسته باشد.

---

## ۴. محدوده پروژه

محدوده پروژه شامل طراحی، پیاده‌سازی، تست و تحویل یک EC200U Component کامل برای Firmware محصول است.

### ۴.۱ مدیریت خود ماژول

راه‌اندازی، تشخیص Ready بودن، مدیریت وضعیت ماژول، تشخیص عدم پاسخگویی (Timeout)، Recovery، درخواست Reset، مدیریت Startup و Shutdown در صورت نیاز.

### ۴.۲ موتور ارتباط AT

ارسال Command، دریافت Response، Parsing، مدیریت Timeout، مدیریت Commandهای همزمان/متوالی، مدیریت Error Response، مدیریت URC، تفکیک Response و URC.

### ۴.۳ ارتباط UART

دریافت و ارسال داده، Buffer Management، Interrupt/DMA در صورت نیاز، مدیریت Overflow، تشخیص خطا، ارتباط با UART Driver.

### ۴.۴ شبکه سلولار

SIM State، Network Registration، Signal Strength/Quality، Operator Information، Network Loss، Network Recovery، تغییر وضعیت اتصال.

### ۴.۵ ارتباط داده

Data Connection، اتصال و قطع، Reconnect، مدیریت خطا، Timeout، Retry، Recovery.

### ۴.۶ MQTT

Connect، Disconnect، Publish، Subscribe، Receive، Reconnect، Keep Alive، مدیریت خطا، مدیریت Session در حد موردنیاز محصول.

### ۴.۷ HTTP/HTTPS (در صورت استفاده محصول)

GET، POST، ارسال و دریافت داده، TLS/SSL، Certificate Management، Timeout، Retry، Recovery.

### ۴.۸ SMS

دریافت SMS، تشخیص Notification، خواندن پیام، ارسال SMS، مدیریت خطا، مدیریت وضعیت SMS Service.

### ۴.۹ تماس صوتی

تماس خروجی، تشخیص تماس ورودی، Answer، Hang-up، وضعیت تماس، Timeout، مدیریت خطا.

### ۴.۱۰ GNSS

Start، Stop، دریافت Position، Fix State، مدیریت Timeout، مدیریت خطای GNSS، ارائه آخرین وضعیت معتبر، مدیریت مصرف توان GNSS.

### ۴.۱۱ مدیریت مصرف توان

Component باید حداقل الگوهای زیر را پشتیبانی کند: Low Power، Normal/Active، Wake-up برای عملیات موردنیاز، بازگشت به Low Power.

نمونه:

```text
Low Power → GNSS → دریافت موقعیت → پایان GNSS → Low Power
```

یا:

```text
Low Power → Data Connection → MQTT Publish → Low Power
```

همچنین باید مشخص شود در Low Power کدام قابلیت‌ها می‌توانند همچنان فعال یا قابل دریافت باشند؛ از جمله SMS و تماس.

### ۴.۱۲ Fault و Recovery

Component باید خطاهای مهم را تشخیص داده و طبقه‌بندی کند، از جمله: UART Failure، AT Timeout، Invalid Response، Unexpected Response، SIM Failure، Network Failure، Data Connection Failure، MQTT Failure، GNSS Failure، Module Unresponsive.

Recovery باید تا حد امکان به‌صورت مرحله‌ای انجام شود:

```text
Retry → Local Recovery → Module Recovery → Reset Request
```

---

## ۵. اهداف و انتظارات پروژه

هدف اصلی، تحویل یک Component آماده Integration است، نه صرفاً یک Library از AT Commandها.

Component باید:

* پایدار، Non-Blocking و Stateful باشد.
* در برابر قطع و وصل شبکه مقاوم باشد.
* مصرف توان را مدیریت کند.
* قابلیت‌های SMS، Call، GNSS و Data را همزمان مدیریت کند.
* امکان Wake-up برای عملیات موقت را فراهم کند.
* Recovery مشخص داشته باشد و Faultهای مهم را گزارش کند.
* از Managementهای محصول مستقل باشد.
* تست‌پذیر باشد و دارای Contract مشخص باشد.
* مستندات Integration داشته باشد.
* روی برد واقعی EC200U تست شده باشد.

---

## ۶. خروجی‌های مورد انتظار پیمانکار

### خروجی نرم‌افزاری

EC200U Component Source Code، Header/API، Configuration، Driver Interface، Fake/Mock Interface، Test Code.

### خروجی فنی معماری

Component API Contract، State Model، Operation Model، Power Profile، Recovery Strategy، Fault Model، Timing/Timeout Model، Buffer Model، Memory Usage.

### خروجی تست

Unit Test، Component Test، تست با EC200U واقعی، تست قطع و وصل شبکه، تست SIM Failure، تست Signal ضعیف، تست Module Reset، تست MQTT، تست SMS، تست Call، تست GNSS، تست Low Power/Wake-up، تست ترکیبی قابلیت‌ها، تست پایداری طولانی‌مدت.

---

## ۷. معیار کلی پذیرش

Component زمانی قابل تحویل است که Firmware اصلی بتواند بدون دانستن جزئیات AT Command و رفتار داخلی EC200U، از طریق Contract تعریف‌شده قابلیت‌های موردنیاز را مصرف کند. همچنین تمام عملیات طولانی باید بدون Blocking شدن Super Loop اجرا شوند و رفتار Component در شرایط عادی، قطع شبکه، خطای ماژول و تغییر وضعیت توان قابل پیش‌بینی و قابل تست باشد.

**اصل نهایی پروژه:**

> EC200U Component باید پیچیدگی EC200U را پنهان کند، نه اینکه آن را به Managementهای Firmware منتقل کند.

---

## رابطه با سایر اسناد

این سند اسپک/SOW برون‌سپاری است (خروجی مورد انتظار از پیمانکار EC200U). Contract معماری سطح Application که Managementهای محصول با آن کار می‌کنند در `docs/03-infrastructure-contracts/ec200u-manager.md` تعریف شده و مرجع اصلی برای پیاده‌سازی Managementهاست، نه این سند.

> **منبع این فایل:** استخراج‌شده از فایل اصلی Word دریافتی از پیمانکار (فرمت باینری، غیرقابل‌خواندن مستقیم؛ متن با استخراج از XML داخلی بازسازی شد). فرمت‌بندی جزئی (مثل جدول‌بندی دقیق) ممکن است با نسخه اصلی docx تفاوت جزئی داشته باشد؛ محتوا کامل و مطابق منبع است.
