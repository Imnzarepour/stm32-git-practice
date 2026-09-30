پیوست فنی معماری
مدل رخداد، داده و صف ارسال در Firmware
1. هدف
این سند مدل معماری مربوط به تولید، انتقال و ارسال رخدادها و داده‌های عملیاتی در Firmware را مشخص می‌کند.
هدف اصلی، تفکیک روشن میان:
•	رخدادهای معنادار سیستم (Event)
•	داده‌های اندازه‌گیری و تله‌متری (Telemetry)
•	درخواست انجام عملیات (Request)
•	صف‌های ارسال به سرور (Queue)
است؛ به‌گونه‌ای که هر بخش مسئولیت مشخص و مستقلی داشته باشد و هیچ عملیات ارسال، سیستم را متوقف یا Blocking نکند.
________________________________________
2. اصل بنیادین معماری
در سیستم دو مفهوم مستقل وجود دارد:
Event برای بیان وقوع یک اتفاق.
Telemetry برای انتقال یک مقدار یا مجموعه‌ای از داده‌ها.
بنابراین هر داده‌ای که به سرور ارسال می‌شود الزاماً Event نیست.
نمونه:
•	باز شدن در → Event
•	تشخیص ضربه → Event
•	قطع برق → Event
•	تکمیل FOTA → Event
•	ولتاژ باتری → Telemetry
•	موقعیت GPS → Telemetry
•	نتیجه عملیات Diagnostic → Telemetry
________________________________________
3. مدل Event
Event نشان‌دهنده یک Occurrence است؛ یعنی بیان می‌کند که یک اتفاق در سیستم رخ داده است.
Event وضعیت دائمی سیستم را نمایش نمی‌دهد.
به عنوان مثال:
DoorOpened
به معنای «در باز شد» است، نه اینکه وضعیت فعلی در برابر باز یا بسته بودن را نگهداری کند.
3.1 ساختار Event
مدل پایه Event به صورت زیر در نظر گرفته می‌شود:
Event {
    EventType
    EventId
    Timestamp
}
EventType
نوع رخداد را مشخص می‌کند.
نمونه:
DoorOpened
ShockDetected
PowerDisconnected
FotaCompleted
SmsReceived
DiagnosticCompleted
در صورت تفاوت رفتاری معنادار میان رخدادها، رخدادها به صورت EventTypeهای مستقل تعریف می‌شوند.
برای مثال:
PowerDisconnectShortDetected
PowerDisconnectLongDetected
به جای اینکه مدت زمان قطع برق به عنوان منطق داخلی Policy تعریف شود.
EventId
هر Event دارای شناسه یکتا است.
EventId برای تشخیص رخدادهای تکراری در سمت سرور و پشتیبانی از ارسال At-Least-Once استفاده می‌شود.
Timestamp زمان وقوع رخداد را مشخص می‌کند.
________________________________________
4. Flag، Detection و Event
Interrupt یا ورودی سخت‌افزاری مستقیماً Event ایجاد نمی‌کند.
زنجیره تولید Event به صورت زیر است:
Input / Interrupt
        ↓
Flag
        ↓
Detection
        ↓
Event
Flag
Flag صرفاً ثبت می‌کند که یک تغییر یا رخداد سخت‌افزاری مشاهده شده است.
Flag با Event یکسان نیست.
Detection
Detection مسئول بررسی شرایط لازم برای تشخیص یک رخداد است.
این بررسی ممکن است شامل:
•	چند ورودی
•	ترتیب وقایع
•	زمان‌بندی
•	چند مرحله بررسی
•	شرایط لازم برای معتبر بودن رخداد
باشد.
Detection همچنین مسئول جلوگیری از Triggerهای تکراری نامعتبر است.
پس:
Flag ≠ Event
Detection ≠ Event
Event تنها زمانی ایجاد می‌شود که Detection وقوع رخداد را تأیید کند.
________________________________________
5. Event و Policy
Event مسئول اجرای عملیات نیست.
پس از ایجاد Event، Policy مشخص می‌کند که این رخداد چه اثراتی در سیستم داشته باشد.
برای مثال:
DoorOpened
      ↓
   Policy
      ├── StoreRequest
      ├── SmsRequest
      └── ServerRequest
Policy از Detection مستقل است.
Detection پاسخ می‌دهد:
آیا این رخداد اتفاق افتاده است؟
Policy پاسخ می‌دهد:
اگر این رخداد اتفاق افتاد، چه کارهایی باید درخواست شوند؟
بنابراین:
Detection
    ↓
Event
    ↓
Policy
    ↓
