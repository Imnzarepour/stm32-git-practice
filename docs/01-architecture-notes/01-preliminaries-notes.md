پیوست فنی
معماری، محدوده و الزامات فریم‌ورک Firmware میکروکنترلر TCU
1. هدف و جایگاه این سند
این سند به‌عنوان پیوست فنی معماری Firmware میکروکنترلر واحد تلماتیک (TCU) پروژه خودروی متصل تهیه شده است.
هدف سند، ایجاد یک مرجع مشترک برای موارد زیر است:
•	تعیین نقش و مرز مسئولیت Firmware میکروکنترلر؛
•	تعیین ارتباط Firmware با سخت‌افزار، مودم ارتباطی، GPS/GNSS، شبکه خودرو و Backend؛
•	تعیین معماری اجرایی Firmware؛
•	تعیین نحوه مدیریت رخدادها، ارتباطات، SMS، تماس، دیاگ، حافظه و توان؛
•	تعیین سیاست رفتار سیستم در شرایط عادی، قطع شبکه، قطع ACC و خطا؛
•	ایجاد مبنایی برای تعریف WBS، تقسیم کار، طراحی تفصیلی و کنترل پیمانکار Firmware.
این سند باید بین «نیازمندی قطعی»، «تصمیم معماری» و «فرض یا پیشنهاد فنی» تفکیک قائل شود.
________________________________________
2. منابع و سطح اعتبار
2.1. RFP سایپا یدک — منبع نیازمندی محصول
RFP با موضوع «خودروهای متصل در گروه خودروسازی سایپا» هدف پروژه را ایجاد یک پلتفرم یکپارچه خودروی متصل بیان می‌کند و در معماری سه‌لایه، لایه Vehicle-On را شامل ماژول تلماتیک، GPS و ارتباط با شبکه CAN خودرو می‌داند.
RFP همچنین قابلیت‌هایی نظیر ردیابی، Remote Diagnostics، به‌روزرسانی نرم‌افزار ECUها، خدمات ضدسرقت، SOS، خدمات امدادی، دسترسی به اطلاعات لحظه‌ای خودرو و تحلیل داده را برای پلتفرم هدف مطرح می‌کند.
در RFP اتصال به ECUها و توانایی استخراج اطلاعات عملگرها و سنسورها و انجام امور قابل انجام توسط تجهیزات دیاگ نیز به‌عنوان قابلیت مورد انتظار مطرح شده است.
اعتبار: الزام/نیازمندی سطح محصول.
________________________________________
2.2. سند پیش‌نویس اولیه معماری
این سند یک پیشنهاد معماری نسبتاً تفصیلی برای Firmware ارائه می‌کند و شامل موضوعاتی مانند Super Loop، EC200U، FOTA، GPS، Storage، BLE، RTC، SMS، تماس، HAL و مدیریت توان است.
اما این سند نباید به‌عنوان حقیقت نهایی معماری تلقی شود. بعضی از مفروضات آن در مصاحبه معماری اخیر اصلاح یا تأیید نشده‌اند.
به‌خصوص:
•	استفاده از RTOS در آن مطرح شده، در حالی که معماری مورد توافق فعلی مبتنی بر Super Loop غیرمسدودکننده است.
•	Mutex/Semaphore و Preemption برای FOTA پیشنهاد شده، در حالی که چنین مدل اجرایی هنوز تصویب نشده است.
•	BLE در آن فرض شده، ولی در معماری فعلی هنوز جزو نیازمندی‌های تأییدشده نیست.
•	جزئیات دقیق حافظه، تعداد فایل‌ها، نرخ GPS و برخی رفتارهای Power/GPS در آن به‌صورت عددی پیشنهاد شده‌اند، اما همه این اعداد از طرف کارفرما یا تیم فعلی تثبیت نشده‌اند.
•	برخی موارد مانند mTLS، Dual-Bank، LBS، Cell ID و نحوه FOTA نیازمند تصمیم تفصیلی و تطبیق با سخت‌افزار نهایی هستند.
اعتبار: پیشنهاد/فرض معماری؛ نه الزام قطعی، مگر جایی که جداگانه تأیید شده باشد.
________________________________________
2.3. سند پیش‌نویس دوم معماری

