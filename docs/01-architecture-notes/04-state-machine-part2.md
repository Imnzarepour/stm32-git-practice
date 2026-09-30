پیوست فنی شماره ۲
معماری ماشین‌های حالت و فرآیندهای Firmware
1. هدف
این سند، مدل معماری ماشین‌های حالت (FSM)، فرآیندهای چندمرحله‌ای و وضعیت‌های کنترلی Firmware را تعریف می‌کند.
هدف اصلی این سند، جلوگیری از ایجاد یک FSM مرکزی و پیچیده برای کل Firmware و تعیین دقیق محل مالکیت State در هر Capability است.
اصل معماری:
State متعلق به Process/Capability است، نه کل Firmware.
Firmware بر پایه یک Cooperative Non-Blocking Event-Driven Super Loop اجرا می‌شود. بنابراین هر Process در صورت نیاز می‌تواند State داخلی خود را بین چند اجرای متوالی Super Loop حفظ کند، بدون آنکه اجرای سیستم را متوقف کند.
________________________________________
2. مدل اجرایی Firmware
معماری اجرای Firmware بر پایه Super Loop است:
while(1)
{
    InputRequestManagement_Process();
    GPSManagement_Process();
    DiagnosticManagement_Process();
    ServerManagement_Process();
    SMSManagement_Process();
    PowerManagement_Process();
    HealthRecoveryManagement_Process();
}
هر Process باید:
1.	وضعیت فعلی خود را بررسی کند.
2.	وجود کار را بررسی کند.
3.	حداکثر یک گام کوچک از کار را انجام دهد.
4.	State موردنیاز را حفظ کند.
5.	سریعاً به Super Loop بازگردد.
هیچ Process نباید برای تکمیل یک عملیات منتظر بماند.
ممنوع
•	Delay
•	Busy Wait
•	Waiting Loop
•	Blocking I/O
•	انتظار مستقیم برای پاسخ Hardware
•	انتظار مستقیم برای Network
•	انتظار مستقیم برای Timeout
مجاز
•	State داخلی
•	Flag
•	Timestamp
•	Deadline
•	Queue
•	Tick / Time Base
•	Retry Counter
________________________________________
3. ISR و DMA
Interrupt و DMA بخشی از FSMهای Application نیستند.
وظیفه ISR/DMA صرفاً ثبت سریع رخداد سخت‌افزاری است.
Hardware
   ↓
ISR / DMA
   ↓
Flag / Status / Data
   ↓
Super Loop
   ↓
Management
اصل:
ISR/DMA = ثبت سریع رخداد
Super Loop = پردازش رخداد
State = حفظ وضعیت پردازش
پردازش سنگین، تصمیم‌گیری و اجرای Business Logic نباید داخل ISR انجام شود.
________________________________________
4. طبقه‌بندی وضعیت‌ها و FSMها
تمام اجزای Firmware الزاماً FSM نیستند.
سه دسته اصلی تعریف می‌شود:
4.1 FSM واقعی
Process دارای عملیاتی است که در چند اجرای Super Loop ادامه پیدا می‌کند و باید وضعیت داخلی خود را حفظ کند.
موارد فعلی:
•	Bootloader
•	GPS Management
•	Server Communication Management
•	SMS Management
•	FOTA در سطح Application
•	Health & Recovery، در صورت نیاز
________________________________________
4.2 Process / Management بدون FSM مستقل
Process وظیفه مشخصی دارد ولی الزاماً State Machine مستقل ندارد.
موارد فعلی:
•	Input & Request Management
•	Diagnostic Management در سطح معماری محصول
•	Memory Management
•	Actuator / Relay Management
•	Application Startup
این اجزا فقط در صورت ایجاد نیاز واقعی به عملیات چندمرحله‌ای، Timeout، Retry یا Recovery می‌توانند در آینده State داخلی اضافه کنند.
________________________________________
4.3 Global Mode
برخی وضعیت‌ها متعلق به کل سیستم هستند اما FSM محسوب نمی‌شوند.
مهم‌ترین نمونه:
Power Mode
 ├── NORMAL / FULL ACTIVITY
 └── LOW POWER / REDUCED ACTIVITY