Requests
________________________________________
6. Request
Request بیان‌کننده درخواست انجام یک عملیات است.
Event و Request یک مفهوم نیستند.
برای مثال:
DoorOpened
یک Event است.
اما در نتیجه Policy ممکن است درخواست‌های زیر ایجاد شوند:
StoreRequest
SmsRequest
ServerRequest
Event ایجاد می‌شود و Policy درخواست‌های لازم را تولید می‌کند؛ اجرای واقعی این درخواست‌ها توسط سرویس مسئول انجام می‌شود.
بنابراین Event منتظر نتیجه اجرای Request نمی‌ماند.
________________________________________
7. Event و Telemetry
Telemetry یک داده یا اندازه‌گیری است و الزاماً بیانگر وقوع یک رخداد نیست.
نمونه:
BatteryVoltage = 12.3V
این یک Telemetry است.
همچنین:
GPS = {
    Latitude,
    Longitude
}
Telemetry است.
و:
DiagnosticData = {...JSON...}
نیز Telemetry است.
بنابراین Telemetry دارای مفهوم EventType نیست.
مدل مفهومی Telemetry:
Telemetry {
    DataType
    Timestamp
    Payload
}
Payload می‌تواند بر اساس نوع داده متفاوت باشد.
برای مثال:
BatteryVoltage
    Payload = 12.3

GPS
    Payload = {
        lat: ...,
        long: ...
    }

DiagnosticData
    Payload = {...JSON...}
________________________________________
8. یک ورودی می‌تواند هم Telemetry و هم Event ایجاد کند
یک منبع داده می‌تواند هم برای تولید Telemetry و هم برای Detection استفاده شود.
مثال Battery:
Battery Measurement
        │
        ├──────────────→ Telemetry
        │                   ↓
        │             Telemetry Request
        │                   ↓
        │             Telemetry Queue
        │
        └──────────────→ Detection
                            ↓
                    LowBatteryDetected
                            ↓
                          Policy
                            ↓
                          Event
                            ↓
                       Event Queue
بنابراین اندازه‌گیری و رخداد از یکدیگر مستقل هستند، حتی اگر منبع اطلاعاتی آنها یکسان باشد.
________________________________________
9. معماری صف‌های ارسال
ارسال اطلاعات به سرور به دو صف مستقل تقسیم می‌شود:
                    Server Communication
                            │
                 ┌──────────┴──────────┐
                 │                     │
                 ▼                     ▼
           Event Queue          Telemetry Queue
این تفکیک به دلیل تفاوت ماهیت و اولویت دو نوع اطلاعات انجام می‌شود.
________________________________________
10. Event Queue
Event Queue برای ارسال رخدادهای سیستم استفاده می‌شود.
نمونه داده‌های آن:
DoorOpened
ShockDetected
PowerDisconnected
FotaCompleted
SmsReceived
...
Event Queue دارای اولویت ارسال بالاتر نسبت به Telemetry Queue است.
قانون ارسال:
اگر Event Queue دارای داده باشد
        ↓
Event ارسال شود
else
        ↓
از Telemetry Queue ارسال شود
بنابراین وجود Eventهای منتظر ارسال باعث می‌شود ارسال Telemetry در اولویت دوم قرار گیرد.
________________________________________
11. Telemetry Queue
Telemetry Queue برای ارسال داده‌های عادی عملیاتی و اندازه‌گیری استفاده می‌شود.
نمونه:
GPS
BatteryVoltage
DiagnosticData
Telemetry
...
این صف می‌تواند از Event Queue بزرگ‌تر باشد، زیرا حجم و نرخ تولید Telemetry معمولاً بسیار بیشتر است.
Telemetry Queue از نظر مفهوم Eventمحور نیست.
یعنی در آن چیزی مانند:
EventType
تعریف نمی‌شود.
بلکه داده دارای نوع و Payload خود است:
DataType
Timestamp
Payload
________________________________________
12. اولویت صف Telemetry
در Telemetry Queue میان انواع مختلف داده اولویت تعریف نمی‌شود.
به عنوان مثال:
GPS
Battery
Diagnostic
همگی در سطح Queue دارای اولویت یکسان هستند.
بنابراین Queue مسئول تشخیص اهمیت کسب‌وکاری داده‌های Telemetry نیست.
________________________________________
13. رفتار Queue در حالت پر شدن
هیچ‌یک از صف‌ها نباید باعث Blocking سیستم شوند.
اگر Queue پر شود، قدیمی‌ترین داده حذف می‌شود و داده جدید وارد صف می‌شود.
قاعده عمومی:
Queue Full
    ↓
Drop Oldest
    ↓
Insert New
این سیاست برای هر دو صف Event و Telemetry اعمال می‌شود.
مثال:
E1 E2 E3 E4
اگر صف پر باشد و E5 وارد شود:
E1 حذف