یک پیش‌نویس دوم و مستقل نیز در همان مرحله اولیه تهیه شد که پوشش مشابهی (Super Loop، ماشین‌حالت‌های سلسله‌مراتبی، پارسر AT Command، Flash Layout، بوت‌لودر/FOTA، Pin Map، تست) داشت اما جزئیات و اعداد متفاوتی پیشنهاد می‌داد. مثل پیش‌نویس اول (بخش ۲.۲)، این سند هم مبنای نهایی معماری نیست؛ موارد زیر از آن صراحتاً پذیرفته نشدند:
•	تشخیص ضربه/تصادف مبتنی بر IMU شش‌محوره — معماری فعلی یک ورودی Shock ساده‌تر دارد (`hardware-input-management.md`).
•	Backend مبتنی بر Apache Kafka — خارج از Scope این سند؛ تصمیم معماری Backend به تیم/پیمانکار آن واگذار شده.
•	به‌روزرسانی Firmware مبتنی بر پروتکل UDS (ISO 14229) با سرویس‌های 0x22/0x31/0x10 — معماری پذیرفته‌شده برای Firmware Update بر پایه Dual-Bank/CRC ساده‌تر است (`bootloader.md`)، بدون فرض UDS برای این مسیر؛ UDS همچنان برای Diagnostic Management (روی CAN) به‌کار می‌رود.
•	فرمت ثابت ۱۸ بایتی رکورد تله‌متری و چیدمان دقیق فلش پیشنهادی — فرمت نهایی Payload تله‌متری هنوز باز است (نگاه کن OPEN-ITEMS).
•	مشخصات دقیق سخت‌افزاری مثل خروجی‌های Open-Collector با جریان ۳۰۰mA یا پشتیبانی 5G — نیازمند تأیید جداگانه با HDS/سخت‌افزار نهایی، در هیچ Contract فعلی به این شکل نیامده‌اند.
اعتبار: پیشنهاد/فرض معماری؛ نه الزام قطعی.
________________________________________
3. وضعیت فعلی پروژه از دید توسعه
پروژه Firmware عملاً از ترکیب چند مسیر توسعه تشکیل می‌شود:
3.1. مسیر PCB
[واقعیت]
برد TCU توسط یک پیمانکار سخت‌افزاری در حال توسعه است و حداقل یک نسخه از PCB ساخته شده است. این مسیر ماهیت سخت‌افزاری دارد و شامل تکمیل و اصلاح PCB است.
Firmware باید در نهایت با نسخه نهایی سخت‌افزار منطبق شود.
________________________________________
3.2. مسیر Diagnostic
[واقعیت]
تیم داخلی تجربه طولانی و تخصص قابل توجه در حوزه Diagnostic دارد و دانش این حوزه در سازمان موجود است.
دیاگ از نظر دانش دامنه‌ای، ریسک اصلی پروژه محسوب نمی‌شود.
اما معماری داده و نحوه استقرار/اجرای داده‌های Diagnostic در Firmware نیازمند سازمان‌دهی است.
________________________________________
3.3. مسیر Infrastructure
[واقعیت]
تیم داخلی در حوزه زیرساخت Firmware تجربه بالایی دارد؛ از جمله:
•	Bootloader
•	Serial Number
•	Memory Management
•	Versioning
•	زیرساخت‌های عمومی Firmware
بنابراین این بخش نیز از نظر دانش سازمانی ریسک اصلی پروژه نیست.
________________________________________
3.4. مسیر EC200U / 200U Bring-up
[واقعیت]
ارتباطات سلولار پروژه بر پایه ماژول Quectel EC200U است.
دانش و تجربه فعلی تیم در حوزه EC200U نسبت به سه حوزه دیگر کمتر است و این بخش مهم‌ترین شکاف دانشی پروژه محسوب می‌شود.
بنابراین Bring-up، شناخت رفتار واقعی مودم، AT Interface، Network Registration، Data Session، SMS، Voice، خطاهای مودم و Recovery آن باید در ابتدای پروژه به‌صورت جدی مدیریت شود.
[برداشت معماری]
ریسک EC200U باید از منطق اصلی Application جدا شود؛ یعنی Application نباید مستقیماً با جزئیات AT Command کار کند.
________________________________________
4. نقش اصلی MCU
[برداشت تثبیت‌شده از مصاحبه]
MCU صرفاً یک کنترلر ساده I/O نیست.
MCU هسته اجرایی Firmware دستگاه است و وظیفه دارد خروجی‌های زیر را در قالب یک Firmware یکپارچه به هم متصل کند:
1.	منطق Diagnostic؛
2.	زیرساخت Firmware؛
3.	ارتباط و تعامل با EC200U؛
4.	مدیریت ورودی‌ها و رخدادهای خودرو؛
5.	مدیریت ارتباطات خروجی و ورودی؛
6.	مدیریت GPS؛
7.	مدیریت توان؛
8.	مدیریت حافظه و داده‌های موقت/آفلاین؛
9.	مدیریت خروجی‌های فیزیکی.
در عین حال، MCU نباید مسئولیت‌هایی را که متعلق به Backend، Application یا لایه اختصاصی Diagnostic است، بدون نیاز به داخل خود بکشد.
________________________________________
5. معماری اجرایی Firmware
5.1. مدل اجرایی پیشنهادی
[تصمیم معماری]
مدل اجرایی مورد نظر:
Cooperative Non-Blocking Event-Driven Super Loop
است.
یعنی سیستم بر پایه یک Super Loop اجرا می‌شود و Taskهای مختلف به‌صورت متوالی اجرا می‌شوند.
نمونه مفهومی:
while (1)
{
    Event_Task();
    Communication_TX_Task();
    Communication_RX_Task();
    SMS_Task();
    Call_Task();
    GPS_Task();
    Diagnostic_Task();
    Memory_Task();
    Power_Task();
}
این کد صرفاً نمای مفهومی است و ترتیب و تعداد Taskها در طراحی تفصیلی تعیین خواهد شد.
________________________________________
5.2. اصل Non-Blocking
[تصمیم معماری]
هیچ Task نباید برای تکمیل یک عملیات، CPU را منتظر نگه دارد.
ممنوع:
wait for network
delay
wait for SMS
wait for GPS
wait for diagnostic response
مجاز:
شروع عملیات
ثبت State
ثبت Timestamp
Return
و در اجرای بعدی:
بررسی State
بررسی نتیجه
ادامه عملیات
Return
بنابراین هر Task باید بتواند در یک اجرای کوتاه، وضعیت را بررسی کرده و برگردد.
________________________________________
6. نقش Interrupt و DMA
[تصمیم معماری]
Interrupt و DMA پایین‌ترین لایه اجرایی Firmware هستند و وظیفه آنها انجام عملیات بسیار اولیه و سریع است.
Interrupt
نمونه:
ACC تغییر کرد
        ↓
