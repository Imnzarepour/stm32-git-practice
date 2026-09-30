# معرفی کلی پروژه و معماری Firmware

## 1. معرفی پروژه

این پروژه با هدف توسعه Firmware یک **واحد کنترل و ارتباطات خودرویی (TCU)** انجام می‌شود؛ واحدی که در خودرو نصب شده و وظیفه ارتباط با سامانه‌های داخلی خودرو، دریافت و پردازش اطلاعات، ارتباطات بی‌سیم، مدیریت توان، اجرای عملیات کنترلی و ارسال اطلاعات موردنیاز به Backend را بر عهده دارد.

این Firmware باید در شرایط واقعی خودرو و در یک بازه زمانی طولانی و بدون دسترسی مستقیم تیم توسعه به دستگاه نصب‌شده کار کند. بنابراین معماری آن باید علاوه بر انجام قابلیت‌های محصول، در برابر خطاهای ارتباطی، قطع شبکه، عدم دسترسی GNSS، خطاهای حافظه، اختلال تجهیزات جانبی و شرایط متغیر توان مقاوم باشد.

محصول از چند حوزه عملکردی اصلی تشکیل می‌شود:

* ارتباط و تشخیص خودرو
* ارتباط Cellular و Server
* GNSS و موقعیت‌یابی
* SMS و تماس
* مدیریت توان
* مدیریت حافظه و داده
* FOTA و به‌روزرسانی Firmware
* کنترل تجهیزات و Actuatorها
* ثبت خطا و وضعیت سیستم
* نمایش وضعیت توسط LEDها
* زیرساخت‌های Firmware شامل Bootloader، Watchdog، Reset و Version Management

هدف معماری این است که این قابلیت‌ها بتوانند **مستقل از یکدیگر توسعه، تست و نگهداری شوند** و خرابی یک قابلیت تا حد امکان باعث از کار افتادن کل دستگاه نشود.

---

# 2. معماری کلان

Firmware از یک Application مرکزی تشکیل می‌شود که قابلیت‌های مختلف آن به‌صورت Managementهای مستقل سازمان‌دهی شده‌اند.

```text
                     RESET / POWER ON
                            │
                            ▼
                       BOOTLOADER
                            │
                            ▼
                    APPLICATION STARTUP
                            │
                            ▼
                       SUPER LOOP
                            │
        ┌───────────────────┼────────────────────┐
        │                   │                    │
        ▼                   ▼                    ▼
   Power Management    GPS Management      Server Management
        │                   │                    │
        ▼                   ▼                    ▼
   SMS Management     Diagnostic Mgmt      FOTA Management
        │                   │                    │
        └───────────────────┼────────────────────┘
                            │
                     System Management
                            │
                 ┌──────────┼──────────┐
                 ▼          ▼          ▼
              Watchdog     LED        Reset
```

این اجزا از طریق یک **Super Loop غیرمسدودکننده و رویدادمحور** اجرا می‌شوند.

---

# 3. فلسفه اصلی اجرا

معماری Firmware بر مبنای:

> **Cooperative Non-Blocking Event-Driven Super Loop**

طراحی شده است.

Super Loop به هر Management فرصت اجرا می‌دهد، اما مسئول مدیریت فرآیند داخلی آن نیست.

```text
Super Loop
     │
     ├── Process()
     ├── Process()
     ├── Process()
     └── Process()
```

هر Management خودش تصمیم می‌گیرد:

* آیا کاری دارد؟
* در چه Stateای قرار دارد؟
* چه زمانی باید اجرا شود؟
* آیا باید Retry کند؟
* آیا Timeout شده است؟
* آیا Recovery لازم است؟
* آیا در Power Mode فعلی باید فعال باشد؟

هیچ Management نباید با `Delay`، انتظار برای Network، انتظار برای Hardware یا Loop قفل‌شونده، اجرای Super Loop را متوقف کند.

---

# 4. ساختار ارتباطات داخلی

ارتباط بین اجزای Firmware به‌صورت مستقیم و وابسته به یکدیگر طراحی نمی‌شود.

به‌خصوص:

```text
GPS Management ──X──→ Server Management
SMS Management ──X──→ Power Management
Diagnostic ──────X──→ SMS Management
```

هر Management مالک فرآیند خودش است.

تعاملات در چهار مفهوم اصلی سازمان‌دهی می‌شوند:

```text
Data
Event
Request
Fault
```

و Reset کل سیستم یک مسیر ویژه دارد:

```text
Reset Request → System Management → System Reset
```

---

# 5. Data و Provider

وقتی دریافت یک داده زمان‌بر است، Acquisition از مصرف داده جدا می‌شود.

```text
Source
   ↓
Provider
   ↓
Latest Value + Timestamp
   ↓
Consumer
```

Provider داده را در زمان مناسب دریافت و آماده می‌کند.

Consumer برای دریافت داده نباید باعث شروع یک عملیات زمان‌بر شود.

برای مثال:

```text
GNSS
 ↓
GPS Provider
 ↓
Latest Position
 ↓
Server / Event Detection / سایر مصرف‌کنندگان
```

اگر عملیات دریافت داده سریع و Non-Blocking باشد، الزام به ایجاد Provider مستقل وجود ندارد.

---

# 6. Event و Policy

Event بیانگر وقوع یک رخداد است.

نمونه‌ها:

```text
DoorOpened
VehicleStarted
VehicleStopped
ShockDetected
FotaCompleted
SmsReceived
```

Event خودش عملیاتی انجام نمی‌دهد.

پس از ایجاد Event، Policy تعیین می‌کند که چه اثری باید ایجاد شود.

برای کاهش تغییرات نرم‌افزاری، رفتار Eventها می‌تواند توسط یک Control Byte یا Configuration Matrix تعیین شود:

```text
Event
  ↓
Control Byte
  ├── Call
  ├── SMS
  ├── Server
  └── Store
```

بنابراین تغییر رفتار یک Event تا حد امکان با تغییر Configuration انجام می‌شود و نیاز به تغییر منطق اجرایی کاهش پیدا می‌کند.

---

# 7. Request و Queue

وقتی Policy انجام یک عملیات را تعیین می‌کند، یک Request ایجاد می‌شود.

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

مثلاً:

```text
DoorOpened
    ↓
Policy
 ├── Server Request
 ├── SMS Request
 └── Storage Request
```

هر Queue مسئول نگهداری کارهای منتظر اجراست و Management مالک عملیات، اجرای آن را بر عهده دارد.

Queue مستقیماً Management دیگری را اجرا نمی‌کند.

---

# 8. مسئولیت Management

اصل اساسی این است:

> **هر Management مسئولیت خودش را تا انتها می‌بندد.**

مثلاً SMS Management:

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

فرستنده Request نباید درگیر این جزئیات شود و Result عمومی برای تمام عملیات‌های Firmware وجود ندارد.

به همین ترتیب Diagnostic، GPS، Server، FOTA و سایر Managementها مالک کامل فرآیند داخلی خود هستند.

---

# 9. Fault و Recovery

خطاهای داخلی توسط همان Managementی که مالک آن قابلیت است تشخیص داده می‌شوند.

```text
Management
   ├── Detect Fault
   ├── Log Fault
   ├── Retry
   └── Local Recovery
```

ثبت خطا با Timestamp می‌تواند در Error Queue/Log انجام شود.

اما ثبت خطای داخلی به معنی ارسال آن به Server نیست.

Server محل ثبت همه جزئیات داخلی Firmware محسوب نمی‌شود.

Recovery نیز تا حد امکان محلی است.

مثلاً:

```text
SMS بدون اعتبار
→ Retry معنی ندارد
→ Fault / پایان

EC200U Unresponsive
→ Re-init / Module Reset

MCU Hang
→ Watchdog / System Reset
```

---

# 10. System Management

برای جلوگیری از پراکنده شدن مسئولیت‌های سیستمی، تعدادی قابلیت عمومی در System Management متمرکز می‌شوند:

```text
System Management
├── LED / Status
├── Error / Fault Queue
├── Fault Log
├── Watchdog
├── Module Reset
└── System Reset
```

اما System Management مسئول Recovery داخلی سرویس‌ها نیست.

