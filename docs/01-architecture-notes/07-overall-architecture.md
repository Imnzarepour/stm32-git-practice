با داده‌هایی که تا اینجا جمع کرده‌ایم، من معماری را دیگر یک لیست از Moduleها نمی‌بینم؛ یک سیستم با چند نوع مسئولیت مشخص می‌بینم.
به نظرم تصویر فعلی این است:
                         ┌──────────────────────┐
                         │      BOOTLOADER      │
                         │ Validate / Update    │
                         └──────────┬───────────┘
                                    │
                                    ▼
                         ┌──────────────────────┐
                         │ APPLICATION STARTUP │
                         │ HW / EC200U / EEPROM│
                         │ Files / ACC / Mode   │
                         └──────────┬───────────┘
                                    │
                                    ▼
══════════════════════════════════════════════════════════════
                     APPLICATION FIRMWARE
══════════════════════════════════════════════════════════════

                         ┌──────────────────┐
                         │    SUPER LOOP    │
                         │  فقط فرصت اجرا   │
                         └────────┬─────────┘
                                  │
        ┌─────────────────────────┼──────────────────────────┐
        │                         │                          │
        ▼                         ▼                          ▼
   MANAGEMENTS              DATA PROVIDERS             INFRASTRUCTURE
        │                         │                          │
        │                         │                          ├─ Tick/Time
        │                         │                          ├─ Storage
        │                         │                          ├─ GPIO
        │                         │                          └─ Drivers
        │                         │
        │                    Latest Values
        │                         │
        │              ┌──────────┼──────────┐
        │              ▼          ▼          ▼
        │            GPS       Battery    Signals
        │
        ├── GPS Management
        ├── Server Management
        ├── SMS Management
        ├── Diagnostic Management
        ├── Power Management
        └── Health/Recovery
        │
        │
        ├──────────── Events / Policy ─────────────┐
        │                                          │
        ▼                                          ▼
     Requests                                  Detection
        │                                          ▲
        ▼                                          │
 ┌──────────────┐                           Inputs / ISR
 │    QUEUES    │
 │ Event        │
 │ Telemetry    │
 └──────┬───────┘
        │
        ▼
   Owner Management
        │
        ▼
    Execution / FSM
اما مهم‌تر از شکل بالا، منطق معماری است.
۱. چهار مسیر اصلی داریم
مسیر اول: Data
Source → Provider → Latest Value → Consumer
برای چیزهایی که تأمینشان زمان‌بر است.
مثلاً GPS، ولتاژ، جریان، وضعیت شبکه و...
________________________________________
مسیر دوم: Command
Command
   ↓
Request
   ↓
Queue
   ↓
Management
   ↓
Execution
برای «کاری را انجام بده».
مثلاً:
Open Door
Cut Fuel Pump
Send SMS
Run Diagnostic
FOTA
________________________________________
مسیر سوم: Event
Input
  ↓
Detection
  ↓
Event
  ↓
Policy
  ↓
Requests
  ↓
Queues
یعنی:
«یک اتفاق افتاد؛ حالا ببین چه اثراتی باید داشته باشد.»
________________________________________
مسیر چهارم: Evidence / Telemetry
این یکی هنوز باید یک دور دیگر دقیقش کنیم، ولی فعلاً:
Operational Data
       ↓
Telemetry
       ↓
Telemetry Queue
       ↓
Server
و در کنار آن:
System Activity
       ↓
Evidence / Logs
       ↓