Power Manager مالک Power Mode است.
اما Power Manager مالک State داخلی GPS، Server، SMS یا Diagnostic نیست.
هر Component رفتار خودش را بر اساس Power Mode تنظیم می‌کند.
________________________________________
5. Bootloader
Bootloader یک فرآیند مستقل در ابتدای چرخه عمر Firmware است.
مدل فعلی:
RESET / POWER ON
        ↓
   BOOTLOADER
        ↓
 Validate Firmware
        ↓
 ┌──────┴─────────┐
 │                │
Valid          Update Available
 │                │
 │          Validate Update
 │                │
 │             Program
 │                │
 │             Verify
 │           ┌────┴────┐
 │           │         │
 │          OK        FAIL
 │           │         │
 └─────┐     │       RESET
       │     │         │
       └─────┴─────────┘
             ↓
       Application
5.1 Firmware Validation
اعتبار Firmware بر اساس مکانیزم موجود و قبلی پروژه انجام می‌شود.
این مکانیزم در معماری جدید بازتعریف نمی‌شود.
Validation شامل همان ساختار قبلی اعتبارسنجی Firmware است.
________________________________________
5.2 Firmware Update
اگر Update موجود باشد:
1.	وجود Update تشخیص داده می‌شود.
2.	Update اعتبارسنجی می‌شود.
3.	Firmware برنامه‌ریزی می‌شود.
4.	Firmware Verify می‌شود.
5.	در صورت موفقیت، Application اجرا می‌شود.
اگر Program یا Verify شکست بخورد:
FAIL
 ↓
RESET
 ↓
BOOTLOADER
 ↓
Retry / Decision
Dual Bank
Dual Bank از نظر معماری مطلوب است، اما الزام فعلی نیست؛ زیرا ظرفیت Flash محدود است.
در صورت امکان سخت‌افزاری، Dual Bank می‌تواند Robustness فرآیند FOTA را افزایش دهد.
در غیر این صورت، Bootloader باید بر اساس ظرفیت موجود و مکانیزم فعلی پروژه طراحی شود.
________________________________________
6. Application Startup
Startup پس از Jump از Bootloader انجام می‌شود.
Startup یک Initialization Sequence است و در وضعیت فعلی نیازمند FSM مستقل نیست.
ترتیب مفهومی:
Bootloader
    ↓
Jump to Application
    ↓
Hardware Configuration
    ↓
EC200U / SIM Initialization
    ↓
Load EEPROM Configuration
    ↓
Open Required Files
    ↓
Read ACC
    ↓
Determine Power Mode
    ↓
Start Super Loop
اطلاعاتی مانند:
•	Serial Number
•	Version
•	External Key
•	Configuration
از EEPROM بارگیری می‌شوند.
EEPROM به صورت Dual Bank در نظر گرفته شده است.
NOR Flash نیز می‌تواند در صورت نیاز به عنوان محل Backup اطلاعات استفاده شود.
________________________________________
7. رفتار Startup در خطا
خرابی یک Initialization الزاماً نباید باعث توقف کل Firmware شود.
خطا باید به Capability مربوطه نسبت داده شود.
نمونه:
خطا	نتیجه
SIM / EC200U	قابلیت ارتباطات شبکه‌ای از دست می‌رود
NOR / فایل‌های Diagnostic	قابلیت Diagnostic از دست می‌رود
EEPROM	Serial Number / Version / External Key / Configuration آسیب می‌بیند
اصل:
Failure of a Capability ≠ Failure of the Entire System
جزئیات Recovery برای حافظه خارجی فعلاً بخشی از معماری نیست و در صورت نیاز در طراحی تفصیلی تعیین خواهد شد.
________________________________________
8. Power Mode
Power Management به جای FSM مرکزی، یک Global Power Mode مدیریت می‌کند.
دو حالت فعلی:
NORMAL / FULL ACTIVITY
        ↕