مثلاً تصمیم اینکه SMS چند بار Retry شود متعلق به SMS Management است.

---

# 11. Reset

Reset کل سیستم یک اختیار متمرکز است.

هیچ Managementی نباید مستقیماً MCU را Reset کند.

```text
Management
      ↓
Reset Request
      ↓
System Management
      ↓
System Reset
```

این محدودیت از ایجاد Resetهای پراکنده و رفتارهای غیرقابل‌کنترل جلوگیری می‌کند.

---

# 12. Power Management

Firmware دارای دو وضعیت اصلی توان است:

```text
NORMAL / FULL ACTIVITY
          ↕
LOW POWER / REDUCED ACTIVITY
```

ACC مهم‌ترین عامل تعیین‌کننده وضعیت است.

در `NORMAL`، قابلیت‌های سیستم با فعالیت کامل اجرا می‌شوند.

در `LOW POWER`، سیستم همچنان قابلیت‌های اصلی را حفظ می‌کند اما:

* فعالیت CPU کاهش می‌یابد.
* دسترسی‌های غیرضروری به Memory کاهش می‌یابد.
* فعالیت Cellular/GNSS تا حد امکان کاهش می‌یابد.
* تعداد عملیات‌ها کاهش می‌یابد.
* LEDها و خروجی‌های غیرضروری محدود می‌شوند.

Power Manager فقط وضعیت کلی Power Mode را مشخص می‌کند.

هر Management مسئول تصمیم‌گیری درباره رفتار خودش در آن Mode است.

---

# 13. GNSS

GNSS دو رفتار اصلی دارد.

### Vehicle ON

هدف، Tracking مسیر خودرو است.

نمونه سیاست محصول:

```text
تقریباً 6 نقطه در دقیقه
≈ یک نقطه هر 10 ثانیه
```

Firmware می‌تواند چند نمونه GNSS دریافت کند و بر اساس کیفیت، نمونه مناسب را انتخاب کند.

### Vehicle OFF

هدف Tracking مداوم نیست؛ هدف تشخیص اینکه خودرو در محل باقی مانده یا حرکت کرده است.

الگوی اولیه:

```text
Attempt
 ↓
~5 min
 ↓
Failure
 ↓
~10 min
 ↓
Failure
 ↓
~20 min
```

در صورت مشاهده تغییر معنی‌دار موقعیت:

```text
Low Power Monitoring
        ↓
Significant Movement?
        ↓
Verification
        ↓
GNSS Active
        ↓
Movement Confirmed
        ↓
Tracking
```

عدم دریافت Fix الزاماً Failure سیستم محسوب نمی‌شود و می‌تواند یک وضعیت عادی محیطی باشد.

---

# 14. Server Communication

ارتباط Server دو مسیر دارد:

### Outbound

```text
Event / Telemetry
       ↓
Queue
       ↓
Server Management
       ↓
Network
       ↓
Server
```

Event Queue نسبت به Telemetry اولویت بالاتری دارد.

### Inbound

```text
Server
   ↓
Server Management
   ↓
Command / Request
   ↓
Owner Management
   ↓
Execution
```

Server Management مسئول اجرای Command نیست؛ فقط دریافت، اعتبارسنجی و تحویل آن را انجام می‌دهد.

در صورت نیاز به گزارش نتیجه، آن نتیجه از مسیر Event/Telemetry مناسب خودش ایجاد می‌شود.

---

# 15. Event Queue و Telemetry Queue

دو مفهوم Queue مستقل وجود دارد:

```text
Event Queue
Telemetry Queue
```

Eventها اولویت بالاتری دارند.

Telemetry شامل داده‌هایی مانند:

* GPS
* Battery
* Diagnostic
* سایر Measurementها

است.

در صورت پر شدن Queue:

> **قدیمی‌ترین داده حذف و داده جدید وارد می‌شود.**

این سیاست به‌خصوص برای Telemetry باعث می‌شود سیستم بیشتر روی وضعیت جدید متمرکز باشد.

برای Telemetry، ظرفیت Queue بهتر است بر اساس حجم داده نیز در نظر گرفته شود، نه فقط تعداد Itemها.

---