Storage / Server
این بخش را هنوز به اندازه سه مسیر دیگر نبسته‌ایم.
________________________________________
۲. یک نکته خیلی مهم: Management دیگر «همه‌کاره» نیست
اوایل معماری، Management را می‌توانستیم تقریباً مرکز هر Capability ببینیم.
الان نه.
الان مسئولیت‌ها شکسته شده:
مفهوم	مسئولیت
Super Loop	فقط فرصت اجرا
Management	تصمیم و مدیریت فرآیند
Provider	آماده نگه داشتن داده
Latest Value	وضعیت/داده قابل مصرف فوری
Detection	تشخیص وقوع شرایط
Event	اعلام یک اتفاق
Policy	تعیین اثر اتفاق
Request	درخواست انجام عملیات
Queue	نگهداری درخواست
FSM	نگهداری وضعیت فرآیند طولانی
Infrastructure	دسترسی به سخت‌افزار/زیرساخت
این به نظرم یکی از مهم‌ترین دستاوردهای مصاحبه‌هاست.
________________________________________
۳. و یک اصل خیلی قوی از دل این معماری درآمده
قبلاً می‌گفتیم:
هیچ‌کس نباید Super Loop را Block کند.
الان یک لایه عمیق‌تر داریم:
هیچ Consumer نباید مجبور شود برای تأمین داده صبر کند.
پس:
                    BAD
Consumer ──────► Acquire ──────► Wait
در مقابل:
                    GOOD

Provider ──────► Acquire
                    │
                    ▼
               Latest Value
                    │
                    ▼
Consumer ───────► Read
این اصل خیلی جاهای معماری را ساده می‌کند.
________________________________________
۴. FSM هم جای خودش را پیدا کرده
دیگر FSM یک مفهوم سراسری نیست.
FSM فقط وقتی می‌آید که:
یک عملیات بین چند اجرای Super Loop ادامه پیدا کند و نیاز به حفظ وضعیت داشته باشد.
مثلاً:
Server Communication → FSM
GPS Acquisition       → FSM
SMS                   → FSM
FOTA                  → FSM
ولی:
Read Battery → FSM لازم نیست
Relay OFF    → FSM لازم نیست
GetLatestGPS → FSM لازم نیست
این به نظرم معماری را خیلی تمیز کرده.
________________________________________
۵. Power هم دیگر مرکز کنترل نیست
این را هم الان بهتر می‌بینم.
              Power Manager
                    │
                    ▼
              Power Mode
             /          \
         NORMAL       LOW POWER
            │              │
            └──────┬───────┘
                   ▼
             هر Component
          Policy خودش را دارد
یعنی Power Manager نمی‌آید بگوید:
GPS برو State 3
Server برو State 5
SMS برو State 2
فقط Mode را تعیین می‌کند.
هر Component خودش می‌فهمد در آن Mode چه کار کند.
________________________________________
۶. EC200U هم در این تصویر جای مشخصی دارد
من EC200U را اصلاً جزو معماری داخلی Application نمی‌بینم.
مرزش اینجاست:
             Application
                  │
        ┌─────────┴──────────┐
        │ GPS / Server / SMS │
        └─────────┬──────────┘
                  │
             EC200U Contract
                  │
                  ▼
          ┌───────────────┐
          │ EC200U Manager│  ← پیمانکار
          └───────┬───────┘
                  │
                  ▼
               EC200U
ما نمی‌خواهیم بدانیم داخلش:
AT
PDP
Socket
GNSS
Retry
Recovery
...
چطور پیاده شده.
این دقیقاً همان چیزی است که باید از پیمانکار بخواهیم.
________________________________________
۷. بنابراین اگر بخواهم معماری را در یک جمله تعریف کنم
به این نقطه رسیده‌ایم:
یک Firmware مبتنی بر Cooperative Non-Blocking Super Loop که در آن Managementها فرآیندها را مدیریت می‌کنند، Providerها داده‌های زمان‌بر را از قبل آماده نگه می‌دارند، Eventها اتفاقات را به Policy و Request تبدیل می‌کنند، Queueها عملیات معوق را نگهداری می‌کنند و FSM فقط برای فرآیندهای چندمرحله‌ای و زمان‌بر استفاده می‌شود.
و به نظرم این دیگر یک معماری واقعی است، نه صرفاً مجموعه‌ای از تصمیمات پراکنده.

