# پیوست فنی شماره ۶ — نقشه ردیابی و قراردادهای ارتباطی Firmware

## ۱. هدف

این پیوست، چارچوب ارتباطی Firmware و مسیر ردیابی مسئولیت‌ها را مشخص می‌کند. هدف آن این است که هر قابلیت سیستم از سطح رفتار محصول تا مالک اجرایی آن قابل ردیابی باشد:

```text
Capability / Requirement
        ↓
System Behavior
        ↓
Event / Data / Request / Fault
        ↓
Management / Provider
        ↓
Contract
        ↓
Implementation
```

این سند وارد جزئیات FSM داخلی، الگوریتم‌ها، Driverها یا Protocol Sequence نمی‌شود. این موارد در پیوست‌های فنی مربوط به هر Management مشخص خواهند شد.

---

## ۲. اصل معماری

Firmware از تعدادی Management مستقل تشکیل می‌شود. هر Management:

* مالک فرآیند خودش است.
* وضعیت داخلی خودش را نگه می‌دارد.
* مسئول اجرای عملیات خودش است.
* خطاهای خودش را تشخیص می‌دهد.
* Recovery خودش را انجام می‌دهد.
* در صورت نیاز درخواست Reset سطح سیستم می‌دهد.

هیچ Management نباید مستقیماً Management دیگری را فراخوانی کند.

---

## ۳. Super Loop به‌عنوان نقطه اجرای مشترک

تمام Managementهای Application توسط Super Loop فرصت اجرا دریافت می‌کنند:

```text
Super Loop
    │
    ├── Power Management
    ├── GPS Management
    ├── Server Management
    ├── SMS Management
    ├── Diagnostic Management
    ├── FOTA Management
    ├── System Management
    └── ...
```

Super Loop مسئول تصمیم‌گیری درباره منطق داخلی Managementها نیست. وظیفه آن فقط فراهم کردن فرصت اجراست: «برو و کارت را برس.»

هر Management باید Non-Blocking باشد و اجرای خود را در چند مرحله کوچک و قابل ادامه مدیریت کند.

---

## ۴. چهار مسیر اصلی ارتباط

تمام تعاملات Firmware تا حد امکان در چهار مفهوم اصلی قابل دسته‌بندی هستند:

```text
                Firmware Interaction
                        │
        ┌───────────────┼───────────────┐
        ▼               ▼               ▼
      Data            Event           Action
        │               │               │
 Provider/Latest      Policy      Request/Queue
        │               │               │
        └───────────────┴───────────────┘
                        │
                      Fault
                        │
                   Error Queue
```

---

## ۵. Data

Data برای مقادیری است که باید در اختیار مصرف‌کنندگان قرار گیرد. برای داده‌های زمان‌بر:

```text
Source
   ↓
Provider
   ↓
Latest Value
   ↓
Consumer
```

Provider مسئول Acquisition است. Consumer مسئول Acquisition نیست و نباید برای دریافت داده منتظر بماند.

مثال:

```text
GNSS
 ↓
GPS Provider
 ↓
Latest Position + Timestamp
 ↓
Server / Event Detection / سایر مصرف‌کنندگان
```

برای عملیات سریع و Non-Blocking، الزام به ایجاد Provider مستقل وجود ندارد.

---

## ۶. Event

Event بیانگر وقوع یک رخداد است.

```text
Detection
   ↓
Event
   ↓
Policy
```

Event مسئول اجرای عملیات نیست. مثال: `DoorOpened`, `VehicleStarted`, `VehicleStopped`, `ShockDetected`, `FotaCompleted`, `SmsReceived`.

Event فقط بیان می‌کند: «این اتفاق افتاده است.»

---

## ۷. Policy

Policy مشخص می‌کند که در مقابل یک Event چه اثراتی باید ایجاد شود. برای هر Event می‌توان یک کنترل ساده داشت که Effectهای موردنظر را مشخص کند. برای مثال:

```text
Event: VehicleStarted
Control Byte
┌──────┬─────┬────────┬───────┐
│ Call │ SMS │ Server │ Store │
└──────┴─────┴────────┴───────┘
```