# 16. Boot و Startup

پس از Power-On یا Reset:

```text
RESET
 ↓
BOOTLOADER
 ↓
Firmware Validation
 ↓
Application
 ↓
Startup
 ↓
Hardware Init
 ↓
EC200U / SIM Init
 ↓
EEPROM Load
 ↓
Required Files
 ↓
ACC Check
 ↓
Power Mode
 ↓
SUPER LOOP
```

Bootloader مستقل از Application است.

در صورت وجود Update:

```text
Validate Update
      ↓
Program
      ↓
Verify
      ↓
OK → Application

FAIL
 ↓
RESET
 ↓
BOOTLOADER
```

هر Failure در Startup تا حد امکان به Capability مربوط به خودش محدود می‌شود.

برای مثال:

```text
EC200U / SIM Failure
→ Communication Capability unavailable

NOR Failure
→ Diagnostic unavailable

EEPROM Failure
→ Serial Number / Version / Key unavailable
```

خرابی یک Capability الزاماً باعث توقف کل Firmware نمی‌شود.

---

# 17. Diagnostic

Diagnostic یک Management مستقل است.

هدف معماری این است که:

* CAN پشتیبانی شود.
* معماری K-Line نیز حفظ شود.
* عملیات Non-Blocking باشد.
* هیچ Loop قفل‌شونده‌ای وجود نداشته باشد.
* Diagnostic بتواند فرآیندهای طولانی خود را در چند مرحله اجرا کند.

جزئیات FSM، Protocol Sequence، Timeout و Driver داخل Contract عمومی محصول تحمیل نمی‌شود و متعلق به تیم Diagnostic است.

---

# 18. EC200U

ارتباط Firmware با EC200U از طریق یک Boundary مشخص انجام می‌شود.

```text
Application Firmware
 ├── GPS Management
 ├── Server Management
 ├── SMS Management
 └── Power Management
             │
             ▼
       EC200U Manager
             │
             ▼
          EC200U
```

EC200U Manager لایه‌ای است که جزئیات:

* AT Command
* PDP
* Socket
* GNSS
* Modem Timing
* Retry
* Recovery
* Buffer Management

را از Application پنهان می‌کند.

پیاده‌سازی داخلی EC200U Manager می‌تواند توسط پیمانکار انجام شود، ولی Contract آن باید توسط پروژه مشخص و کنترل شود.

---

# 19. FOTA

FOTA بخشی از زنجیره Boot و Application است.

در صورت شکست Programming یا Verification:

```text
FOTA Failure
     ↓
Reset
     ↓
Bootloader
     ↓
Validation
     ↓
Retry / Recovery
```

در صورت امکان، معماری Dual Bank برای افزایش ایمنی Update ترجیح داده می‌شود، هرچند محدودیت ظرفیت Flash باید در طراحی نهایی لحاظ شود.

---

# 20. Actuator

Actuatorها مانند Relay و کنترل تجهیزات، در این معماری عمدتاً **Command Executor** هستند.

```text
Request
   ↓
Actuator Management
   ↓
Execute
```

Actuator الزاماً FSM پیچیده یا Feedback Loop مستقل ندارد، مگر اینکه یک محصول مشخص نیاز واقعی به آن ایجاد کند.

---

# 21. Memory و Persistence

Memory بر اساس نوع داده و نیاز محصول استفاده می‌شود.

دو مفهوم از یکدیگر جدا هستند:

### Operational Data

داده موردنیاز برای عملکرد جاری محصول.

### Evidence / Fault Data

داده‌ای که برای ثبت خطا یا نگهداری یک رخداد مهم لازم است.

Firmware به‌صورت پیش‌فرض Local Logging عمومی ندارد.

Policy تعیین می‌کند که یک Event:

* به Server ارسال شود.
* SMS ایجاد کند.
* تماس ایجاد کند.
* در حافظه ذخیره شود.
* یا ترکیبی از این موارد باشد.

بنابراین Persistence یک رفتار Policy-driven است، نه یک Logging عمومی برای تمام اتفاقات داخلی.

---

# 22. Interface / Contract

Contract هر Management حداقلی است:

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