GPIO Interrupt
        ↓
ثبت وضعیت جدید
یا
Set Flag
        ↓
Return
ISR نباید:
•	به سرور وصل شود؛
•	داده را Parse کند؛
•	File System را اجرا کند؛
•	Diagnostic را اجرا کند؛
•	عملیات طولانی انجام دهد.
DMA
DMA برای انتقال داده‌هایی مانند UART و سایر جریان‌های حجیم یا زمان‌حساس استفاده می‌شود.
وظیفه DMA:
انتقال داده و اعلام آماده‌شدن داده
است، نه پردازش کامل داده.
پس:
DMA/ISR
   ↓
Data Available
   ↓
Return
   ↓
Super Loop
   ↓
Task
   ↓
Processing
________________________________________
7. مدل Event
یک نمونه کلیدی، تغییر وضعیت ACC است.
سناریو:
ACC تغییر می‌کند
      ↓
Interrupt
      ↓
ثبت Level جدید
      ↓
Return
      ↓
Super Loop
      ↓
Switch/Event Task
      ↓
تشخیص تغییر
      ↓
ایجاد درخواست ارسال Event
      ↓
Return
بنابراین Interrupt مسئول اعلام رخداد است، نه اجرای رفتار کامل رخداد.
________________________________________
8. اصل تفکیک Event از Action
[برداشت معماری]
Event Manager نباید به یک God Module تبدیل شود.
مثلاً:
ACC_CHANGED
فقط اعلام می‌کند که ACC تغییر کرده است.
ماژول مربوطه تصمیم می‌گیرد در پاسخ چه کاری انجام شود.
بنابراین:
Hardware
   ↓
Interrupt
   ↓
Event/Signal
   ↓
Relevant Task
   ↓
Action
بهتر از این است که:
Interrupt
   ↓
Event Manager
   ↓
اجرای تمام منطق سیستم
باشد.
________________________________________
9. ارتباط با Server
9.1. تفکیک تولید پیام و ارسال
[تصمیم معماری]
ماژولی که تشخیص می‌دهد داده‌ای باید برای Server ارسال شود، نباید خودش مسئول جزئیات ارتباط باشد.
مدل پیشنهادی:
Event / GPS / Command Response
             ↓
          Outbox
             ↓
 Communication Manager
             ↓
           EC200U
             ↓
          Network
             ↓
          Server
Application می‌گوید:
این پیام باید ارسال شود.
Communication Manager مسئول این است که:
•	آیا مودم آماده است؟
•	آیا شبکه آماده است؟
•	آیا Session برقرار است؟
•	آیا ارسال در حال انجام است؟
•	آیا Timeout رخ داده؟
•	آیا باید Retry شود؟
•	آیا مودم نیاز به Recovery دارد؟
________________________________________
10. اولویت داده‌ها
بر اساس تصمیمات مصاحبه معماری، سه نوع داده رفتار متفاوت دارند.
10.1. Event
[تصمیم]
Event مهم‌ترین نوع داده است.
اگر شبکه قطع باشد:
Event
 ↓
Storage/Queue
 ↓
Retry
 ↓
Send
 ↓
Server ACK
 ↓
Remove
Event نباید صرفاً به دلیل قطع شبکه Drop شود.
________________________________________
10.2. GPS
[تصمیم]
GPS از Event کم‌اهمیت‌تر است.
در صورت قطع شبکه، نقاط GPS می‌توانند در حافظه ذخیره شوند و بعداً ارسال شوند.
اما از دست رفتن تعدادی رکورد GPS در اثر Reset، Crash یا شرایط مشابه قابل قبول است.
بنابراین برای GPS الزام به Durable Storage مشابه Event وجود ندارد.
________________________________________
10.3. Diagnostic
[تصمیم]
در وضعیت فعلی تصمیم گرفته شده است که Diagnostic Data در صورت نبود ارتباط شبکه ذخیره نشود و می‌تواند Drop شود.
بنابراین:
Event      → Durable
GPS        → Best Effort / Buffered
Diagnostic → Drop-able
این تصمیم تأثیر مستقیم بر معماری Storage دارد.
________________________________________
11. تضمین تحویل Event
[تصمیم]
روش مورد نظر:
At-Least-Once Delivery + Server-side Idempotency
است.
هر Event دارای Timestamp است و RTC در دستگاه وجود دارد.
در صورت عدم اطمینان از اینکه Server پیام را دریافت کرده یا خیر، MCU می‌تواند مجدداً Event را ارسال کند.
Server باید بتواند Event تکراری را تشخیص داده و فقط یک بار ثبت کند.
در طراحی نهایی، علاوه بر Timestamp، تعریف یک شناسه یکتای Event نیز توصیه می‌شود.
[برداشت معماری]
Timestamp به‌تنهایی برای Idempotency کافی نیست؛ زیرا دو Event متفاوت ممکن است Timestamp یکسان یا بسیار نزدیک داشته باشند.
بنابراین بهتر است ساختار Event شامل حداقل:
Device ID
Event ID
Event Type
RTC Timestamp
Payload
باشد.
________________________________________
12. رفتار در قطع شبکه
[تصمیم]
قطع شبکه نباید باعث Blocking شدن Firmware شود.
مثلاً:
Communication Task
       ↓