LOW POWER / REDUCED ACTIVITY
ACC ON
سیستم در حالت Normal قرار دارد:
•	Network فعال
•	GPS فعال
•	Statusها فعال
•	Diagnostic فعال
•	Memory Management فعال
•	Relay/Actuatorها فعال
•	LEDها در حالت عادی
ACC OFF
هیچ Capability اصلی الزاماً حذف نمی‌شود؛ بلکه شدت فعالیت کاهش می‌یابد.
نمونه:
•	کاهش CPU Clock در صورت امکان
•	کاهش Memory Access
•	کاهش فعالیت GPS
•	کاهش فعالیت Network
•	کاهش تعداد Requestها
•	خاموش کردن LEDها و Outputهای غیرضروری
•	استفاده از Low Power قابلیت‌های EC200U
•	ادامه Listening برای SMS
•	ادامه Hardware Event Detection با نرخ مناسب
تغییر Power Mode
ACC عامل اصلی تغییر Mode است.
وقتی ACC مجدداً ON شود:
ACC ON
   ↓
Power Mode = NORMAL
   ↓
Each Component
   ↓
Next Super Loop Execution
   ↓
Normal Behavior
Wake-up Sequence مرکزی وجود ندارد.
هر Component در اجرای بعدی خود Power Mode را مشاهده کرده و رفتار خودش را تغییر می‌دهد.
اصل مهم
Power Manager نباید State داخلی سایر Componentها را مستقیماً تغییر دهد.
________________________________________
9. GPS Management FSM
GPS یکی از FSMهای واقعی Firmware است، زیرا فرآیند آن چندمرحله‌ای است و در چند اجرای Super Loop ادامه پیدا می‌کند.
مدل محصولی فعلی:
LOW POWER MONITORING
        ↓
Significant Position Change?
        ↓
       YES
        ↓
POSITION ACQUISITION
        ↓
POSITION VERIFICATION
        ↓
      Confirmed?
     /         \
   NO           YES
   ↓             ↓
Recheck       TRACKING
                 ↓
          Stop Confirmed?
                 ↓
       STOP CONFIRMATION
                 ↓
       LOW POWER MONITORING
9.1 Vehicle ON
هدف:
Track Route
تقریباً:
•	6 Position Point در دقیقه
•	حدوداً هر 10 ثانیه یک خروجی
یک Position الزاماً بر اساس یک GNSS Sample انتخاب نمی‌شود.
ممکن است در بازه موردنظر چند Sample دریافت شود و Firmware بهترین Sample را انتخاب کند.
________________________________________
9.2 Vehicle OFF
هدف:
تشخیص اینکه خودرو همچنان در محل پارک است یا حرکت کرده است.
الگوی فعلی:
~5 min
 ↓ failure
~10 min
 ↓ continued failure
~20 min
این زمان‌ها Product Policy هستند و نباید با پارامترهای داخلی GNSS/EC200U اشتباه گرفته شوند.
No Fix یک وضعیت عادی محیطی است و الزاماً System Fault محسوب نمی‌شود.
________________________________________
9.3 Movement Detection
تشخیص حرکت دو مرحله‌ای است:
Low Power Monitoring
        ↓
Position Change Suspicion
        ↓
Verification
        ↓
~1 minute active GNSS
        ↓
Movement Confirmed
دو مفهوم باید از هم جدا بمانند:
•	Suspicion
•	Verified Movement
هدف، کاهش False Positive است.
________________________________________
9.4 Tracking
پس از تأیید حرکت:
TRACKING
با تقریباً:
•	5 Sample در دقیقه
•	ارسال Position به Server
تا زمانی که توقف تأیید شود.
________________________________________
9.5 Stop Detection
اگر حدوداً یک دقیقه Position تغییر معناداری نداشته باشد:
TRACKING
   ↓
