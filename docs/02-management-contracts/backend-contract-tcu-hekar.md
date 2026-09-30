# پیوست فنی — قرارداد ارتباطی TCU ↔ Backend (هکار / هوشران خودرو آریان)

**وضعیت سند:** مبنای طراحی، در حال تکمیل از طریق Simulation.
**پیمانکار Backend:** هکار (نام دیگر: هوشران خودرو آریان) — مسئول کامل Backend و Application طبق تقسیم‌کار پروژه.
**جایگزین می‌کند:** سند اولیه فناپ (نحوه ارسال داده دستگاه‌های ردیاب) + تحلیل فنی اولیه داخلی — هر دو در این نسخه ادغام و اصلاح شده‌اند.

**قاعده مهم برای هر خواننده:** هر بند با یکی از سه برچسب مشخص شده. هیچ‌کس حق ندارد بند `[باز]` را به‌صورت خودسرانه `[قطعی]` تلقی کند.

- **[قطعی]** — تصمیم پذیرفته‌شده، مبنای پیاده‌سازی.
- **[برداشت/پیشنهاد]** — تحلیل مهندسی فعلی؛ تا تأیید صریح، Requirement نیست.
- **[باز]** — نیازمند تصمیم یا توافق با هکار، عمدتاً از طریق Simulation کشف می‌شود.

---

## 1. تغییر مهم نسبت به نسخه قبلی این سند

تماس مستقیم با هکار دو بند «قطعی» نسخه قبلی را به «باز» برگرداند:

| بند | وضعیت قبلی | وضعیت فعلی |
|---|---|---|
| Transport | [قطعی] MQTT | **[باز]** — کاندیدا: MQTT (اولویت اول) / TCP Socket / HTTP |
| Encoding | [قطعی] JSON فعلاً | **[باز]** — کاندیدای Firmware: JSON؛ هکار تجربه قبلی با JSON ندارد؛ تصمیم نهایی از Simulation |

**[قطعی]** این تغییر هیچ اثری روی معماری Firmware ندارد، چون از ابتدا Transport و Encoder به‌صورت Interface مستقل طراحی شده بودند (بخش ۳).

---

## 2. محدوده مسئولیت Server Management

بدون تغییر نسبت به تحلیل قبلی:

**در محدوده:** ارسال Event/Telemetry/Status، دریافت Command، اعتبارسنجی اولیه Command، Command ACK، مدیریت Connection/Reconnect/Timeout/Retry، Authentication در سطح Contract، تبدیل Message به فرمت انتقال.

**خارج از محدوده:** اجرای Command، منطق Actuator/Firmware Update/Diagnostic، تصمیم‌گیری درباره Event (این با Event Policy است)، Persistence دائمی، مدیریت مستقیم UART/EC200U.

---

## 3. معماری لایه‌ای (بدون تغییر، حالا با اهمیت بیشتر)

```text
Management (GPS, Actuator, Firmware Update, ...)
        ↓  (فقط از طریق Queue/Event، هرگز مستقیم)
Event Queue / Telemetry Queue / Command Path
        ↓
Server Management
        ↓
Encoder Interface  ──┬── JSON Encoder       (کاندیدای فعلی)
                      └── Binary Encoder     (گزینه آینده)
        ↓
Transport Interface ──┬── MQTT Adapter
                      ├── TCP Adapter
                      └── HTTP Adapter
        ↓
EC200U Manager
        ↓
Backend (هکار)
```

**[قطعی]** هیچ Management نباید مستقیم Server Management را صدا بزند؛ فقط از طریق Queue/Event.
**[قطعی]** هیچ AT Command یا جزئیات UART نباید داخل Server Management باشد؛ همه از طریق EC200U Manager.
**[قطعی]** انتخاب Transport یا Encoder نباید به کد Business Server Management نشت کند.

---

## 4. انواع پیام

**[برداشت/پیشنهاد — تا تأیید هکار]**

| نوع پیام | سؤالی که جواب می‌دهد | جهت |
|---|---|---|
| Event | چه اتفاقی افتاد؟ | TCU → Backend |
| Telemetry | چه مقداری اندازه‌گیری شد؟ | TCU → Backend |
| Status | الان وضعیت دستگاه چیست؟ | TCU → Backend (Periodic + on-Change) |
| Command | چه کاری انجام بده؟ | Backend → TCU |
| Command ACK | دستور دریافت/پذیرفته شد (نه لزوماً اجرا شد) | TCU → Backend |

نتیجه واقعی اجرای Command از مسیر خودش (Event/Telemetry/Status) گزارش می‌شود، نه از طریق ACK.

---

## 5. Message Envelope

**[برداشت/پیشنهاد]**

```text
Message
├── Version        (نسخه Protocol - برای تغییرات آینده)
├── Type            (Event / Telemetry / Status / Command / CommandAck)
├── MessageID        (برای Trace و تشخیص Duplicate)
├── DeviceID         (مستقل از Authentication - بخش ۷)
├── Timestamp
└── Payload          (ساختار بسته به Type متفاوت است)
```

این Envelope مستقل از Transport است — چه روی MQTT چه TCP چه HTTP همین ساختار منطقی حمل می‌شود؛ فقط لایه Transport فرق می‌کند.

---

## 6. Topic / آدرس‌دهی (فقط در صورت انتخاب نهایی MQTT)

**[برداشت/پیشنهاد]**