E2 E3 E4 E5
همین رفتار برای Telemetry نیز برقرار است.
________________________________________
14. ویژگی مهم Telemetry Queue
با توجه به اینکه قدیمی‌ترین داده در صورت پر شدن صف حذف می‌شود، Telemetry Queue بیشتر برای نگهداری داده‌های جدیدتر و قابل ارسال طراحی می‌شود، نه تضمین نگهداری نامحدود تاریخچه.
بنابراین قطع بودن شبکه برای مدت طولانی نباید باعث توقف تولید داده یا Blocking شدن سرویس‌های تولیدکننده شود.
داده‌های جدید جای داده‌های قدیمی‌تر را می‌گیرند.
________________________________________
15. رفتار کلی ارسال
مدل کلی ارسال به سرور:
                         Requests
                            │
                 ┌──────────┴──────────┐
                 │                     │
          Event Requests       Telemetry Requests
                 │                     │
                 ▼                     ▼
           Event Queue          Telemetry Queue
                 │                     │
                 └──────────┬──────────┘
                            │
                   Server Communication
                            │
                         EC200U
                            │
                         Network
                            │
                          Server
Server Communication مسئول اجرای واقعی ارسال است.
Queue فقط مسئول نگهداری درخواست‌های ارسال تا زمان پردازش آنهاست.
________________________________________
16. اصل Non-Blocking
تمام این زنجیره باید با معماری Cooperative Non-Blocking Super Loop سازگار باشد.
هیچ بخش نباید برای:
•	ایجاد Event
•	ایجاد Request
•	ورود به Queue
•	انتظار برای شبکه
•	انتظار برای پاسخ سرور
•	Retry
سیستم را متوقف کند.
مدل مفهومی:
Create
  ↓
Queue
  ↓
Return
و پردازش واقعی در اجرای بعدی Super Loop انجام می‌شود.
________________________________________
17. قرارداد مسئولیت‌ها
مسئولیت اجزای اصلی به صورت زیر تعریف می‌شود:
جزء	مسئولیت
Interrupt / Input	ثبت تغییر سخت‌افزاری
Flag	نگهداری نشانه وقوع تغییر
Detection	تشخیص معتبر رخداد
Event	اعلام وقوع رخداد
Policy	تعیین اثرات رخداد
Request	درخواست انجام عملیات
Queue	نگهداری درخواست برای اجرا
Service Owner	اجرای واقعی Request
Server Communication	انتقال داده به سرور
این تفکیک باعث می‌شود خطاها نیز قابل ردیابی باشند.
برای مثال:
Event ساخته نشد
→ Detection را بررسی کن

Event ساخته شد ولی Request ساخته نشد
→ Policy را بررسی کن

Request ساخته شد ولی وارد Queue نشد
→ Queue را بررسی کن

Request در Queue است ولی اجرا نمی‌شود
→ Service Owner را بررسی کن

ارسال انجام نشد
→ Server Communication / Network را بررسی کن
________________________________________
18. وضعیت تصمیمات معماری
تا این مرحله موارد زیر به عنوان اصول معماری مشخص شده‌اند:
Event Model
بسته شده
•	Event بیانگر Occurrence است.
•	Event وضعیت نیست.
•	EventType مشخص‌کننده نوع رخداد است.
•	Event دارای EventId و Timestamp است.
•	Event مسئول اجرای عملیات نیست.
•	Detection از Event جداست.
•	Policy از Detection جداست.
Queue Model
بسته شده در سطح معماری
•	Event Queue و Telemetry Queue مستقل هستند.
•	Event Queue اولویت ارسال بالاتری دارد.
•	Telemetry Queue اولویت داخلی ندارد.
•	Telemetry Queue می‌تواند بزرگ‌تر باشد.
•	Telemetry دارای EventType نیست.
•	Telemetry دارای DataType و Payload است.
•	در حالت پر شدن Queue، قدیمی‌ترین داده حذف می‌شود.
•	Queue نباید باعث Blocking سیستم شود.
مواردی که عمداً هنوز وارد نشده‌اند
جزئیات زیر به مرحله بعدی معماری موکول شده‌اند:
•	طراحی دقیق State Machineها
•	Stateهای Server Communication
•	Stateهای GPS
•	Stateهای Diagnostic
•	Stateهای SMS
•	Stateهای FOTA
•	Timeout و Retry هر State
•	قرارداد دقیق API داخلی Queue
•	جزئیات حافظه و پیاده‌سازی Queue
•	فرمت نهایی Payloadهای Telemetry
این موارد بخشی از طراحی FSM و جزئیات پیاده‌سازی هستند و در این سند وارد نشده‌اند.