Network unavailable
       ↓
ثبت State
       ↓
Return
در اجرای بعدی:
Communication Task
       ↓
بررسی Timeout / Retry Timer
       ↓
تلاش مجدد
       ↓
Return
بنابراین Retry باید State-based و Time-based باشد.
________________________________________
13. زمان‌بندی و GetTick
[تصمیم]
از یک Time Base مانند GetTick() برای کنترل Timeout، Retry، Periodic Task و فاصله زمانی عملیات استفاده می‌شود.
نمونه:
last_attempt
current_tick

if (current_tick - last_attempt >= retry_period)
{
    try_again();
}
به این ترتیب نیازی به:
delay()
وجود ندارد.
________________________________________
14. Real-Time Requirement
[تصمیم]
سیستم نیازمند یک Scheduler سخت‌گیرانه Hard Real-Time برای اکثر قابلیت‌های سطح Application نیست.
برای مثال، از دید محصول قابل قبول است که برقراری مجدد ارتباط با Server چند ثانیه زمان ببرد.
در نتیجه:
•	اجرای فوری Application Taskها الزام ندارد؛
•	واکنش اولیه Hardware Event باید سریع باشد؛
•	Taskها باید کوتاه و Non-Blocking باشند؛
•	زمان پاسخ ارتباطی می‌تواند در محدوده چند ثانیه باشد، مشروط بر اینکه Event از دست نرود.
این موضوع باعث می‌شود معماری ساده Super Loop نسبت به RTOS پیچیده مناسب‌تر باشد.
________________________________________
15. مدیریت ورودی از Server
مدل مورد نظر:
EC200U
   ↓
RX / DMA
   ↓
Communication RX Task
   ↓
Parse
   ↓
Validate
   ↓
Identify Command
   ↓
Dispatch
RX Task نباید منطق اجرای همه Commandها را در خود داشته باشد.
برای مثال:
Server Command
      ↓
Command Manager
      ↓
Unlock / Lock / Engine Cut / ...
________________________________________
16. SMS
[واقعیت/نیازمندی]
SMS یکی از قابلیت‌های محصول موجود و مورد انتظار است. RFP نیز وجود قابلیت‌های مرتبط با SMS را در محصولات قبلی و خدمات مورد انتظار نشان می‌دهد.
[تصمیم معماری]
SMS باید حتی در شرایطی که ACC خاموش است نیز پاسخگو باشد.
بنابراین SMS نباید به چرخه ACC ON محدود شود.
پیشنهاد ساختاری:
SMS RX
  ↓
SMS Task
  ↓
Parse / Validate
  ↓
Command
  ↓
Dispatch
________________________________________
17. تماس
[واقعیت]
قابلیت هشدار از طریق تماس تلفنی در محصولات قبلی وجود داشته است.
RFP همچنین قابلیت‌های صوتی/ارتباطی را در اکوسیستم خودروی متصل مطرح می‌کند.
[برداشت معماری]
MCU بهتر است در سطح کنترل تماس باقی بماند و جزئیات مسیر صوتی را به EC200U واگذار کند.
در همان سند پیش‌نویس اولیه نیز استفاده از AT Commandهایی مانند ATD/ATH پیشنهاد شده است، اما این جزئیات باید در Bring-up واقعی EC200U اعتبارسنجی شوند.
________________________________________
18. مدیریت Diagnostic
[واقعیت]
Remote Diagnostic یکی از قابلیت‌های اصلی RFP است. همچنین RFP اتصال به ECUها و استخراج اطلاعات سنسورها و عملگرها و انجام عملیات Diagnostic را مطرح می‌کند.
[واقعیت پروژه]
تیم داخلی در Diagnostic دارای دانش و تجربه طولانی است.
[تصمیم معماری]
Diagnostic باید به‌صورت یک قابلیت مستقل Firmware سازمان‌دهی شود.
یک نیاز مهم این است که Processing Data/Files بتواند در Firmware خوانده و اجرا شود.
در وضعیت فعلی، داده‌های پردازشی Diagnostic در External NOR Flash قرار می‌گیرند و MCU باید آنها را بخواند و بر اساس Timing Framework پردازش کند.
این موضوع نشان می‌دهد External NOR صرفاً یک Backup Storage ساده نیست، بلکه بخشی از Runtime Data Architecture است.
________________________________________
19. معماری داده Diagnostic
[واقعیت پروژه]
در حال حاضر داده‌های Diagnostic و Update در معماری داده پراکنده هستند.
مدل مورد نظر:
Diagnostic Data
       ↓
External NOR
       ↓