No Significant Movement
   ↓
STOP CONFIRMATION
   ↓
LOW POWER MONITORING
________________________________________
9.6 GNSS Quality
سه مفهوم کیفیت در سطح محصول در نظر گرفته می‌شود:
GOOD
SUSPICIOUS
INVALID / NO FIX
Suspicious به معنی Invalid نیست.
Sample مشکوک ممکن است در مقایسه با Sampleهای دیگر انتخاب شود.
معیار دقیق Best Sample وابسته به قابلیت‌های واقعی EC200U و داده‌های قابل دسترس آن است و باید در قرارداد EC200U Manager تعیین شود.
________________________________________
10. Server Communication FSM
Server Communication دارای دو جریان مستقل است:
Outbound
Queue
 ↓
Server Management
 ↓
Channel Check
 ↓
Secure Channel Ready
 ↓
Send One Item
 ↓
Success / Failure
Inbound
Server
 ↓
Receive Command
 ↓
Validate
 ↓
Create Request / Event
 ↓
Queue
 ↓
Return
Server Communication مسئول اجرای Command نیست.
مثلاً:
MQTT Command: TurnOffFuelPump
        ↓
Server Communication
        ↓
Validate
        ↓
Request
        ↓
Queue
        ↓
Actuator Management
Server Communication منتظر نتیجه اجرای Command نمی‌ماند.
اگر نتیجه اجرا باید به Server گزارش شود، آن گزارش یک جریان جدید Event/Telemetry خواهد بود.
________________________________________
10.1 Server FSM
مدل مفهومی:
IDLE
  ↓
Activity
  ↓
CHANNEL CHECK
  ↓
CHANNEL READY
  ↓
COMMUNICATE
  ├── Success → IDLE
  └── Failure → BACKOFF / RETRY
                         ↓
                    CHANNEL CHECK
اگر Network در دسترس نباشد:
•	Retry انجام می‌شود.
•	فاصله Retry افزایش می‌یابد.
•	Backoff استفاده می‌شود.
•	Queue در صورت پر شدن طبق سیاست Queue مدیریت می‌شود.
پس از بازگشت Network:
Network Available
       ↓
Secure Channel Ready
       ↓
Normal Communication
هر اجرای ServerManagement_Process() حداکثر یک Queue Item را پردازش/ارسال می‌کند.
________________________________________
11. FOTA
FOTA از نظر محصول یک Capability زیرمجموعه Server Communication است.
در سطح Application می‌تواند FSM مستقل داشته باشد:
IDLE
  ↓
UPDATE REQUEST
  ↓
DOWNLOADING
  ↓
VERIFYING
  ↓
READY
  ↓
UPDATE
جزئیات پایین‌سطح Modem، AT Command، PDP، Socket و GNSS متعلق به EC200U Manager است و نباید وارد FSM محصول شود.
پس:
Application FOTA State ≠ EC200U Internal State
________________________________________
12. SMS Management FSM
SMS یک Management مستقل دارد زیرا چرخه دریافت و ارسال آن چندمرحله‌ای است.
مدل مفهومی:
IDLE
 ↓
SMS RECEIVED
 ↓
READ / PARSE
 ↓
VALIDATE
 ↓
CREATE REQUEST
 ↓
EXECUTE / HANDOFF
 ↓
OPTIONAL RESPONSE
 ↓
IDLE
در صورت نیاز:
•	Timeout
•	Retry
•	Failure Handling
به عنوان State داخلی SMS Management اضافه می‌شود.
SMS در حالت LOW POWER نیز فعال باقی می‌ماند.
فرمان معتبر SMS باید در هر Power Mode قابل اجرا باشد.
________________________________________
13. Diagnostic Management
Diagnostic در سطح معماری اصلی، FSM مشخصی که توسط معماری Firmware تحمیل شود ندارد.
مسئولیت Product Architecture:
Diagnostic Management
        ↓
