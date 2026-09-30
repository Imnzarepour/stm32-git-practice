پیوست فنی شماره ۱
معماری ماشین‌های حالت Firmware
1. هدف
این پیوست، ماشین‌های حالت مورد نیاز در Firmware دستگاه TCU و مرز مسئولیت هر ماشین حالت را در سطح معماری مشخص می‌کند.
هدف این سند، تعیین جزئیات پیاده‌سازی FSMها نیست؛ بلکه مشخص کردن موارد زیر است:
•	چه فرآیندهایی واقعاً نیازمند ماشین حالت هستند.
•	مالک هر ماشین حالت کدام بخش Firmware است.
•	حالت‌های اصلی هر فرآیند چیست.
•	چه رویدادها یا شرایطی باعث تغییر حالت می‌شوند.
•	چه بخش‌هایی نباید در FSM قرار گیرند.
•	مرز میان Firmware داخلی و EC200U Manager برون‌سپاری‌شده کجاست.
اصل معماری این است که یک FSM مرکزی برای کل Firmware وجود ندارد. هر فرآیند پیچیده، چندمرحله‌ای و غیرهمزمان، در Component مالک خود ماشین حالت محلی دارد.
________________________________________
2. مدل اجرای Firmware
اجرای Firmware بر مبنای:
Cooperative Non-Blocking Event-Driven Super Loop
است.
ساختار کلی:
Super Loop
    │
    ├── Input & Request Management
    ├── GPS Management
    ├── Diagnostic Management
    ├── Server Communication Management
    ├── SMS Management
    ├── Power Management
    └── Health & Recovery
هر Component در هر بار اجرای Process():
1.	وضعیت فعلی خود را بررسی می‌کند.
2.	وجود کار را بررسی می‌کند.
3.	حداکثر یک گام کوچک از فرآیند را انجام می‌دهد.
4.	State مورد نیاز را حفظ می‌کند.
5.	سریعاً کنترل را به Super Loop بازمی‌گرداند.
هیچ FSM داخلی نباید باعث توقف Super Loop شود.
________________________________________
3. اصل Non-Blocking
تمام FSMها باید به‌صورت Non-Blocking طراحی شوند.
موارد زیر در معماری مجاز نیستند:
•	delay()
•	انتظار حلقه‌ای برای پاسخ سخت‌افزار
•	انتظار حلقه‌ای برای Timeout
•	Busy Wait
•	انتظار برای شبکه
•	انتظار برای GNSS Fix
•	انتظار برای پاسخ CAN
•	اجرای عملیات طولانی در ISR
•	اجرای عملیات اصلی در Timer Callback
به‌جای انتظار، FSM باید:
State
  ↓
Request / Start Operation
  ↓
Return
  ↓
Super Loop
  ↓
Check Result / Timeout
  ↓
Next State
را دنبال کند.
________________________________________
4. نقش Tick / Time Base
Tick یا Time Base یک زیرساخت مشترک Firmware است و خود آن یک Task یا FSM مستقل محسوب نمی‌شود.
FSMها از زمان برای تصمیم‌گیری استفاده می‌کنند، برای مثال:
if (GetTick() >= deadline)
اما Timer نباید مسئول اجرای فرآیند باشد.
در نتیجه:
Tick / Time Base
        │
        ├── GPS timing
        ├── Diagnostic timeout
        ├── Server timeout
        ├── SMS timeout
        └── Power timing
استفاده از زمان در FSM مجاز است، اما اجرای Callback برای پیشبرد FSM اصل معماری نیست.
________________________________________
5. Interrupt و DMA
Interrupt و DMA مسئول اجرای FSM نیستند.
وظیفه آنها محدود به مواردی مانند:
•	ثبت وقوع Hardware Event
•	ثبت دریافت داده
•	ثبت پایان DMA
•	ثبت وضعیت سخت‌افزار
•	Set کردن Flag
است.
مدل کلی:
ISR / DMA
    ↓
Flag / Status / Data
    ↓
Super Loop
    ↓
Management
    ↓