MCU
       ↓
Read / Process
به‌روزرسانی داده می‌تواند در محل دیگری دریافت شود و پس از Validation در محل فعال قرار گیرد.
[پیشنهاد معماری]
بهتر است مفهوم زیر از ابتدا طراحی شود:
ACTIVE
STAGING
VALIDATION
COMMIT
تا Update ناقص باعث خراب شدن Data فعال Diagnostic نشود.
[باز]
نوع دقیق File System، قالب فایل، Layout حافظه، Atomic Commit و روش Rollback هنوز تصمیم نهایی محسوب نمی‌شوند.
استفاده از LittleFS در گفتگوی اولیه به‌عنوان یک فرض مطرح شد، اما هنوز نباید آن را الزام معماری دانست.
________________________________________
20. GPS
[واقعیت]
GPS/GNSS یکی از اجزای اصلی TCU است و RFP ردیابی لحظه‌ای و دوره‌ای خودرو را مطالبه می‌کند.
[تصمیم]
GPS در حالت Online باید با نرخ مناسب محصول داده تولید کند.
در ACC ON نمونه نیاز مطرح‌شده:
حدود 6 Position در دقیقه
و در ACC OFF نرخ بسیار پایین‌تر خواهد بود؛ مثلاً:
حدود 1 Position در 5 دقیقه
این اعداد فعلاً نمونه طراحی حاصل از گفتگو هستند و نه الزام قطعی RFP.
________________________________________
21. GPS Offline Buffer
[تصمیم]
در صورت قطع شبکه:
GPS
 ↓
Offline Buffer
 ↓
Reconnect
 ↓
Upload
GPS باید امکان Buffer شدن داشته باشد.
اما Storage GPS نیازمند تضمین کامل در برابر Reset نیست.
در نتیجه Circular Buffer برای این کاربرد منطقی است.
[باز]
تعداد فایل‌ها، اندازه دقیق فایل‌ها و File System هنوز باید بر اساس ظرفیت واقعی NOR و نرخ داده نهایی تعیین شود.
در سند پیش‌نویس اولیه سه فایل 1MB پیشنهاد شده است.
این عدد فعلاً همان پیشنهاد پیش‌نویس اولیه است و تصمیم قطعی پروژه محسوب نمی‌شود.
________________________________________
22. مدیریت توان
[تصمیم]
مدیریت توان یکی از اجزای اصلی Firmware است، به‌خصوص در ACC OFF.
معماری باید بین حداقل دو وضعیت کلی تفاوت قائل شود:
ACC ON
ACC OFF
در ACC OFF:
•	مصرف توان باید کاهش یابد؛
•	GPS می‌تواند با نرخ پایین‌تر فعالیت کند یا خاموش شود؛
•	Diagnostic اجرا نمی‌شود؛
•	SMS و فرمان‌های ارتباطی همچنان باید قابل پاسخ باشند؛
•	رخدادهای مهم مانند Shock باید قابلیت بیدارکردن سیستم را داشته باشند.
RFP نیز باتری پشتیبان داخلی و استمرار ارسال اطلاعات پس از قطع باتری خودرو را به‌عنوان ویژگی محصول مطرح می‌کند.
________________________________________
23. Event در ACC OFF
یکی از مهم‌ترین سناریوهای معماری:
ACC OFF
   ↓
Shock
   ↓
Interrupt
   ↓
ثبت رخداد
   ↓
Wake / Event Task
   ↓
Create Event
   ↓
Outbox
   ↓
Communication Manager
   ↓
EC200U
   ↓
Server
این رفتار نباید وابسته به ACC ON باشد.
در صورت نبود شبکه:
Event
 ↓
Persistent Queue
 ↓
Retry
________________________________________
24. اولویت‌های عملیاتی
بر اساس مصاحبه معماری، اولویت‌ها به‌صورت مفهومی چنین هستند:
اولویت ۱ — Event
رخدادهای مهم کاربر یا دستگاه باید از دست نروند.
اولویت ۲ — GPS
آخرین وضعیت مکانی باید تا حد امکان حفظ شود.
اولویت ۳ — Diagnostic
Diagnostic اهمیت دارد، ولی در صورت نبود شبکه الزام به ذخیره آن وجود ندارد.
الزام دائمی — SMS
SMS باید در هر وضعیت دستگاه قابل پاسخ باشد.
الزام زیرساختی — Communication Reliability
قطع شبکه نباید موجب قفل شدن Firmware شود و ارتباط باید قابلیت Recovery و Retry داشته باشد.
________________________________________
25. Communication Manager
این بخش یکی از مهم‌ترین اجزای Firmware خواهد بود.
[تصمیم]
لایه‌های بالاتر نباید با EC200U و AT Command مستقیماً کار کنند.
مدل:
Application
     ↓
Communication API
     ↓
Communication Manager
     ↓
EC200U Driver
     ↓