Schedule
        ↓
Start Diagnostic Operation
        ↓
Diagnostic Engine
        ↓
Result
Diagnostic Engine می‌تواند State داخلی خود را داشته باشد.
اما ساختار دقیق آن متعلق به تیم Diagnostic / پیمانکار است.
Contract
Diagnostic_Process() باید:
•	توسط Super Loop قابل فراخوانی باشد.
•	Non-Blocking باشد.
•	سریع Return کند.
•	State داخلی خود را حفظ کند.
•	از Delay استفاده نکند.
•	Busy Wait نداشته باشد.
•	Waiting Loop نداشته باشد.
•	اجرای خود را به Timer Callback وابسته نکند.
•	برای Timeout از Tick/Deadline استفاده کند.
ساختار داخلی FSM آزاد است.
ارتباط با CAN
CAN Driver باید پشت Interface مناسب قرار گیرد تا Diagnostic Engine مستقیماً به Register/HAL وابسته نشود.
معماری باید Compatibility با K-Line را نیز حفظ کند.
برای K-Line در صورت نیاز می‌توان Timer ساده‌ای فراهم کرد که صرفاً:
•	Flag
•	Timestamp
•	Timeout
را ثبت کند.
پردازش اصلی همچنان در Super Loop باقی می‌ماند.
________________________________________
14. Event و FSM
Event نباید مستقیماً FSM را اجرا کند.
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
FSM / Process
مثلاً:
DoorOpened
    ↓
Policy
    ├── StoreRequest
    ├── SmsRequest
    └── ServerRequest
Event مسئول اجرای Request نیست.
Event پس از Trigger شدن و ایجاد Requestهای موردنیاز، چرخه خود را پایان می‌دهد.
________________________________________
15. Detection و Event
Flag، Event نیست.
Interrupt
   ↓
Flag
   ↓
Detection
   ↓
Event
Detection مسئول تشخیص وقوع واقعی Event است.
Detection باید از Trigger شدن تکراری Event جلوگیری کند.
مثلاً در Door Detection:
Switch Sequence
       ↓
10-Step Validation
       ↓
Valid
       ↓
DoorOpened Event
Occurrence با State متفاوت است.
Door is OPEN       = State
DoorOpened         = Event
اگر شرایط دوباره رخ دهد، Event جدید ایجاد می‌شود.
________________________________________
16. Event Model
ساختار پایه:
Event
{
    EventType;
    EventId;
    Timestamp;
}
نمونه EventType:
ShockDetected
PowerDisconnectShortDetected
PowerDisconnectLongDetected
DoorOpened
FotaCompleted
SmsReceived
DiagnosticCompleted
اگر تفاوت دو شرایط باعث رفتار متفاوت شود، EventType جداگانه تعریف می‌شود.
Event دارای Payload اجباری نیست.
Event Delivery
مدل فعلی:
At-Least-Once Delivery
بنابراین ممکن است در شرایط Retry یک Event بیش از یک بار ارسال شود.
Server باید با استفاده از EventId امکان Idempotency / Deduplication را فراهم کند.
Timestamp به تنهایی برای Deduplication کافی نیست.
Event فعلاً الزام به Persistence در برابر Reset/Power Loss ندارد.
________________________________________
17. Telemetry
Telemetry با Event متفاوت است.
Event
وقوع یک اتفاق معنی‌دار:
DoorOpened
ShockDetected
FotaCompleted
Telemetry
مقدار یا داده عملیاتی:
BatteryVoltage = 12.3V

GPS = {
    lat,
    long
}

