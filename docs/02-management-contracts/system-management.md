# پیوست فنی — تعریف مسئولیت، ورودی و خروجی System Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها و خروجی‌های **System Management** در معماری Firmware دستگاه TCU را مشخص می‌کند.

هدف اصلی، ایجاد یک مرز روشن میان System Management و سایر Managementها است؛ به‌گونه‌ای که مسئولیت‌های هر بخش قابل تفکیک بوده و پیاده‌سازی اجزای Firmware به‌صورت مستقل و موازی امکان‌پذیر باشد.

این سند در سطح معماری و Contract تهیه شده و وارد جزئیات پیاده‌سازی داخلی، ماشین حالت، الگوریتم‌های Recovery و Driverهای سخت‌افزاری نمی‌شود.

---

## ۲. جایگاه System Management

System Management یکی از Managementهای اصلی Firmware است و مسئولیت‌های **سراسری و حاکمیتی سیستم** را بر عهده دارد.

این Management نباید مالک منطق داخلی سایر سرویس‌ها باشد و نباید در فرآیند Recovery اختصاصی آن‌ها دخالت کند.

اصل حاکم:

> **هر Management مسئول حل مشکل خودش است؛ System Management تنها مرجع اجرای Reset در سطح سیستم یا Module است.**

---

# ۳. مسئولیت‌ها

## ۳-۱. مدیریت System Reset

System Management تنها بخش مجاز برای اجرای **System Reset** است.

سایر Managementها در صورت نیاز می‌توانند درخواست Reset صادر کنند، اما اجرای Reset مستقیماً توسط آن‌ها مجاز نیست.

```text
Management
     │
     │ Reset Request
     ▼
System Management
     │
     ▼
System Reset
```

---

## ۳-۲. مدیریت Module Reset

System Management مرجع متمرکز اجرای Reset برای Moduleهای قابل Reset، مانند EC200U، است.

```text
Management
     │
     │ Module Reset Request
     ▼
System Management
     │
     ▼
Module Reset
```

تشخیص اینکه یک Module چه زمانی نیاز به Recovery یا Reset دارد، بر عهده Management مالک آن Module یا Capability مربوطه است.

---

## ۳-۳. مدیریت Watchdog

System Management مسئول مدیریت Watchdog سخت‌افزاری سیستم است.

این مسئولیت شامل تغذیه صحیح و دوره‌ای Watchdog در شرایطی است که سیستم در وضعیت اجرای عادی قرار دارد.

هدف:

* جلوگیری از باقی ماندن سیستم در وضعیت Freeze یا Lock
* ایجاد مکانیزم نهایی Recovery در صورت توقف غیرقابل‌بازگشت اجرای Firmware

جزئیات تشخیص سلامت هر Management و Recovery محلی آن‌ها خارج از مسئولیت System Management است.

---

## ۳-۴. مدیریت نشانگرهای محلی

System Management مسئول کنترل نشانگرهای محلی دستگاه است، از جمله:

* LEDهای وضعیت
* Buzzer

نمونه وضعیت‌هایی که می‌توانند در این لایه نمایش داده شوند:

* وضعیت Power
* وضعیت Cellular / Network
* وضعیت GPS
* وضعیت SIM

System Management مسئول **نمایش وضعیت** است و مالک منطق داخلی GPS، شبکه، SIM یا Power نیست.

---

## ۳-۵. مدیریت Faultهای سیستم

System Management مسئول مدیریت متمرکز Faultهای قابل مشاهده در سطح سیستم است.

این مسئولیت شامل:

* دریافت Faultهای اعلام‌شده توسط Managementها
* نگهداری وضعیت Faultها در صورت نیاز
* مدیریت Fault Queue
* فراهم کردن اطلاعات Fault برای مصرف‌کنندگان مجاز
* ایجاد Fault Log در صورت وجود Policy و زیرساخت Persistence مربوطه

### نکته

ثبت محلی همه Faultها به‌صورت پیش‌فرض الزامی نیست.

در صورتی که یک Fault نیازمند Persistence باشد، نحوه ذخیره‌سازی آن مطابق Policy و زیرساخت Memory تعیین خواهد شد.

بنابراین System Management مستقیماً مالک پیاده‌سازی Memory نیست.

---

# ۴. محدودیت‌های مسئولیت

System Management **مسئول Recovery اختصاصی سایر Managementها نیست.**

برای مثال، در صورت عدم دریافت پاسخ Diagnostic:

```text
Diagnostic
    ↓
Retry / Local Recovery
    ↓
Failure
    ↓
Reset Request
    ↓
System Management
    ↓
Reset
```

System Management نباید خود تصمیم بگیرد که Diagnostic چند بار Retry کند یا چه فرآیند Recoveryای انجام دهد.

همین اصل برای GPS، Server، SMS، Firmware Update و سایر Managementها نیز برقرار است.

---

# ۵. ورودی‌ها

## ۵-۱. Reset Request

درخواست Reset از سایر Managementها.

انواع درخواست می‌تواند شامل:

* System Reset Request
* Module Reset Request

باشد.

System Management پس از دریافت درخواست، مسئول اجرای Reset مطابق Contract سیستم است.

---

## ۵-۲. Fault Data

Faultهای اعلام‌شده توسط سایر Managementها.

اطلاعات Fault می‌تواند شامل مواردی مانند:

* Fault Type
* Source
* Timestamp
* اطلاعات تکمیلی مورد نیاز

باشد.

System Management مسئول ثبت و مدیریت این اطلاعات در سطح سیستم است.

---

## ۵-۳. Status Data

وضعیت‌های مورد نیاز برای نمایش وضعیت سیستم روی LED یا Buzzer.

برای مثال:

* وضعیت شبکه
* وضعیت GPS
* وضعیت SIM
* وضعیت Power

این اطلاعات به‌صورت وضعیت/داده از Management مالک خود دریافت می‌شوند.

---

## ۵-۴. System Time

System Management می‌تواند از Time Base مشترک سیستم برای مواردی مانند:

* زمان‌بندی LED
* الگوهای چشمک‌زدن
* مدیریت زمان‌بندی داخلی مرتبط با مسئولیت‌های خودش

استفاده کند.

Time Base خود یک Infrastructure مشترک است و Management مستقلی محسوب نمی‌شود.

---

# ۶. خروجی‌ها

## ۶-۱. System Reset

فرمان یا فعال‌سازی سخت‌افزاری Reset برای MCU و راه‌اندازی مجدد کل سیستم.

---

## ۶-۲. Module Reset

فرمان یا فعال‌سازی مسیر سخت‌افزاری Reset برای Module مورد نظر، مانند EC200U.

---

## ۶-۳. LED / Buzzer Control

کنترل فیزیکی:

* LEDها
* Buzzer

مطابق وضعیت‌هایی که باید به کاربر یا سرویس‌دهنده نمایش داده شوند.

---

## ۶-۴. Fault Status / Fault Data

اطلاعات Faultهای مدیریت‌شده در سطح سیستم برای استفاده توسط بخش‌های مجاز سیستم.

در صورت نیاز، این اطلاعات می‌تواند برای:

* عیب‌یابی
* نمایش وضعیت
* ارسال به Server
* Persistence

در اختیار مسیرهای مربوطه قرار گیرد.

---

# ۷. مدل ارتباطی

ارتباط System Management با سایر Managementها مطابق مدل عمومی معماری Firmware انجام می‌شود.

```text
             ┌─────────────────────┐
             │ System Management   │
             └─────────┬───────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼
 Reset Request       Fault          Status
        │              │              │
        ▼              ▼              ▼
 System / Module    Fault Queue     LED/Buzzer
     Reset             /Log          Control
```

System Management نباید برای انجام مسئولیت‌های خود، مستقیماً وارد منطق داخلی سایر Managementها شود.

---

# ۸. Contract سطح معماری

```text
System Management

Responsibility:
    System Reset
    Module Reset
    Watchdog
    LED / Buzzer
    System Fault Management

Inputs:
    Reset Request
    Fault Data
    Status Data
    System Time

Outputs:
    System Reset
    Module Reset
    LED / Buzzer Control
    Fault Status / Fault Data

Constraints:
    Non-Blocking
    Super Loop Compatible
    No direct Management-to-Management control
    No ownership of other Management Recovery
    No direct ownership of Memory implementation
```

---

# ۹. مرز با سایر اجزا

| جزء                   | مسئولیت System Management                          |
| --------------------- | -------------------------------------------------- |
| Power Management      | دریافت وضعیت مورد نیاز؛ عدم مالکیت منطق Power      |
| GPS Management        | دریافت وضعیت برای نمایش؛ عدم مالکیت GPS            |
| Server Management     | دریافت Fault/Status؛ عدم مالکیت ارتباط Server      |
| Diagnostic Management | دریافت Fault/Status؛ عدم مالکیت Diagnostic         |
| EC200U Manager        | درخواست Module Reset؛ عدم مالکیت منطق داخلی EC200U |
| Memory Infrastructure | استفاده در صورت نیاز؛ عدم مالکیت Storage           |
| Bootloader            | مستقل از System Management                         |
| Watchdog Hardware     | System Management مالک مدیریت آن                   |
| LED / Buzzer Hardware | System Management مالک کنترل آن                    |

---

# ۱۰. اصل نهایی

System Management **مدیر کل منطق Firmware نیست**.

این بخش صرفاً مالک قابلیت‌هایی است که ذاتاً در سطح کل سیستم باید یک صاحب واحد داشته باشند:

> **Reset، Watchdog، وضعیت محلی و مدیریت متمرکز Faultهای سیستم.**

در مقابل:

> **Recovery و منطق عملیاتی هر Capability بر عهده Management مالک همان Capability است.**

این تفکیک، امکان توسعه مستقل Managementها و جایگزینی موقت وابستگی‌های در دسترس‌نبودنی با Fake/Stub را بدون ایجاد وابستگی متقابل فراهم می‌کند.