UART / DMA
Application می‌تواند مفاهیمی مانند:
Send()
GetStatus()
RequestConnection()
داشته باشد.
اما Communication Manager باید جزئیات زیر را مدیریت کند:
•	AT Command;
•	Response;
•	Timeout;
•	Retry;
•	Network Registration;
•	Session;
•	SIM;
•	مودم Reset/Recovery;
•	ارسال و دریافت؛
•	خطاهای ارتباطی.
________________________________________
26. Reliability مودم
[واقعیت]
از دید محصول، قابل قبول نیست که Failure مودم مستقیماً به Application منتقل شود و سیستم بگوید «ارتباط Quectel خراب است».
[تصمیم]
Reliability ارتباطات بخشی از مسئولیت Firmware است.
یعنی:
Application
   ↓
Send Request
   ↓
Communication Service
   ↓
Retry / Recovery / Reconnect
و Application نباید نیاز داشته باشد بداند:
AT+...
CME ERROR
Network Registration
PDP Context
...
________________________________________
27. State Machine
[برداشت معماری]
State Machine حذف نمی‌شود؛ اما بهتر است یک State Machine عظیم برای کل MCU ساخته نشود.
هر Service می‌تواند State Machine داخلی خود را داشته باشد.
مثلاً:
Communication:
IDLE
CONNECTING
CONNECTED
SENDING
WAIT_RESPONSE
ERROR
RETRY
یا:
FOTA:
IDLE
DOWNLOADING
VALIDATING
READY
COMMIT
FAILED
اما Super Loop همچنان هماهنگ‌کننده اصلی اجرای Taskها است.
________________________________________
28. Memory Management
[تصمیم]
مدیریت حافظه باید یک Service مستقل باشد.
اما Storage باید بر اساس نوع داده سیاست متفاوت داشته باشد:
Configuration → Persistent
Event          → Persistent / Durable
GPS            → Circular / Best Effort
Diagnostic     → Drop-able when offline
FOTA           → Staging Storage
بنابراین یک «Memory Manager» واحد نباید الزاماً همه داده‌ها را با یک Policy مدیریت کند.
________________________________________
29. RTC و زمان
[واقعیت]
RTC در سخت‌افزار وجود دارد.
[تصمیم]
RTC مبنای Timestamp کردن Eventها است.
در نتیجه Event در زمان قطع شبکه نیز دارای Timestamp خواهد بود.
[پیشنهاد معماری]
RTC باید در معماری به‌عنوان یک Time Source مستقل تعریف شود و منطق سیستم نباید مستقیماً به وجود Network برای داشتن Timestamp وابسته باشد.
________________________________________
30. FOTA
[واقعیت محصول]
RFP امکان به‌روزرسانی نرم‌افزار ECUها از راه دور را به‌عنوان قابلیت کلان پلتفرم مطرح می‌کند.
[پیشنهاد پیش‌نویس اولیه]
سند پیش‌نویس اولیه برای Firmware خود TCU نیز External NOR، Dual-Bank و Validation/Signature را مطرح کرده است.
اما این موارد هنوز باید از نظر MCU واقعی و Hardware نهایی تأیید شوند.
به‌خصوص:
•	Dual-Bank واقعاً توسط MCU پشتیبانی می‌شود؟
•	اندازه Firmware چقدر است؟
•	Bootloader دقیقاً چه مسئولیتی دارد؟
•	Signature Verification کجا انجام می‌شود؟
•	Encryption لازم است؟
•	Rollback چگونه انجام می‌شود؟
•	FOTA خود TCU با FOTA ECUها چه تفاوتی دارد؟
همه این موارد باید در طراحی تفصیلی تعیین شوند.
________________________________________
31. BLE
سند پیش‌نویس اولیه وجود BLE را در Hardware و Passive Keyless Entry فرض کرده است.
[وضعیت فعلی]
این قابلیت در مصاحبه فعلی به‌عنوان نیازمندی قطعی Firmware تأیید نشده است.
بنابراین BLE نباید در WBS قطعی Firmware قرار گیرد مگر اینکه Hardware/RFP/Business Requirement آن را تأیید کند.
________________________________________
32. CAN
[واقعیت]
RFP اتصال به شبکه CAN خودرو و دسترسی به اطلاعات ECUها را مطالبه می‌کند.
[واقعیت پروژه]
دانش Diagnostic در تیم داخلی وجود دارد.
[پیشنهاد معماری]
CAN Driver و Hardware Interface باید از Diagnostic Logic جدا باشد.
مدل:
CAN Driver
    ↓
CAN Interface
    ↓
Diagnostic Runtime
و جزئیات پروتکل‌های Diagnostic نباید به Driver سخت‌افزار وابسته شوند.
________________________________________
33. معماری لایه‌ای پیشنهادی
بر اساس تمام اطلاعات فعلی، معماری پیشنهادی من به شکل زیر است:
┌──────────────────────────────────────────┐
│              APPLICATION                 │
│                                          │
│ Event │ GPS │ Diagnostic │ Commands     │
│ SMS   │ Call │ Tracking  │ Security     │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│                SERVICES                  │
│                                          │
│ Communication │ Memory │ Power │ Config │
│ Event         │ Time   │ Storage        │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│             HARDWARE ABSTRACTION         │
│                                          │
│ EC200U │ GNSS │ CAN │ GPIO │ ADC        │
│ UART   │ DMA  │ RTC │ Flash │ Timer     │
└────────────────────┬─────────────────────┘
                     │