DiagnosticData = {...}
ساختار مفهومی:
Telemetry
{
    DataType;
    Timestamp;
    Payload;
}
Telemetry الزاماً EventType ندارد.
Payload می‌تواند اندازه و ساختار متفاوت داشته باشد.
________________________________________
18. Queue و FSM
Queue صرفاً مسئول ذخیره و وضعیت داده است.
API پایه:
Push()
Pop()
IsEmpty()
IsFull()
Count()
Queue نباید:
•	Callback داشته باشد.
•	Service را صدا بزند.
•	FSM را اجرا کند.
•	Event را بشناسد.
•	Business Logic داشته باشد.
مدل اجرا:
Super Loop
    ↓
Management
    ↓
Queue.HasData?
    ↓
FSM / Process
    ↓
Queue.Pop()
    ↓
Process One Step
________________________________________
19. Event Queue و Telemetry Queue
دو Queue مستقل وجود دارد:
                 Server Communication
                         │
             ┌───────────┴───────────┐
             │                       │
        Event Queue            Telemetry Queue
        High Priority          Normal Priority
قاعده ارسال:
if EventQueue not empty
    send Event
else
    send Telemetry
Event Queue اولویت بالاتری دارد.
Telemetry Queue برای داده‌هایی مانند:
•	GPS
•	Battery
•	Diagnostic
استفاده می‌شود.
داخل Telemetry Queue اولویت داخلی بین این داده‌ها تعریف نمی‌شود.
________________________________________
20. Queue Full Policy
اگر Queue پر شود:
Oldest item حذف و New Item وارد می‌شود.
این سیاست برای:
•	Event Queue
•	Telemetry Queue
در نظر گرفته شده است.
Telemetry به دلیل تفاوت زیاد اندازه Payload باید از نظر ظرفیت، صرفاً بر اساس تعداد Item طراحی نشود؛ ظرفیت برحسب Byte نیز باید در طراحی تفصیلی بررسی شود.
________________________________________
21. Input & Request Management
این Management مسئول تبدیل Input به Event و Request است.
مدل:
Hardware Input
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
مسئولیت آن اجرای Capability نیست.
مثلاً:
Door Open
 ↓
DoorOpened Event
 ↓
Policy
 ↓
SmsRequest
 ↓
SMS Queue
از این نقطه به بعد، اجرای SMS مسئولیت SMS Management است.
________________________________________
22. Memory Management
Memory Management فعلاً FSM مستقل ندارد.
مسئولیت آن شامل فراهم کردن سرویس‌های حافظه موردنیاز Componentهاست.
موارد اصلی:
•	EEPROM
•	NOR Flash
•	فایل‌های Diagnostic
•	Configuration
EEPROM دارای Dual Bank است.
برای NOR نیز امکان قرار دادن Backup برای اطلاعات مهم در نظر گرفته شده است.
Recovery پیچیده فایل‌ها فعلاً در Scope معماری قرار ندارد.
________________________________________
23. Actuator / Relay Management
Actuator Management یک Command Executor ساده است.
مدل:
Request
   ↓
Validate
   ↓
Execute Command
   ↓
Done
در وضعیت فعلی:
•	Feedback FSM وجود ندارد.
•	انتظار برای Feedback وجود ندارد.
•	Command باعث یک عملیات مستقیم می‌شود.
مثال:
TurnOffFuelPump
       ↓
Validate
       ↓
Set Output
       ↓
Done
اگر در آینده نیاز به Feedback یا تأیید اجرای فیزیکی ایجاد شود، FSM محلی می‌تواند به آن اضافه شود.
________________________________________
24. Health & Recovery
Health & Recovery یک قابلیت Cross-Cutting است.
موارد احتمالی:
•	Watchdog
•	Timeout Detection
•	Fault Detection
•	Stuck Process Detection
•	Recovery
در صورتی که Recovery چندمرحله‌ای باشد، FSM محلی ایجاد می‌شود.
اصل مهم:
Recovery نباید به یک FSM مرکزی تبدیل شود که State تمام Componentها را کنترل کند.
________________________________________
25. EC200U Manager
EC200U یک مرز معماری مهم است.
Application Firmware
        │
        ├── GPS Management
        ├── Server Communication
        ├── SMS Management
        └── سایر Components
                │
                ▼
         EC200U Manager
                │
                ▼
             EC200U
