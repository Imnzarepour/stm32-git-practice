پیوست فنی شماره ۵ — معماری Interface و Contract بین ماژول‌ها
1. هدف
هدف این معماری، ایجاد استقلال بین Managementهای Firmware و جلوگیری از وابستگی مستقیم آن‌ها به پیاده‌سازی داخلی یکدیگر است.
هر Management مالک کامل فرآیند و رفتار داخلی خود است و سایر اجزا فقط از طریق Contractهای مشخص با آن تعامل می‌کنند.
________________________________________
2. اصل استقلال Managementها
هیچ Management نباید مستقیماً Management دیگری را فراخوانی کند.
بنابراین:
GPS Management ─────X────→ Server Management

SMS Management ─────X────→ Power Management

Diagnostic Management ─X──→ SMS Management
Managementها مستقل هستند و ارتباط بین آن‌ها فقط از طریق الگوهای تعریف‌شده معماری انجام می‌شود.
________________________________________
3. انواع ارتباط
در معماری Firmware چهار نوع ارتباط اصلی وجود دارد:
3.1 Data
برای داده‌ای که قبلاً توسط Provider تهیه و آماده شده است:
Source
   ↓
Provider
   ↓
Latest Value
   ↓
Consumer
Consumer نباید برای دریافت داده، فرآیند زمان‌بر Acquisition را آغاز کند.
________________________________________
3.2 Event
برای اعلام وقوع یک رخداد:
Detection
   ↓
Event
   ↓
Policy
Event صرفاً بیان می‌کند که یک رخداد اتفاق افتاده است و مسئول اجرای عملیات نیست.
________________________________________
3.3 Request / Action
برای درخواست انجام یک عملیات:
Request
   ↓
Queue
   ↓
Owner Management
   ↓
Execution
Management درخواست‌دهنده مسئول اجرای عملیات نیست.
برای مثال:
Send SMS Request
       ↓
SMS Queue
       ↓
SMS Management
اینکه SMS ارسال شده، ارسال نشده، Retry شده یا به دلیل نبود اعتبار انجام نشده است، مسئولیت SMS Management است.
Result عمومی برای تمام Managementها تعریف نمی‌شود.
________________________________________
3.4 Fault
برای اعلام خطای یک Management:
Management
   ↓
Fault
   ├──→ Error Queue / Log
   └──→ Local Recovery
هر Management مالک تشخیص و Recovery خطای خودش است.
________________________________________
4. Contract عمومی Management
Contract هر Management حداقلی و مستقل است:
Management Contract
├── Process()
├── Input / Request
└── Fault / Event (در صورت نیاز)
Process()
تمام Managementها باید قابل فراخوانی از Super Loop باشند.
Process():
•	Non-Blocking است.
•	نباید منتظر Hardware، Network، Timeout یا عملیات کند بماند.
•	وضعیت فرآیند را در State داخلی خود نگه می‌دارد.
•	در هر فراخوانی، فقط به اندازه لازم پیشروی می‌کند.
•	مسئولیت زمان‌بندی و تصمیم‌گیری داخلی با خود Management است.
•	باید رفتار خود را متناسب با Power Mode مدیریت کند.
________________________________________
5. عدم تعریف Result عمومی
معماری Firmware یک Result عمومی و اجباری برای همه عملیات تعریف نمی‌کند.
برای مثال:
Application
    ↓
Send SMS Request
    ↓
SMS Management
Application منتظر نتیجه SMS نمی‌ماند.
SMS Management خودش مسئول:
•	اجرای ارسال
•	Timeout
•	Retry
•	تشخیص Failure
•	Recovery
•	ثبت Fault
است.
در صورت نیاز به اطلاع‌رسانی بیرونی، SMS Management می‌تواند Event یا Fault مناسب ایجاد کند.
________________________________________
6. مسئولیت Contract در مقابل Implementation
Contract فقط موارد زیر را مشخص می‌کند:
•	ورودی‌ها
•	درخواست‌ها
•	خروجی‌های قابل مشاهده
•	Eventها و Faultهای قابل اعلام
•	محدودیت‌های اجرایی
•	مسئولیت هر Management
اما موارد زیر داخل Contract محصول الزاماً تعریف نمی‌شوند:
•	FSM داخلی
•	Stateهای داخلی
•	الگوریتم Retry
•	Timeoutهای داخلی
•	Protocol Sequence
•	روش ارتباط با Hardware
•	جزئیات Driver
•	جزئیات پیاده‌سازی
این موارد متعلق به مالک همان Management هستند.
________________________________________
7. مثال Diagnostic
برای Diagnostic، Contract می‌تواند شامل موارد زیر باشد:
Diagnostic Management
├── Process()
├── Start Request
├── Diagnostic Data
├── Fault
└── Execution Constraints
      └── Non-Blocking
اما ساختار داخلی Diagnostic Engine، پروتکل CAN/K-Line، Timeoutها و FSM داخلی توسط تیم Diagnostic تعیین می‌شود.
________________________________________
8. اصل عدم انتشار مسئولیت
هر Management باید مسئولیت خود را تا انتها ببندد.
مثلاً:
Send SMS
   ↓
SMS Management
   ├── Send
   ├── Retry
   ├── Detect Failure
   ├── Log Fault
   └── Finish
نیازی نیست Management دیگری درگیر جزئیات این فرآیند شود.
این اصل باعث کاهش Coupling و جلوگیری از زنجیره وابستگی بین Managementها می‌شود.
________________________________________
9. Reset به‌عنوان استثنا
Managementها مجازند Recovery محلی خود را انجام دهند.
اما System Reset یک اختیار متمرکز است:
Management
   ↓
Reset Request
   ↓
System Management
   ↓
System Reset
هیچ Managementی نباید مستقیماً MCU یا کل سیستم را Reset کند.
________________________________________
10. جمع‌بندی معماری
الگوی نهایی ارتباطات:
Data
Source → Provider → Latest Value → Consumer

Event
Detection → Event → Policy

Action
Request → Queue → Owner Management → Execution

Fault
Management → Error Queue / Log
              ↓
          Local Recovery

System Reset
Management → Reset Request → System Management → Reset
اصل نهایی
Managementها مستقل‌اند؛ هر Management مسئول کار، خطا و Recovery خودش است. ارتباط فقط از طریق Contractهای مشخص انجام می‌شود و هیچ Management مستقیماً Management دیگری را فراخوانی نمی‌کند.
معماری عمداً حداقلی نگه داشته می‌شود و از ایجاد لایه‌های عمومی مانند Signal Bus، Result Manager یا Health Manager مستقل که ارزش عملی مشخصی ندارند، اجتناب می‌شود.