FSM
اصل:
ISR = ثبت سریع اتفاق
Super Loop = پردازش
FSM = نگهداری وضعیت فرآیند
________________________________________
6. ماشین‌های حالت شناسایی‌شده
در معماری فعلی، ماشین‌های حالت زیر شناسایی شده‌اند:
Component	FSM	وضعیت طراحی
GPS Management	GPS FSM	مشخص شده در سطح معماری
Diagnostic Management	Diagnostic Process / Engine	ساختار کلی مشخص، جزئیات نیازمند طراحی تیم دیاگ
Server Communication	Communication FSM	مشخص شده در سطح معماری
FOTA	FOTA FSM داخلی Server	حالت‌های اصلی مشخص
SMS Management	SMS Lifecycle FSM	نیازمند تکمیل جزئیات
Power Management	Power FSM	ساختار کلی مشخص
Health & Recovery	Recovery FSM / Local Recovery States	نیازمند تکمیل
Input & Request Management	FSM مرکزی ندارد	عمدتاً Event/Request processing
Queue	FSM ندارد	Component زیرساختی
Tick / Time Base	FSM ندارد	زیرساختی
________________________________________
7. GPS Management FSM
7.1 مسئولیت
GPS Management مسئول رفتار محصول در قبال موقعیت مکانی است.
این Component نباید وارد جزئیات EC200U، AT Command، NMEA، PDP یا مکانیزم داخلی GNSS شود.
این موارد متعلق به:
EC200U Manager
هستند.
مرز معماری:
GPS Management
      │
      │ Position Needed
      ▼
EC200U Manager
      │
      │ Position / Status / Quality
      ▼
GPS Management
________________________________________
7.2 حالت‌های اصلی
در سطح فعلی پنج حالت اصلی برای GPS استخراج شده است:
LOW_POWER_MONITORING
        ↓
POSITION_ACQUISITION
        ↓
POSITION_VERIFICATION
        ↓
TRACKING
        ↓
STOP_CONFIRMATION
        ↓
LOW_POWER_MONITORING
________________________________________
7.3 LOW_POWER_MONITORING
هدف:
کاهش مصرف انرژی در زمانی که خودرو در وضعیت عادی و متوقف قرار دارد.
در این حالت GPS به‌صورت دائم برای Tracking فعال نیست.
سیاست اولیه محصول:
Position Attempt
    ↓
Failure
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
این زمان‌ها Policy محصول هستند و پارامتر داخلی GNSS محسوب نمی‌شوند.
عدم دریافت Fix در این حالت الزاماً خطای سیستم نیست.
________________________________________
7.4 POSITION_ACQUISITION
در این حالت GPS Management درخواست موقعیت می‌کند.
GPS Management نباید نحوه دریافت Position را کنترل کند.
مثلاً نباید مستقیماً با:
AT+QGPS
کار کند.
بلکه از Contract مربوط به EC200U Manager استفاده می‌کند.
نتایج قابل انتظار در سطح معماری:
Position Available
Timeout / No Fix
Error
و فرآیند بدون Blocking ادامه پیدا می‌کند.
________________________________________
7.5 POSITION_VERIFICATION
Position دریافت‌شده باید قبل از تصمیم‌گیری محصول بررسی شود.
مفاهیم کیفیت شناسایی‌شده:
GOOD
SUSPICIOUS
INVALID / NO FIX
تفاوت مهم:
Suspicious با Invalid یکسان نیست.
در حالت Suspicious ممکن است نمونه به‌طور کامل رد نشود و در صورت وجود نمونه‌های دیگر، بهترین نمونه انتخاب شود.
معیارهای دقیق کیفیت وابسته به داده‌ای است که EC200U Manager ارائه خواهد کرد.
________________________________________
7.6 Movement Detection
تشخیص حرکت نباید صرفاً بر اساس یک Position انجام شود.
مدل مورد نظر:
Position Change
      ↓
Suspicion
      ↓
Verification
      ↓
Movement Confirmed
بنابراین بین:
•	حرکت مشکوک
•	حرکت تأییدشده
تفاوت وجود دارد.
هدف این ساختار کاهش False Positive است.
________________________________________
8. TRACKING
پس از تأیید حرکت:
POSITION_VERIFICATION
        ↓
Movement Confirmed
        ↓
TRACKING
در Tracking، محصول باید موقعیت‌های مورد نیاز برای Route Tracking را تولید کند.
سیاست اولیه:
~5 تا ~6 نقطه در دقیقه
این نرخ، نرخ خروجی محصول است و لزوماً نرخ نمونه‌برداری GNSS نیست.
ممکن است EC200U نمونه‌های بیشتری تولید کند و GPS Management از میان آنها نمونه مناسب را انتخاب کند.
اصل:
GNSS Sample Rate
        ≠