EC200U Manager توسط پیمانکار/تیم مربوطه پیاده‌سازی می‌شود.
جزئیات داخلی آن شامل مواردی مانند:
•	AT Commands
•	PDP
•	Socket
•	Modem State
•	GNSS Internal State
•	Buffer
•	Timeout
•	Retry
•	Recovery
•	Modem Power State
نباید به Application Architecture نشت کند.
اصل
Application با Capability کار می‌کند، نه با AT Command.
Application نباید مستقیماً به AT Command، PDP یا Socket دسترسی داشته باشد.
EC200U Manager باید:
•	Non-Blocking باشد.
•	State داخلی خود را حفظ کند.
•	Error/Timeout را مدیریت کند.
•	Retry/Recovery داخلی داشته باشد.
•	Status/Result مشخص ارائه کند.
•	Health/Readiness مشخص داشته باشد.
•	Interface نسخه‌پذیر داشته باشد.
•	امکان Mock/Stub برای تست فراهم کند.
________________________________________
26. اصل استقلال FSMها
FSMهای مختلف نباید State یکدیگر را مستقیماً تغییر دهند.
نمونه صحیح:
GPS
 ↓
Telemetry Queue
 ↓
Server Management
نمونه نادرست:
GPS
 ↓
Force Server FSM = SEND
یا:
Power Manager
 ↓
Force GPS FSM = SLEEP
Component باید از طریق:
•	Event
•	Request
•	Queue
•	API
•	Global Power Mode
با Component دیگر تعامل کند.
________________________________________
27. جدول نهایی وضعیت معماری
Component / Process	نوع	FSM مستقل
Bootloader	Boot Process	بله
Application Startup	Initialization Sequence	خیر
Power Management	Global Mode	خیر
GPS Management	Management	بله
Server Communication	Management	بله
FOTA	Capability	بله
SMS Management	Management	بله
Diagnostic Management	Management / Contract	خیر در سطح معماری
Diagnostic Engine	Internal Process	متعلق به تیم Diagnostic
Input & Request Management	Process	خیر
Event	Data Model	خیر
Policy	Rule/Decision	خیر
Event Queue	Infrastructure	خیر
Telemetry Queue	Infrastructure	خیر
Memory Management	Infrastructure/Service	فعلاً خیر
Actuator / Relay	Command Executor	خیر
Health & Recovery	Cross-Cutting	در صورت نیاز
EC200U Manager	External Manager	FSM داخلی دارد، ولی خارج از Application Architecture
________________________________________
28. اصل نهایی معماری
معماری Firmware نباید بر اساس تعداد FSMها طراحی شود.
سؤال اصلی این نیست که:
«برای این قابلیت FSM داریم یا نه؟»
بلکه:
«آیا این Process برای تکمیل عملیات خود نیاز دارد وضعیتش را بین چند اجرای Super Loop حفظ کند؟»
اگر پاسخ بله باشد، FSM محلی قابل استفاده است.
اگر پاسخ خیر باشد، Process ساده کافی است.
بنابراین:
             SUPER LOOP
                  │
      ┌───────────┼───────────┐
      ▼           ▼           ▼
   Process      Process      Process
      │           │           │
   Local FSM   Local FSM    No FSM
      │           │           │
      └───────────┼───────────┘
                  │
        Event / Request / Queue
                  │
             Components
و مهم‌ترین اصل:
هیچ FSM مرکزی برای کنترل کل Firmware وجود ندارد.
هر Capability مالک State خودش است، ارتباط بین Capabilityها از طریق Contract، Event، Request، Queue و API انجام می‌شود و Super Loop تنها مکانیزم اجرای تعاونی و Non-Blocking سیستم است.

