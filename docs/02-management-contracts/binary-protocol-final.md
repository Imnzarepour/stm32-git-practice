# پیوست فنی — پروتکل باینری نهایی TCU ↔ Backend (بر اساس پیشنهاد فنی هکار)

**وضعیت سند:** این سند بسیاری از موارد `[باز]` در `server-management-backend-protocol.md` را به `[قطعی]` تبدیل می‌کند. هرجا این دو سند تناقض دارند، **این نسخه ارجح است**، چون از پیشنهاد فنی مشخص تیم Backend می‌آید.

---

## ۰. خلاصه تصمیمات — قطعی‌شده در این نسخه

| موضوع | تصمیم قبلی (باز) | تصمیم نهایی این سند |
|---|---|---|
| Encoding | JSON (Sandbox) → Binary (آینده) | **Binary از همین امروز** — هیچ فاز JSON در کار نیست |
| Transport | MQTT با یک Topic ثابت | **MQTT با ۴ Topic جدا** (`telemetry`, `events`, `cmd`, `response`) |
| Device Identity | نامشخص | **IMEI** — در مسیر Topic، نه در Payload |
| Message Envelope | مفهومی، ۶ فیلد | **هدر ۸ بایتی دقیق**: Version, MsgType, SeqID, Timestamp |
| انواع پیام | ۵ نوع (شامل Status مستقل) | **۴ نوع** — Status حذف و به‌صورت بیت‌فیلد داخل Telemetry ادغام شد |
| Authentication | نامشخص | IMEI + Username/Password (Deployment/Operation) |
| امنیت انتقال | نامشخص | TLS 1.2/1.3 روی پورت ۸۸۸۳، امکان mTLS |
| Command Safety | هیچ | **آستانه عددی مشخص: سرعت > ۵ km/h → رد با کد UNSAFE** |

---

## ۱. ساختار Topic (طبق محیط: Sandbox/Deployment/Operation)

```text
v1/device/{IMEI}/telemetry   ← دستگاه→سرور، دوره‌ای، QoS 0 یا 1
v1/device/{IMEI}/events      ← دستگاه→سرور، فوری، QoS 1
v1/device/{IMEI}/cmd         ← سرور→دستگاه، QoS 1
v1/device/{IMEI}/response    ← دستگاه→سرور، پاسخ فرمان، QoS 1
```

---

## ۲. هدر مشترک (۸ بایت، ابتدای همه Frameها)

| آفست | فیلد | نوع | شرح |
|---|---|---|---|
| 0x00 | Proto Version | uint8 | ثابت `0x01`؛ نسخه دیگر = Drop + Log خطا |
| 0x01 | Message Type | uint8 | `0x01`=Telemetry, `0x02`=Event, `0x10`=Command, `0x11`=Response |
| 0x02–0x03 | Sequence ID | uint16 (BE) | شمارنده بسته |
| 0x04–0x07 | Timestamp | uint32 (BE) | UTC Epoch (ثانیه) |

---

## ۳. Frame تله‌متری (۳۲ بایت، `msg_type=0x01`)

| آفست | فیلد | فرمول Decode | **مالک معنایی در Firmware** |
|---|---|---|---|
| 0x08–0x0B | Latitude | `/10^7` | GPS Management |
| 0x0C–0x0F | Longitude | `/10^7` | GPS Management |
| 0x10–0x11 | Heading | مستقیم | GPS Management |
| 0x12 | GPS Flags/Sats | بیت‌فیلد (بیت۷=Fix) | GPS Management |
| 0x13 | Vehicle Speed | مستقیم (km/h) | 🔴 نامشخص — Diagnostic (OBD) یا GPS؟ (بخش ۹-۱) |
| 0x14–0x15 | Engine RPM | مستقیم | Diagnostic Management (Read Parameter دوره‌ای) |
| 0x16 | Coolant Temp | `-40` | Diagnostic Management |
| 0x17 | Throttle Pos | مستقیم % | Diagnostic Management |
| 0x18 | Fuel Level | مستقیم % | Diagnostic Management |
| 0x19 | Battery Voltage | `*0.1` | Hardware Input Management |
| 0x1A | Engine Load | مستقیم % | Diagnostic Management |
| 0x1B–0x1C | Status Flags | بیت‌فیلد | ترکیبی: بیت۰=ACC (Hardware Input)، بیت۱=Check Engine (Diagnostic)، بیت۲-۵=وضعیت ۴ رله (Actuator Management) |
| 0x1D–0x1E | CRC16-CCITT | روی 0x00–0x1C | Server Management (محاسبه در لحظه ارسال) |

**نکته مهم:** بیشتر این فیلدها (RPM، دما، Throttle، Fuel، Load) دقیقاً همون داده‌ایه که از طریق **Read Parameter دوره‌ای Diagnostic Management** (که چند سند پیش تصمیم گرفتیم ~۲ بار در دقیقه اجرا بشه) تأمین می‌شه — یعنی Server Management باید این مقادیر رو از Diagnostic Management (نه مستقیم از CAN) بگیره.

---

## ۴. Frame رویداد فوری (۱۴ بایت، `msg_type=0x02`)

| آفست | فیلد | مقادیر | **مالک معنایی** |
|---|---|---|---|
| 0x08 | Event Code | `0x01`=سوئیچ, `0x02`=ضربه, `0x03`=قطع باطری | Hardware Input Management |
| 0x09 | Event State | بسته به Event Code | Hardware Input Management |
| 0x0A | Speed | لحظه وقوع | همون ابهام بخش ۹-۱ |
| 0x0B | Battery Voltage | `*0.1` | Hardware Input Management |
| 0x0C–0x0D | CRC | روی 0x00–0x0B | Server Management |