Firmware Decision Rate
        ≠
Product Output Rate
________________________________________
9. انتخاب بهترین Position
در صورت وجود چند نمونه GNSS، الزام معماری فعلی این نیست که:
•	اولین نمونه انتخاب شود.
•	آخرین نمونه انتخاب شود.
•	میانگین همه نمونه‌ها گرفته شود.
اصل فعلی:
نمونه‌ای که کیفیت مناسب‌تری دارد انتخاب شود.
معیار دقیق Best Sample باید پس از مشخص شدن Contract واقعی EC200U Manager تعیین شود.
معیارهای احتمالی مانند:
•	Fix Status
•	Accuracy
•	Satellite Information
•	سایر شاخص‌های کیفیت
در این مرحله صرفاً Candidate هستند و هنوز Contract نهایی محسوب نمی‌شوند.
________________________________________
10. STOP_CONFIRMATION
پس از ورود به Tracking، سیستم باید توقف خودرو را تأیید کند.
سیاست اولیه مورد بحث:
بدون تغییر معنی‌دار در حدود ~1 دقیقه
        ↓
Vehicle Stopped
        ↓
LOW_POWER_MONITORING
بنابراین توقف نیز یک تصمیم چندمرحله‌ای است و صرفاً با یک Position ثابت تصمیم‌گیری نمی‌شود.
________________________________________
11. مدل کلی GPS FSM
                    ┌──────────────────────┐
                    │ LOW_POWER_MONITORING │
                    └──────────┬───────────┘
                               │
                         Position Needed
                               │
                               ▼
                    ┌──────────────────────┐
                    │ POSITION_ACQUISITION │
                    └──────────┬───────────┘
                               │
                     Position Available
                               │
                               ▼
                    ┌──────────────────────┐
                    │ POSITION_VERIFICATION│
                    └───────┬────────┬─────┘
                            │        │
                 No Movement│        │Movement Confirmed
                            │        │
                            │        ▼
                            │  ┌────────────┐
                            │  │  TRACKING  │
                            │  └─────┬──────┘
                            │        │
                            │   Stop Suspected
                            │        │
                            │        ▼
                            │  ┌─────────────────┐
                            └─►│STOP_CONFIRMATION│
                               └────────┬────────┘
                                        │
                                  Vehicle Stopped
                                        │
                                        ▼
                              LOW_POWER_MONITORING
این نمودار در این مرحله معماری اولیه است و جزئیات Transitionها هنوز باید تکمیل شود.
________________________________________
12. Diagnostic Management
Diagnostic با GPS تفاوت دارد.
در Diagnostic، یک Operation می‌تواند چند مرحله داشته باشد و بین مراحل نیاز به دریافت پاسخ، Timeout یا زمان‌بندی وجود داشته باشد.
ساختار کلی:
Diagnostic Management
        ↓
Diagnostic Engine
        ↓
Communication Interface
        ↓
CAN / K-Line
________________________________________
13. مسئولیت Diagnostic Management
Diagnostic Management مسئول مواردی مانند:
•	شروع Operation
•	زمان‌بندی Operationهای دوره‌ای
•	انتخاب/خواندن Definition
•	اجرای Operation
•	دریافت نتیجه
•	تولید خروجی
•	پایان Operation
است.
برای نمونه، Operationهای دوره‌ای می‌توانند سیاست‌هایی مانند:
Read Parameters → هر ~30 ثانیه
Read Faults     → هر ~5 دقیقه
داشته باشند.
این زمان‌ها صرفاً مثال‌های مورد بحث هستند و مقدار نهایی محصول محسوب نمی‌شوند.
________________________________________
14. Diagnostic Engine
جزئیات پروتکل و Sequenceهای Diagnostic در اختیار تیم Diagnostic است.
بنابراین معماری نباید FSM داخلی Diagnostic Engine را بیش از حد محدود کند.
تیم می‌تواند داخل Engine از FSM استفاده کند، مشروط به اینکه Contract معماری را رعایت کند.
Contract اصلی:
Diagnostic_Process()
باید:
•	Non-Blocking باشد.
•	سریع Return کند.
•	وضعیت داخلی را حفظ کند.
•	برای پاسخ منتظر نماند.
•	برای Timeout وارد Loop نشود.
•	Super Loop را متوقف نکند.
مثلاً:
SEND REQUEST
    ↓