┌────────────────────▼─────────────────────┐
│                 MCU/HAL                  │
│                                          │
│ Interrupt │ DMA │ Registers │ Startup   │
└──────────────────────────────────────────┘
Super Loop در بالای لایه سخت‌افزار و در سطح اجرای Services/Application قرار دارد.
________________________________________
34. Taskهای محتمل Firmware
لیست زیر هنوز WBS نهایی نیست، اما مبنای مناسبی برای شکستن کار است:
Core
•	Main / Super Loop
•	Time Base / Tick
•	Scheduler/Dispatcher ساده
•	Error Handling
•	Watchdog
Hardware Interface
•	GPIO
•	EXTI
•	UART
•	DMA
•	ADC
•	RTC
•	Timer
•	CAN
•	External NOR
•	Internal Flash
Event
•	ACC Event
•	Shock Event
•	Digital Input Events
•	Server Command Events
•	Event Queue
•	Event Persistence
•	Event Retry
Communication
•	EC200U Driver
•	AT Parser
•	Modem State Machine
•	SIM Management
•	Network Registration
•	Data Session
•	TX
•	RX
•	Timeout
•	Retry
•	Recovery
SMS
•	SMS RX
•	SMS Parser
•	SMS Command Dispatcher
•	SMS Response
Call
•	Call Control
•	Dial
•	Hang-up
•	Call State
GPS
•	GNSS Interface
•	NMEA/Data Parser
•	Position Validation
•	Periodic Acquisition
•	GPS Power Control
•	Offline Buffer
•	GPS Upload
Diagnostic
•	Diagnostic Runtime
•	Processing Data Reader
•	Timing Engine
•	Diagnostic Memory Interface
•	Data Update/Staging
•	Validation
•	Activation
Storage
•	Internal Flash
•	External NOR
•	Configuration
•	Event Storage
•	GPS Buffer
•	Diagnostic Data
•	FOTA Storage
Power
•	ACC State
•	Sleep
•	Wake-up
•	Battery Monitoring
•	Peripheral Power Control
•	Low Power Policy
Firmware Infrastructure
•	Bootloader
•	Firmware Version
•	Device Serial Number
•	Configuration
•	Firmware Integrity
•	FOTA
________________________________________
35. اصولی که پیمانکار Firmware باید رعایت کند
اصل ۱ — Non-Blocking
هیچ تابعی نباید به‌صورت غیرضروری CPU را منتظر نگه دارد.
اصل ۲ — ISR کوتاه
Interrupt فقط باید حداقل عملیات لازم را انجام دهد.
اصل ۳ — DMA برای انتقال
پردازش سنگین در ISR/DMA انجام نشود.
اصل ۴ — State-based Processing
هر عملیات چندمرحله‌ای باید State داشته باشد.
اصل ۵ — Time-based Retry
Retry و Timeout با Time Base مدیریت شوند.
اصل ۶ — Separation of Concerns
Application نباید مستقیماً با Register، AT Command یا جزئیات Driver درگیر باشد.
اصل ۷ — Hardware Abstraction
تعویض MCU/Modem/Hardware نباید منطق Application را تخریب کند.
اصل ۸ — Failure Recovery
Failure هر Service باید قابل تشخیص و Recovery باشد.
اصل ۹ — Data Policy
هر نوع داده باید Policy خودش را داشته باشد.
اصل ۱۰ — No Hidden Blocking
هر Library یا Driver ثالث نیز باید از نظر Blocking بودن بررسی شود.
________________________________________
36. چیزهایی که از سند پیش‌نویس اولیه پذیرفته نشده‌اند
موارد زیر در سند پیش‌نویس اولیه آمده‌اند اما در این سند به‌عنوان واقعیت قطعی پذیرفته نمی‌شوند:
1.	استفاده از RTOS؛
2.	Mutex/Semaphore برای SPI؛
3.	Preemption واقعی Taskها؛
4.	وجود قطعی BLE؛
5.	Passive Keyless Entry؛
6.	سه فایل دقیقاً 1MB برای GPS؛
7.	کاهش دقیق GPS به 1/10 هنگام Offline؛
8.	Wake-up دقیقاً هر 10 دقیقه؛
9.	GPS Timeout دقیقاً 30 ثانیه؛
10.	LBS با Cell ID به‌عنوان رفتار قطعی؛
11.	mTLS به‌عنوان طراحی نهایی Firmware؛
12.	Hard-coded Certificate در فاز توسعه؛
13.	Dual-Bank FOTA به‌عنوان قابلیت قطعی MCU؛
14.	Decryption/Signature/Bank Switch با همین تقسیم مسئولیت؛
15.	حذف یا وجود قطعی قابلیت‌های خاص مانند Jammer Detection؛
16.	منطق دقیق Engine Cut-off؛
17.	رفتار دقیق Battery Cutoff؛
18.	خاموش شدن تمام LEDها در ACC OFF؛
19.	اتصال مستقیم مسیر صوتی به EC200U؛
20.	هر نوع جزئیات سخت‌افزاری که هنوز در HDS/PCB نهایی تأیید نشده باشد.
این موارد باید یا از سند Hardware نهایی، یا از RFP، یا از تصمیم رسمی معماری استخراج و تثبیت شوند.
________________________________________
37. مهم‌ترین تصمیمات معماری حاصل از مصاحبه
تا این نقطه، تصمیمات زیر نسبتاً روشن هستند:
موضوع	تصمیم فعلی
مدل اجرای Firmware	Super Loop
RTOS	فعلاً مورد نیاز نیست
ISR	کوتاه و حداقلی
DMA	انتقال و اعلام آماده‌بودن داده
Task	کوتاه، مشخص و Non-Blocking
Blocking	ممنوع
Timing	Tick / GetTick
Communication	Service مستقل
EC200U	پشت Communication Abstraction
Event	مهم‌ترین داده
Event در قطع شبکه	حفظ و Retry تا ارسال
Event Delivery	At-Least-Once
Duplicate	Idempotency در Server
Timestamp	RTC
GPS	Buffered / Best Effort
GPS Loss	قابل قبول برای چند رکورد
Diagnostic Offline	Drop مجاز
SMS	در همه وضعیت‌ها پاسخگو
ACC OFF	Low Power
Shock	قابلیت واکنش در ACC OFF
Storage	Policy متفاوت برای انواع داده
Diagnostic Data	قابل پردازش از External NOR
LittleFS	هنوز تصمیم نهایی نیست
________________________________________
38. موارد باز که قبل از طراحی تفصیلی باید تعیین شوند
این سند عمداً برخی موارد را باز نگه می‌دارد.
Hardware
•	MCU دقیق؛
•	ظرفیت Internal Flash؛
•	ظرفیت RAM؛
•	ظرفیت External NOR؛
•	نوع و تعداد SPI؛
•	UARTهای واقعی؛
•	CAN Controller؛
•	Power Architecture؛
•	GPIO Mapping؛
•	ADC Resolution؛
•	GNSS Interface.
EC200U
•	مدل دقیق و Variant؛
•	AT Command Set؛
•	Network Registration Behavior؛
•	Data Session؛
•	SMS Behavior؛
•	Voice Behavior؛
•	RI Behavior؛
•	Reset Mechanism؛
•	Failure Modes؛
•	Firmware Update؛
•	مصرف توان در حالت‌های مختلف.
Communication Protocol
•	Protocol بین TCU و Server؛
•	ACK؛
•	Message ID؛
•	Event ID؛
•	Retry Policy؛
•	Timeout؛
•	Authentication؛
•	Encryption؛
•	Idempotency Contract.
Storage
•	File System؛
•	NOR Partition؛
•	Event Storage Format؛
•	GPS Storage Format؛
•	Diagnostic Storage Format؛
•	Wear Leveling؛
•	Power Loss Behavior؛
•	Corruption Detection؛
•	Recovery.
Diagnostic
•	فرمت Processing File؛
•	Runtime Instruction Model؛
•	Parser؛
•	Timing Model؛
•	Update Mechanism؛
•	Validation؛
•	Version Compatibility.
FOTA
•	Bootloader Boundary؛
•	Image Format؛
•	Signature؛
•	Encryption؛
•	Rollback؛
•	Dual Bank؛
•	Update Trigger؛
•	Failure Recovery.
________________________________________
39. جمع‌بندی معماری
معماری پیشنهادی Firmware را می‌توان در یک جمله این‌گونه خلاصه کرد:
Firmware یک سیستم Event-Driven، Non-Blocking و مبتنی بر Cooperative Super Loop است که در آن Interrupt و DMA فقط واکنش‌های اولیه را انجام می‌دهند، Taskهای مستقل در Super Loop هر بار یک گام محدود از عملیات خود را اجرا می‌کنند، و Services مستقلی مانند Communication، Event، GPS، Diagnostic، SMS، Call، Memory و Power مسئول مدیریت State و رفتار تخصصی خود هستند.
اصل کلیدی معماری این است:
Hardware Event
      ↓