تمام موارد برای همه Managementها الزاماً وجود ندارند.

Contract، مرز مسئولیت را مشخص می‌کند اما وارد پیاده‌سازی داخلی نمی‌شود.

مواردی مانند:

* FSM
* Stateهای داخلی
* Retry Algorithm
* Timeout
* Protocol Sequence
* Driver
* Register Access

در اختیار مالک همان Management باقی می‌مانند.

---

# 23. Traceability

هر قابلیت باید از رفتار محصول تا پیاده‌سازی قابل ردیابی باشد:

```text
Requirement
    ↓
Capability
    ↓
Behavior
    ↓
Event / Data / Request / Fault
    ↓
Owner Management
    ↓
Contract
    ↓
Implementation
    ↓
Test
```

این Traceability مبنای توسعه، تست، تحویل و نگهداری Firmware خواهد بود.

---

# 24. اصول کلیدی معماری

معماری Firmware بر اصول زیر استوار است:

1. **Non-Blocking by Design**
2. **Management استقلال دارد.**
3. **هر Management مالک فرآیند خودش است.**
4. **Data از Action جداست.**
5. **Event وقوع را بیان می‌کند؛ Policy اثر آن را تعیین می‌کند.**
6. **Request عملیات را منتقل می‌کند؛ Queue آن را نگه می‌دارد.**
7. **هر Management خطای خودش را مدیریت می‌کند.**
8. **Recovery تا حد امکان محلی است.**
9. **System Reset فقط یک مالک دارد.**
10. **Provider داده‌های زمان‌بر را از مصرف‌کننده جدا می‌کند.**
11. **Power Mode یک وضعیت مشترک است، نه یک FSM مرکزی پیچیده.**
12. **جزئیات داخلی Managementها در Contract تحمیل نمی‌شود.**
13. **Local Logging عمومی وجود ندارد؛ Persistence بر اساس Policy است.**
14. **از Abstractionهای بدون نیاز واقعی اجتناب می‌شود.**
15. **تمام اجزا باید در Super Loop قابل اجرا باشند.**

---

# 25. تصویر نهایی معماری

```text
                         ┌──────────────┐
                         │  BOOTLOADER  │
                         └──────┬───────┘
                                │
                         APPLICATION
                                │
                         ┌──────▼───────┐
                         │  SUPER LOOP  │
                         └──────┬───────┘
                                │
       ┌────────────────────────┼────────────────────────┐
       │                        │                        │
       ▼                        ▼                        ▼
 Managements               Providers              System Mgmt
       │                        │                        │
       │                        │                  ┌─────┼─────┐
       │                        │                  │     │     │
       │                        │               Fault  WDG  Reset
       │                        │
       ├──────── Event ──→ Policy
       │                    │
       │                    └──→ Requests → Queues
       │
       ├──────── Fault ──→ Error Queue / Log
       │
       └──────── Reset Request ──→ System Management

Data:
Source → Provider → Latest Value → Consumer

External Boundary:
Application → EC200U Manager → EC200U

External Communication:
Event / Telemetry → Server Queue → Server Management → Network → Server
```

## جمع‌بندی

این Firmware به‌عنوان مجموعه‌ای از سرویس‌های مستقل و قابل‌کنترل طراحی می‌شود که روی یک **Cooperative Non-Blocking Super Loop** اجرا می‌شوند.

معماری تلاش نمی‌کند تمام رفتار سیستم را در یک FSM یا Manager مرکزی متمرکز کند. برعکس، مسئولیت‌ها تا حد امکان به مالک واقعی آن‌ها سپرده می‌شوند:

> **Management مسئول کار خودش است، Provider مسئول آماده‌سازی داده است، Policy مسئول تعیین اثر Event است، Queue مسئول نگهداری کار است، System Management مسئول امکانات عمومی سیستم و Reset است و Super Loop فقط فرصت اجرا را فراهم می‌کند.**

هدف نهایی این ساختار، ایجاد Firmwareای است که در عین داشتن قابلیت‌های متعدد، **قابل فهم، قابل تست، قابل ردیابی، قابل توسعه و قابل نگهداری در طول عمر واقعی محصول** باقی بماند.