RETURN

Super Loop
    ↓
CHECK RESPONSE
    ↓
PROCESS RESPONSE
    ↓
NEXT STEP
________________________________________
15. Diagnostic Protocol Timing
بین دو مفهوم باید تفکیک شود:
Product Scheduling
مثلاً:
هر 30 ثانیه یک Operation
مالک:
Diagnostic Management
Protocol Timing
مثلاً:
ارسال Request
→ انتظار حداکثر زمان پاسخ
→ Timeout
مالک:
Diagnostic Engine
بنابراین:
Scheduling ≠ Protocol Timing
________________________________________
16. CAN و K-Line
CAN هدف اصلی فعلی است، اما معماری نباید K-Line را غیرممکن کند.
لایه ارتباطی باید از Diagnostic Engine جدا باشد:
Diagnostic Engine
       ↓
Diagnostic Communication Interface
       ↓
      CAN
       یا
     K-Line
تیم Diagnostic اعلام کرده است که:
•	معماری باید با Super Loop سازگار باشد.
•	هیچ Locking Loop وجود نداشته باشد.
•	در صورت نیاز K-Line ممکن است به Timer ساده نیاز داشته باشد.
اگر Timer برای K-Line استفاده شود، نقش آن باید محدود به مواردی مانند:
•	ثبت Timeout
•	Set Flag
•	ثبت Timestamp
•	Signal ساده
باشد.
اجرای اصلی Diagnostic همچنان در Super Loop انجام می‌شود.
________________________________________
17. Server Communication FSM
Server Communication یک FSM محلی دارد زیرا ارتباط با شبکه و Server ذاتاً چندمرحله‌ای و غیرهمزمان است.
مسئولیت‌های اصلی:
•	Network / Session
•	Upload
•	Download
•	Server API
•	Publish / Subscribe
•	دریافت Command
•	ارسال Event
•	ارسال Telemetry
•	Retry
•	Timeout
•	Recovery
•	FOTA
مدل مفهومی:
IDLE
  ↓
REQUEST / PREPARE
  ↓
CONNECT / COMMUNICATE
  ↓
WAIT RESPONSE
  ↓
PROCESS
  ↓
SUCCESS / RETRY / ERROR
  ↓
IDLE
جزئیات دقیق Stateهای Server هنوز طراحی نشده و باید در مرحله طراحی Component تکمیل شود.
________________________________________
18. FOTA FSM
FOTA بخشی از Server Communication است و در سطح Super Loop به‌عنوان یک Management مستقل تعریف نمی‌شود.
اما FOTA در داخل Server Communication یک فرآیند چندمرحله‌ای دارد.
حالت‌های اصلی استخراج‌شده:
IDLE
  ↓
REQUEST
  ↓
DOWNLOADING
  ↓
VERIFYING
  ↓
READY
  ↓
UPDATE
در صورت خطا، مسیرهای:
RETRY
ERROR
RECOVERY
نیز مورد نیاز خواهند بود.
جزئیات Bootloader و مکانیزم نهایی Update خارج از این FSM قرار می‌گیرد.
________________________________________
19. Event / Request و ارتباط آن با FSM
FSM نباید جایگزین Event/Request Architecture شود.
مدل:
Input
  ↓
Detection
  ↓
Event
  ↓
Policy
  ↓
Request
  ↓
Queue
  ↓
Owner Management
  ↓
FSM
مثال:
DoorOpened
    ↓
Policy
    ├── StoreRequest
    ├── SmsRequest
    └── ServerRequest
Event مسئول اجرای Action نیست.
Event فقط وقوع یک اتفاق را اعلام می‌کند و Policy مشخص می‌کند چه Requestهایی باید ایجاد شوند.
________________________________________
20. Event و State
تفاوت Event و State در معماری:
State
وضعیت پایدار یا جاری سیستم است.
مثلاً:
Vehicle = PARKED
GPS = TRACKING
Power = SLEEP
Event
وقوع یک اتفاق است.
مثلاً:
DoorOpened
ShockDetected
FotaCompleted
DiagnosticCompleted
بنابراین:
Door is OPEN
یک State است، ولی:
DoorOpened
یک Event است.
Event خودش FSM ندارد.
________________________________________
21. Queue و FSM
Queue یک زیرساخت مستقل است و FSM ندارد.
API مورد نظر:
Push()
Pop()
IsEmpty()
IsFull()
Count()
Queue نباید:
•	Callback اجرا کند.
•	Service را صدا بزند.
•	FSM را صدا بزند.
•	Event را بشناسد.
•	تصمیم Business بگیرد.
مدل:
Management
    ↓