در نتیجه تغییر رفتار Event تا حد امکان با تغییر Configuration انجام می‌شود، نه تغییر منطق اجرایی. Policy می‌تواند مشخص کند: به Server ارسال شود، SMS ارسال شود، تماس گرفته شود، در حافظه ذخیره شود، ترکیبی از موارد فوق، یا هیچ اقدامی انجام نشود.

---

## ۸. Request / Action

وقتی Policy انجام یک عملیات را تعیین کرد، عملیات به شکل Request بیان می‌شود.

```text
Policy
   ↓
Request
   ↓
Queue
   ↓
Owner Management
   ↓
Execution
```

مثال:

```text
DoorOpened
    ↓
Policy
 ├── Server Request
 ├── SMS Request
 └── Storage Request
```

هر Request متعلق به Management مالک آن عملیات است. Management ایجادکننده Request مسئول اجرای آن نیست.

---

## ۹. مسئولیت Owner Management

وقتی Request به Management مالک رسید، مسئولیت کامل عملیات به همان Management منتقل می‌شود. مثلاً:

```text
Send SMS Request
       ↓
SMS Management
 ├── Send
 ├── Timeout
 ├── Retry
 ├── Detect Failure
 ├── Local Recovery
 └── Finish
```

فرستنده Request نباید منتظر Result عمومی بماند. معماری Firmware یک Result Framework عمومی برای تمام عملیات تعریف نمی‌کند.

---

## ۱۰. Fault

هر Management مسئول خطاهای خودش است.

```text
Management
   ├── Detect Fault
   ├── Log Fault
   ├── Retry
   ├── Local Recovery
   │
   └── در صورت نیاز
          ↓
     Reset Request
```

خطا می‌تواند Timestamp داشته باشد و در Error Queue/Log قرار گیرد. ثبت خطای داخلی الزاماً به معنی ارسال آن به Server نیست.

---

## ۱۱. Recovery

Recovery تا حد امکان در همان Management انجام می‌شود.

مثال:

```text
SMS
 ↓
Send Failed
 ↓
Retry
 ↓
Retry
 ↓
Finish / Fault
```

یا:

```text
EC200U
 ↓
Unresponsive
 ↓
Re-init
 ↓
Module Reset
 ↓
Resume
```

یا:

```text
Diagnostic
 ↓
No Response
 ↓
Retry after interval
 ↓
Continue
```

Recovery مرکزی برای سرویس‌ها تعریف نمی‌شود.

---

## ۱۲. System Reset

System Reset تنها استثنای مهم در مالکیت Recovery است. هیچ Managementی مجاز به Reset مستقیم کل سیستم نیست.

```text
Management
   ↓
Reset Request
   ↓
System Management
   ↓
System Reset
```

System Management مسئول امکانات متمرکز سیستم از جمله: Watchdog، System Reset، Module Reset، LED/Status indication، Error Queue/Fault Log است. System Management مسئول تصمیم‌گیری درباره Recovery داخلی سرویس‌ها نیست.

---

## ۱۳. Contract عمومی Management

Contract تمام Managementها بر یک قالب مشترک استوار است:

```text
Management Contract
├── Process()
├── Input / Request
├── Data
├── Event
├── Fault
├── Power Mode Behavior
└── Reset Request
```

همه موارد برای تمام Managementها الزاماً وجود ندارند. Contract فقط آنچه را که Management از بیرون ارائه یا دریافت می‌کند مشخص می‌کند.

---

## ۱۴. Process Contract

هر Management باید دارای نقطه اجرای مشخص در Super Loop باشد: `Process()`.

الزامات:

* Non-Blocking
* عدم انتظار برای Hardware
* عدم انتظار برای Network
* عدم Delay
* حفظ State داخلی بین فراخوانی‌ها
* اجرای تدریجی عملیات‌های طولانی
* رعایت Power Mode

زمان‌بندی داخلی و سیاست اجرای Management متعلق به خود Management است. هیچ زمان عددی عمومی برای اجرای `Process()` تعیین نمی‌شود.

---

## ۱۵. Traceability Matrix

تمام قابلیت‌های Firmware باید در نهایت در جدولی مشابه زیر قابل ردیابی باشند:

| Capability / Behavior | Event / Data / Request | Owner | Recovery | External Effect |
|---|---|---|---|---|
| Door Open | DoorOpened | Event/Policy | — | SMS/Server/Store طبق Policy |
| Vehicle Start | VehicleStarted | Event/Policy | — | طبق Policy |
| GPS Position | Latest Value | GPS Provider | GPS Recovery | Server |
| SMS Sending | SMS Request | SMS Management | Retry / Re-init | SMS |
| Diagnostic | Diagnostic Request | Diagnostic Management | Retry / Recovery | Server در صورت Policy |
| Server Communication | Server Request | Server Management | Retry / Backoff | Server |
| FOTA | FOTA Request | FOTA Management | Retry / Reset | Firmware Update |
| System Fault | Fault | System Management | ثبت / Watchdog / Reset | — |

این جدول باید در زمان تکمیل معماری با تمام قابلیت‌های محصول تکمیل شود.

---

## ۱۶. مرز Contract و Implementation

Contract مشخص می‌کند:

* چه چیزی وارد Management می‌شود.
* چه چیزی از Management خارج می‌شود.
* چه Eventهایی قابل تولید است.
* چه Faultهایی قابل اعلام است.
* چه Dataهایی قابل مصرف است.
* چه محدودیت‌های اجرایی وجود دارد.
* مالکیت هر مسئولیت با چه کسی است.

Contract مشخص نمی‌کند: FSM داخلی، Stateهای داخلی، الگوریتم Retry، Timeout داخلی، Protocol Sequence، Driver Implementation، Register Access، ساختار داخلی Memory، جزئیات EC200U.

این موارد در سطح Implementation یا پیوست فنی همان Management تعریف می‌شوند.

---

## ۱۷. اصل حداقل‌گرایی معماری

برای جلوگیری از رشد بی‌دلیل Architecture، مفاهیم جدید فقط زمانی ایجاد می‌شوند که یک نیاز واقعی داشته باشند. در نتیجه:

* Signal Bus عمومی نداریم.
* Result Manager عمومی نداریم.
* Health Manager مستقل نداریم.
* Recovery Manager مستقل نداریم.
* Managementها مستقیماً یکدیگر را فراخوانی نمی‌کنند.

هر تعامل باید در یکی از مفاهیم زیر قابل توضیح باشد: Data، Event، Request، Fault، Reset Request. اگر یک ارتباط را نمی‌توان به یکی از این مفاهیم نسبت داد، ابتدا باید ضرورت آن بررسی شود و صرفاً برای زیبایی معماری abstraction جدید ایجاد نشود.

---

## ۱۸. رابطه با پیوست‌های فنی بعدی

این سند نقشه سطح معماری است. پس از تثبیت آن، برای هر Management یک Technical Contract مستقل تهیه می‌شود:

```text
Architecture
    │
    ├── Management Contract
    │
    ├── Technical Contract
    │      ├── Inputs
    │      ├── Requests
    │      ├── Data
    │      ├── Events
    │      ├── Faults
    │      └── Constraints
    │
    └── Implementation
           ├── FSM
           ├── Algorithms
           ├── Drivers
           └── Protocol Details
```

بنابراین ابتدا ردیابی معماری تثبیت می‌شود و سپس جزئیات فنی هر Management در پیوست مربوطه تکمیل خواهد شد.

---

## ۱۹. اصل نهایی

هر Management مستقل است، مالک مسئولیت خودش است و فقط از طریق Contractهای مشخص با سایر اجزای سیستم تعامل می‌کند. Data به‌صورت آماده مصرف می‌شود، Event وقوع را اعلام می‌کند، Policy اثر Event را تعیین می‌کند، Request عملیات را منتقل می‌کند، Queue کار منتظر اجرا را نگه می‌دارد و Fault توسط مالک همان سرویس مدیریت می‌شود. System Reset تنها اختیار متمرکز سیستم است.

این ساختار عمداً حداقلی نگه داشته شده است تا معماری به جای ایجاد لایه‌های انتزاعی اضافی، مستقیماً قابل تبدیل به Firmware واقعی باشد.

---

## منبع این فایل

استخراج‌شده از یادداشت‌های فنی اولیه معماری (فرمت باینری Word؛ متن با استخراج از XML داخلی بازسازی شد).