Interrupt / DMA
      ↓
Signal / Flag
      ↓
Super Loop
      ↓
Task
      ↓
State Processing
      ↓
Service
      ↓
Action
و برای ارتباط:
Event
  ↓
Outbox
  ↓
Communication Manager
  ↓
EC200U
  ↓
Network
  ↓
Server
در این معماری، پیچیدگی از داخل Application به یک State Machine مرکزی منتقل نمی‌شود؛ بلکه هر Service پیچیدگی خودش را مدیریت می‌کند.
این مدل با شرایط فعلی پروژه—وجود دانش قوی در Diagnostic و Infrastructure، وجود سخت‌افزار در حال تکمیل، و شکاف دانش در EC200U—همخوانی دارد و امکان توسعه موازی و کنترل بهتر ریسک پروژه را فراهم می‌کند.
________________________________________
40. سطح بلوغ فعلی این سند
این سند را نباید هنوز Design Specification نهایی Firmware دانست.
در حال حاضر سه سطح داریم:
Level 1
Product / RFP Requirements
        ↓
Level 2
Architecture Decisions
        ↓
Level 3
Detailed Design
        ↓
Level 4
Implementation
        ↓
Level 5
Verification
سند حاضر عمدتاً Level 1 و Level 2 را پوشش می‌دهد.
برای ورود به Level 3 باید ابتدا Hardware Interface Matrix، Communication Protocol، Storage Layout، Diagnostic Data Model، EC200U State Machine و Timing/Power Matrix تثبیت شوند.