Queue.Push()

Super Loop
    ↓
Management.Process()
    ↓
Queue.IsEmpty()
    ↓
FSM
    ↓
Queue.Pop()
________________________________________
22. Event Queue و Telemetry Queue
دو Queue اصلی برای Server Communication در نظر گرفته شده است:
              Server Communication
                       │
              ┌────────┴────────┐
              │                 │
         Event Queue       Telemetry Queue
         Priority High     Priority Normal
قاعده مصرف:
if Event Queue not empty
    send Event
else
    send Telemetry
Event Queue برای رخدادهای معنی‌دار و Telemetry Queue برای داده‌های اندازه‌گیری/عملیاتی است.
Telemetry شامل مواردی مانند:
Battery Voltage
GPS
Diagnostic Data
است و EventType ندارد.
مدل مفهومی:
Telemetry {
    DataType
    Timestamp
    Payload
}
________________________________________
23. Queue Full Policy
برای هر دو Queue، سیاست فعلی:
Queue Full
    ↓
Drop Oldest
    ↓
Insert Newest
این سیاست به‌خصوص برای Telemetry باعث می‌شود داده‌های جدیدتر بر داده‌های قدیمی اولویت داشته باشند.
در Telemetry، ظرفیت بهتر است در سطح Byte Capacity نیز بررسی شود، زیرا اندازه داده‌ها یکسان نیست.
مثلاً:
Battery Data     → کوچک
GPS Data         → کوچک
Diagnostic JSON  → ممکن است بزرگ
________________________________________
24. Power Management FSM
Power Management یکی از FSMهای اصلی Firmware است.
اصل مهم:
Power Manager مالک Power State سیستم است، نه مالک FSM داخلی همه سرویس‌ها.
مدل مفهومی:
ACTIVE
  ↓
IDLE
  ↓
SLEEP PREPARE
  ↓
SLEEP
  ↓
WAKE-UP
  ↓
ACTIVE
حالت‌های دقیق و Transitionهای نهایی هنوز باید تکمیل شوند.
________________________________________
25. مرز Power و سایر Managementها
Power Manager نباید مستقیماً State داخلی GPS، Diagnostic یا Server را تغییر دهد.
مثلاً نباید:
Power Manager
    ↓
GPS FSM = OFF
را به‌عنوان الگوی عمومی اعمال کند.
بلکه:
Power Manager
    ↓
System Power State
    ↓
GPS Management observes state
    ↓
GPS adjusts its behavior
در نتیجه:
Power FSM = مالک وضعیت توان سیستم
و:
Service FSM = مالک رفتار همان Service
است.
________________________________________
26. GPS و Power
GPS یکی از جاهایی است که این تفکیک اهمیت زیادی دارد.
مثلاً GPS Management ممکن است تصمیم بگیرد:
Position Needed
اما اینکه EC200U چگونه روشن شود، چه بخش‌هایی فعال باشند و چه Power Stateای لازم باشد، متعلق به لایه پایین‌تر و Power/EC200U Contract است.
بنابراین:
GPS Management
    = چه زمانی و برای چه هدفی Position لازم است؟

EC200U Manager
    = چگونه GNSS را در اختیار GPS Management قرار دهم؟

Power Management
    = سیستم در چه Power Stateای باشد؟
________________________________________
27. SMS Management FSM
SMS یک Lifecycle کامل دارد و بهتر است RX و TX به‌صورت دو Management مستقل شکسته نشوند.
مدل مفهومی:
IDLE
  ↓
SMS RECEIVED
  ↓
PARSE / VALIDATE
  ↓
CREATE REQUEST
  ↓
EXECUTE / WAIT RESULT
  ↓
SEND RESPONSE
  ↓
WAIT SEND RESULT
  ↓
SUCCESS / RETRY / ERROR
  ↓