یک Topic ثابت به‌ازای هر دستگاه (طبق ظرفیت تأییدشده فعلی هکار در سند فناپ)، نوع پیام داخل `Type` در Envelope مشخص می‌شود — نه از طریق Sub-Topic جدا.

**نکته مهم:** این بند فقط در صورتی معنا دارد که Transport نهایی MQTT باشد. برای TCP/HTTP، معادل آن همان فیلد `DeviceID` در Envelope است؛ نیازی به مفهوم جداگانه «Topic» نیست.

**محیط‌های شناخته‌شده هکار (از سند فناپ):**

| محیط | IP | MQTT Port | TCP Port | UDP Port |
|---|---|---|---|---|
| Sandbox | 185.172.3.70 | 9095 | 9090 | 9091 |
| Deployment | 95.38.182.115 | 5016 | 5015 | 5015 |
| Operation | 5.160.52.66 | 9096 | 9092 | 9093 |

این اطلاعات **زیرساخت فعلی هکار** است، نه Contract نهایی Firmware.

---

## 7. Device Identity و Authentication

**[قطعی]** Device Identity باید مستقل از Authentication دیده شود.

**[باز]** مبنای Identity (IMEI / Serial Number / Device ID اختصاصی)، روش Authentication (Username-Password / Certificate / JWT / سرویس خارجی — هرکدام که EMQX پشتیبانی می‌کند)، TLS، Credential Provisioning/Rotation/Revocation — همگی باید با هکار در Simulation کشف/توافق شوند.

---

## 8. Simulation — هدف و طرح کلی

**[قطعی]** هدف Simulation کشف Contract واقعی هکار است، نه صرفاً تأیید اتصال.

### سناریوهای لازم (برای هرکدام از سه Transport به‌صورت جدا)

```text
Connect → Authenticate → Send Event → Send Telemetry → Send Status
→ Receive Command → Send Command ACK
→ Disconnect → Reconnect → Timeout → Retry
```

### سؤالاتی که باید از هر Transport بیرون بکشیم

Authentication چطور انجام می‌شود؟ · Device چگونه شناخته می‌شود؟ · ACK چیست؟ · Disconnect چگونه تشخیص داده می‌شود؟ · Retry چگونه است؟ · Command از سمت Backend چگونه می‌رسد؟ · Duplicate چگونه تشخیص داده می‌شود؟ · حجم و Latency واقعی چقدر است؟ · محدودیت‌های Backend چیست؟

### جدول تصمیم نهایی (خالی — بعد از Simulation پر می‌شود)

| معیار | MQTT | TCP | HTTP |
|---|---|---|---|
| ارتباط دوطرفه | | | |
| Authentication | | | |
| حجم سربار | | | |
| Command Downlink | | | |
| Reconnect | | | |
| Offline Handling | | | |
| پیچیدگی پیاده‌سازی TCU | | | |
| پیچیدگی Backend | | | |
| محدودیت EC200U | | | |
| نتیجه نهایی | | | |

**🔴 [باز]** جای فیزیکی Simulator هنوز تعیین نشده: برنامه جدا روی PC (مستقل از کد Firmware) یا همان `firmware/` روی Host با یک Adapter واقعی MQTT/TCP/HTTP جایگزین EC200U. این تصمیم قبل از شروع پیاده‌سازی Simulator باید گرفته شود.

---

## 9. حجم داده (برآورد اولیه هکار، هنوز معتبر)

برآورد: ۱۵۰ بایت هر پیام × ۶ پیام در دقیقه × ضریب ۲ ≈ **۲.۶ مگابایت در روز**. این برآورد نشان می‌دهد حجم فعلاً به‌تنهایی دلیل کافی برای الزام به Binary نیست — اما باید بعد از مشخص شدن Envelope واقعی و سربار MQTT/TLS دوباره اندازه‌گیری شود.

---

## 10. الزامات غیرکارکردی (بدون تغییر)

Server Communication باید: Non-Blocking باشد؛ قابل تست Unit باشد؛ Fake/Stub داشته باشد؛ به Hardware واقعی وابسته نباشد؛ Retry محدود داشته باشد؛ در مسیر عادی Memory Allocation پویا نداشته باشد؛ Encoder و Transport قابل تعویض باشند؛ قابل Trace باشد؛ در قطع ارتباط Super Loop را متوقف نکند.

---

## 11. اصل کلیدی (بدون تغییر)

> **Server Management مسئول رساندن پیام است، نه مسئول معنی و اجرای عملیات آن.**

> **تغییر Transport یا Encoding یک تغییر زیرساخت ارتباطی است، نه تغییر معماری کل Firmware.**

---

## 12. فهرست کامل موارد باز (برای OPEN-ITEMS.md)

| # | مورد | وابسته به |
|---|---|---|
| 1 | Transport نهایی (MQTT/TCP/HTTP) | نتیجه Simulation |
| 2 | Encoding نهایی (JSON/Binary) | نتیجه Simulation |
| 3 | جای فیزیکی Simulator | تصمیم داخلی تیم |
| 4 | روش Authentication نهایی | توافق با هکار |
| 5 | مبنای Device Identity | توافق با هکار |
| 6 | TLS/Certificate Management | توافق با هکار |
| 7 | فیلدهای نهایی Status | نیاز محصول + هکار |
| 8 | Schema دقیق JSON/Binary هر نوع پیام | نتیجه Simulation |
| 9 | پارامترهای Timeout/Retry/Backoff | طراحی تفصیلی + تست واقعی |
| 10 | Topic Structure نهایی (در صورت انتخاب MQTT) | توافق با هکار |