**نگاشت به Eventهای موجود ما:**
```text
Event Code 0x01 (سوئیچ) → EVENT_ACC_ON / EVENT_ACC_OFF
Event Code 0x02 (ضربه)  → EVENT_SHOCK_DETECTED
Event Code 0x03 (باطری) → EVENT_LOW_BATTERY_DETECTED یا یک Event جدید BATTERY_DISCONNECTED
```

---

## ۵. Frame فرمان سرور (`msg_type=0x10`)

| Command ID | عنوان | مقصد در Firmware |
|---|---|---|
| `0x0010` | کنترل ۴ رله | Actuator Management (از طریق Server Management → Actuator Request) |
| `0x0020` | بازه ارسال Telemetry (حرکت/پارک) | Configuration Infrastructure |
| `0x0030` | خانواده/پروفایل ECU | Diagnostic Management (ECU-specific Files، `diagnostic-management.md` بخش ۱۷) |
| `0x0040` | شماره‌های تماس مجاز | Configuration Infrastructure (`Allowed Phone Numbers`) |
| `0x00F0` | FOTA (سایز + URL) | FOTA Management — 🔴 نکته مهم در بخش ۹-۲ |

---

## ۶. Frame پاسخ (۱۴ بایت، `msg_type=0x11`)

| آفست | فیلد | مقادیر |
|---|---|---|
| 0x0A | Status Code | `0x00`=SUCCESS, `0x01`=INVALID_PARAM, **`0x02`=UNSAFE (سرعت>۵km/h)**, `0x03`=HARDWARE_ERR |
| 0x0B | Extra Data | وضعیت رله‌ها یا کد تکمیلی |

---

## ۷. CRC16-CCITT

چندجمله‌ای `0x1021`، مقدار اولیه `0xFFFF`، روی کل فریم به‌جز خود فیلد CRC، Big-Endian.

---

## ۸. امنیت

Sandbox: بدون Authentication. Deployment/Operation: IMEI + Username/Password. TLS 1.2/1.3 روی پورت 8883 (MQTTS)، CA Certificate روی دستگاه، امکان آینده mTLS + Certificate Pinning.

---

## ۹. نکات باز جدید — نیازمند تصمیم

### ۹-۱. منبع «Vehicle Speed» — GPS یا Diagnostic؟

این ابهام صرفاً فنی نیست — مستقیم روی **قابلیت اعتماد Command Safety Policy** اثر می‌ذاره (بخش ۹-۳). اگه Speed فقط از Diagnostic (OBD) بیاد و ارتباط CAN در اون لحظه مشکل داشته باشه، آستانه ایمنی ۵ km/h قابل ارزیابی نمی‌مونه. **پیشنهاد:** GPS به‌عنوان منبع اصلی و Fail-Safe (چون مستقل از سلامت CAN Bus هست)، Diagnostic به‌عنوان منبع دقیق‌تر وقتی در دسترسه.

### ۹-۲. مسیر انتقال FOTA — MQTT یا HTTP؟

Command `0x00F0` فقط **URL** فایل رو می‌فرسته، نه خود فایل. یعنی دانلود واقعی احتمالاً از یه اتصال **HTTP جدا** انجام می‌شه، نه از طریق MQTT Chunked Transfer که در `file-transfer-infrastructure.md` فرض کرده بودیم. **این باید با تیم Backend تأیید بشه** — اگه درسته، File Transfer Infrastructure باید یه HTTP Client هم پشتیبانی کنه (کار EC200U Manager، چون HTTP هم از طریق همون Cellular Data Service میاد).

### ۹-۳. محل پیاده‌سازی Command Safety Policy

آستانه (`>۵ km/h`) و کد پاسخ (`UNSAFE`) الان مشخصه، ولی این‌که این چک **کجای Firmware** انجام بشه (داخل خود Actuator Management قبل از Execute، یا یه لایه Policy جدا قبلش) هنوز طبق `OPEN-ITEMS.md` باز مونده — این سند فقط قرارداد بیرونی رو مشخص کرده، نه معماری داخلی.

### ۹-۴. تعداد واقعی رله — ۳ یا ۴؟

`Relay Mask (B0..B3)` صریحاً ۴ رله رو نشون می‌ده. این با تناقض قدیمی «۳ یا ۴ خروجی Actuator» گره می‌خوره — این سند به نفع **۴ تا** (یعنی احتمالاً `DOOR_LOCK`/`DOOR_UNLOCK` دو رله فیزیکی جدان) شهادت می‌ده، ولی باید با سخت‌افزار واقعی نهایی تأیید بشه.

---

## ۱۰. موارد باز به‌روزشده برای OPEN-ITEMS.md

| # | مورد | اولویت |
|---|---|---|
| 1 | منبع Vehicle Speed برای Telemetry و برای خود Command Safety Check (GPS/Diagnostic/هردو) | بالا — مستقیم روی ایمنی اثر دارد |
| 2 | تأیید HTTP به‌عنوان مسیر دانلود FOTA (نه MQTT)؛ نیاز به HTTP Client در EC200U Manager | بالا |
| 3 | محل دقیق پیاده‌سازی Command Safety Policy (کد داخلی) | بالا — مرز ممنوع #۴۶ در `ARCHITECTURE_CONTRACT.md` |
| 4 | تأیید نهایی ۴ رله (نه ۳) با سخت‌افزار واقعی | متوسط |
| 5 | نگاشت دقیق `Event Code=0x03` (قطع باطری) به Event Type فعلی یا یک Event جدید | پایین |