IDLE
جزئیات Stateها، Timeoutها و Retry Policy هنوز نیازمند تکمیل هستند.
________________________________________
28. Health & Recovery
Health & Recovery یک حوزه Cross-Cutting است.
مواردی که تاکنون برای آن شناسایی شده:
•	Watchdog
•	تشخیص Timeout
•	تشخیص Service Stuck
•	تشخیص Fault
•	Recovery
این بخش ممکن است یک FSM مرکزی بزرگ نداشته باشد.
اصل معماری پیشنهادی:
Health Monitor
    ↓
Fault Detected
    ↓
Recovery Decision
    ↓
Local Recovery / Component Recovery / System Recovery
جزئیات این بخش هنوز طراحی نشده است.
________________________________________
29. Input & Request Management
Input & Request Management لزوماً FSM بزرگ ندارد.
مسئولیت:
Hardware / DMA / Input
       ↓
Flag
       ↓
Detection
       ↓
Event
       ↓
Policy
       ↓
Request
       ↓
Queue
است.
در صورتی که یک Detection نیازمند نگهداری وضعیت چندمرحله‌ای باشد، می‌تواند Local State داشته باشد، ولی ایجاد یک FSM مرکزی برای تمام Inputها مورد نظر نیست.
________________________________________
30. جمع‌بندی ماشین‌های حالت
در معماری فعلی، FSMها باید محلی و مالک‌محور باشند:
                    Firmware
                       │
        ┌──────────────┼──────────────┐
        │              │              │
      GPS          Diagnostic       Server
       FSM            Engine          FSM
                                      │
                                      └── FOTA FSM
        │
        └───────────────────────────────
        
        SMS FSM

        Power FSM

        Health / Recovery
        Local Recovery States
در مقابل، موارد زیر FSM مستقل نیستند:
Super Loop
Queue
Tick / Time Base
Event
Policy
ISR
DMA
________________________________________
31. اصل نهایی معماری FSM
قاعده اصلی این معماری:
هر فرآیندی که برای تکمیل شدن به چند اجرای Super Loop، انتظار پاسخ، Timeout، Retry یا حفظ وضعیت بین اجراها نیاز دارد، می‌تواند مالک یک FSM محلی باشد.
اما:
هر چیزی که صرفاً یک عملیات کوتاه و مستقل است، نباید صرفاً به دلیل وجود چند حالت منطقی به FSM تبدیل شود.
بنابراین هدف، حداقل FSM لازم برای مدیریت پیچیدگی است، نه ایجاد FSM برای تمام Firmware.
________________________________________
32. موارد نیازمند تکمیل در مرحله طراحی تفصیلی
موارد زیر در این پیوست عمداً در سطح معماری باقی مانده‌اند:
1.	Transitionهای کامل GPS FSM
2.	تعریف دقیق معیار Movement Detection
3.	معیار دقیق Best GNSS Sample
4.	Contract نهایی EC200U Manager
5.	Stateهای تفصیلی Server Communication
6.	Retry/Timeout Policy ارتباط با Server
7.	Stateهای کامل SMS
8.	Stateهای کامل Power Management
9.	Recovery Matrix
10.	Diagnostic Engine داخلی
11.	Timeoutهای دقیق CAN/K-Line
12.	رفتار دقیق FOTA در خطا و Recovery
این موارد باید در طراحی تفصیلی Component مربوطه تعیین شوند و نباید بدون نیاز در این مرحله وارد معماری سطح بالا شوند.
________________________________________
33. اصل مرزبندی با EC200U Manager
از آنجا که Stack مربوط به EC200U برون‌سپاری می‌شود، FSMهای داخلی زیر EC200U Manager بخشی از معماری داخلی Supplier محسوب می‌شوند.
Firmware اصلی فقط باید با Contract آن تعامل کند.
بنابراین:
Application Firmware
       │
       ├── GPS Management
       ├── Server Management
       └── SMS Management
                │
                ▼
        EC200U Manager
        ───────────────
        Internal FSM
        AT Handling
        GNSS Handling
        Network Handling
        Buffering
        Timeout
        Recovery
                │
                ▼
              EC200U
جزئیات FSM داخلی EC200U Manager موضوع این پیوست نیست.
معیار پذیرش آن، رعایت Contract معماری، Non-Blocking بودن، مدیریت صحیح خطا و ارائه رفتار قابل پیش‌بینی به Firmware اصلی است.

