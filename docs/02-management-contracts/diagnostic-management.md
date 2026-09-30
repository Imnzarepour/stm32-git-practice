# پیوست فنی — تعریف و قرارداد Diagnostic Management

## ۰. یادداشت هماهنگی (این بازبینی چه چیزی را بست)

**تصمیم قطعی: CAN اختصاصی Diagnostic Management است — نه مشترک.**

نسخه قبلی این سند کلمه «Arbitration» را بدون تصمیم معماری پشت آن وارد کرده بود، که به‌اشتباه القا می‌کرد ممکن است یک مصرف‌کننده دوم (مثلاً یک Passive CAN Data Provider برای RPM/Speed) هم به CAN دسترسی داشته باشد. این موضوع بررسی و رد شد:

> **در TCU هیچ مصرف‌کننده دیگری غیر از Diagnostic Management از CAN استفاده نمی‌کند.** نیاز به داده‌های دوره‌ای مثل RPM/Speed از طریق **Read Parameter دوره‌ای** (حدود ۲ بار در دقیقه، زمان‌بندی داخل خود Diagnostic) پوشش داده می‌شود، نه از طریق یک مسیر Passive جداگانه.

بنابراین Arbitration، CAN Sharing و CAN Data Provider **از معماری حذف شدند**. Hardware Proxy Contract فقط برای این است که دسترسی تیم Diagnostic به سخت‌افزار CAN/K-Line، Non-Blocking، قابل تست و بدون آسیب به سایر بخش‌های Firmware باشد — نه برای تقسیم دسترسی بین چند مصرف‌کننده.

---

## ۱. هدف

ایجاد یک مرز مشخص میان Main Firmware و پیاده‌سازی تخصصی Diagnostic؛ به‌گونه‌ای که:

* Main Firmware بتواند عملیات Diagnostic را درخواست کند.
* وضعیت شروع، اجرا و پایان عملیات قابل مشاهده باشد.
* نتیجه بدون Blocking در اختیار Firmware قرار گیرد.
* در هر لحظه فقط یک Operation اجرا شود؛ درخواست بعدی در یک Slot محدود (حداکثر ۱ Pending) منتظر بماند؛ درخواست اضافی صریحاً Reject شود.
* دسترسی به CAN/K-Line فقط از طریق Hardware Proxy Contract انجام شود.
* ECU-specific Files بدون Rebuild Firmware قابل تغییر باشند.

این سند وارد جزئیات داخلی Protocol، FSM، UDS، CAN، K-Line، ECU فایل‌ها یا الگوریتم‌های Diagnostic نمی‌شود.

---

## ۲. جایگاه Diagnostic Management

> **تیم Diagnostic مالک اجرای عملیات Diagnostic است؛ Architecture مالک مرز و Contract آن است.**

```text
Main Firmware
      │
      ▼
Diagnostic Contract
      │
      ▼
Diagnostic Management
      │
      ├── Diagnostic Engine
      ├── ECU Configuration / Files
      ├── Parameter Read (دوره‌ای + بر اساس Request)
      ├── DTC Read
      ├── DTC Clear
      └── Diagnostic State Machine
              │
              ▼
      Hardware Proxy Contract   ← اختصاصی Diagnostic، بدون Arbitration
              │
          ┌───┴───┐
          ▼       ▼
         CAN     K-Line
```

---

## ۳. مالکیت Diagnostic

تیم Diagnostic مسئول کامل: ارتباط با ECU، استفاده از CAN/K-Line، Protocol، ECU-specific Files، Read Parameter، Read DTC، Clear DTC، Timeout/Retry/Recovery داخلی، State داخلی، Sequence، تولید Result، تست پیاده‌سازی. Main Firmware فقط از Contract بیرونی استفاده می‌کند.

---

## ۴. عملیات مجاز

```text
READ_PARAMETER   ← شامل هم درخواست بیرونی، هم زمان‌بندی دوره‌ای داخلی (~۲ بار/دقیقه)
READ_DTC
CLEAR_DTC
```

در هر لحظه فقط یک Operation در حال اجراست. فهرست می‌تواند در آینده توسعه یابد؛ هر Operation جدید Contract مستقل خود را دارد.

**نکته Read Parameter دوره‌ای:** زمان‌بندی این خواندن دوره‌ای (چه زمانی، با چه فاصله‌ای) کاملاً داخل Diagnostic Management تصمیم‌گیری می‌شود؛ Main Firmware فقط نتیجه (از طریق همان مسیر Telemetry/GetResponse عادی) را مصرف می‌کند، نیازی به Trigger صریح از بیرون برای این حالت نیست.

---

## ۵. اصل Non-Blocking

```text
Request → Accept → RUNNING → Diagnostic_Process() → Progress → ... → SUCCESS / FAILED
```

ممنوع: `while(!complete) { wait; }`، Delay، انتظار Blocking برای CAN/K-Line/ECU/Timeout/Retry. State بین فراخوانی‌های `Diagnostic_Process()` حفظ می‌شود.

---

## ۶. Lifecycle Operation

```text
IDLE → (Submit Request) → RUNNING → SUCCESS / FAILED
```

`SUCCESS`/`FAILED` یعنی Result آماده مصرف است؛ این‌ها با State داخلی FSM تیم Diagnostic یکی نیستند و آن FSM هرگز به Main Firmware نشت نمی‌کند.

---

## ۷. Request Contract

```c
DiagnosticRequest request = { .operation = DIAG_OP_READ_PARAMETER, .target = ecu_id, .parameter = parameter_id };
DiagnosticSubmitResult result = Diagnostic_SubmitRequest(&request);
```

(همین امضا برای `DIAG_OP_READ_DTC`/`DIAG_OP_CLEAR_DTC` هم به‌کار می‌رود؛ `Diagnostic_SubmitRequest` یک اشاره‌گر به `DiagnosticRequest` می‌گیرد، نه آرگومان‌های جدا — پیاده‌سازی واقعی: `firmware/include/managements/diagnostic_management.h`.)

Main Firmware هرگز CAN Frame، CAN ID، UDS Service، K-Line Sequence، AT Command، ECU File Format یا Protocol State را مشخص نمی‌کند.

---

## ۸. پذیرش درخواست — مدل Current + Pending (بدون Queue عمومی)

Diagnostic دو Slot منطقی دارد: `Current Operation` و `Pending Request` — **این یک Queue عمومی نیست.**

**حالت ۱ — Current آزاد (`IDLE`):** درخواست مستقیم به Current منتقل و `RUNNING` می‌شود؛ `Diagnostic_SubmitRequest` مقدار `DIAG_SUBMIT_ACCEPTED` برمی‌گرداند.

**حالت ۲ — Current مشغول (`RUNNING`/`SUCCESS`/`FAILED` مصرف‌نشده)، Pending خالی:** درخواست در Pending قرار می‌گیرد؛ بازگشت `DIAG_SUBMIT_PENDING`.

**حالت ۳ — Current مشغول، Pending هم پر:** درخواست جدید صریحاً `DIAG_SUBMIT_BUSY` برمی‌گرداند. Drop شدن بی‌صدا، Overwrite یا صف نامحدود مجاز نیست.

> **نکته کلیدی:** `SUCCESS`/`FAILED` مصرف‌نشده هم مثل `RUNNING` «اشغال» حساب می‌شود — چون تا وقتی Result خوانده نشده، Slot واقعاً آزاد نیست.

---

## ۹. Result تا زمان Consume

```text
Operation → SUCCESS → Result Slot → Diagnostic_GetResponse() → Consumed → Current = IDLE
```

تا مصرف نشدن Result، Current همچنان Occupied است؛ Result جدید روی Result قبلی نوشته نمی‌شود.

---

## ۱۰. GetResponse

```c
bool Diagnostic_GetResponse(DiagnosticResponse* response);
```

```text
Current ∈ {SUCCESS, FAILED} → Copy Response → Current = IDLE → return true
Current ∈ {IDLE, RUNNING}   → return false
```

**Contract الزامی برای Consumerها:** هر Managementی که Diagnostic Request می‌فرستد، موظف است به‌طور منظم `GetResponse()` را Poll کند؛ در غیر این صورت Pending Request بعدی تا ابد معطل می‌ماند. (تصمیم درباره افزودن Timeout خودکار روی Result مصرف‌نشده، در صورت نیاز واقعی، به تیم Diagnostic واگذار می‌شود.)

---

## ۱۱. اجرای Pending

Pending فقط بعد از یک Consume واقعی (نه صرفاً پایان اجرا) وارد اجرا می‌شود:

```text
Current=SUCCESS, Pending=READ_DTC → GetResponse() → Current=IDLE, Pending=READ_DTC
      ↓ (در Diagnostic_Process بعدی)
Current=READ_DTC(RUNNING), Pending=Empty
```

---

## ۱۲. مدل کامل Operation

```text
        Submit Request
              │
              ▼
        ┌───────────┐
        │   IDLE    │
        └─────┬─────┘
              ▼
        ┌───────────┐
        │  RUNNING  │
        └─────┬─────┘
     ┌────────┴────────┐
     ▼                 ▼
 SUCCESS            FAILED
     └────────┬────────┘
              ▼
        GetResponse()
              ▼
            IDLE
```

در هر لحظه که Current اشغال باشد، حداکثر یک Pending مجاز است.

---

## ۱۳. رفتار در برابر درخواست‌های متوالی

```text
T1: Read Parameter → RUNNING  (DIAG_SUBMIT_ACCEPTED)
T2: Read DTC       → PENDING  (DIAG_SUBMIT_PENDING)
T3: Clear DTC      → DIAG_SUBMIT_BUSY
```

ظرفیت منطقی: `1×Current + 1×Pending`، نه بیشتر — عمداً برای جلوگیری از Queue و پیچیدگی غیرضروری.

---

## ۱۴. Parameter

```text
READ_PARAMETER + Parameter ID
```

Main Firmware از Protocol یا ECU-specific implementation بی‌اطلاع است. Result می‌تواند شامل `Parameter ID, Value, Unit/Encoding, Validity, Timestamp, Operation Result` باشد؛ فرمت نهایی بر اساس نیاز واقعی Parameterها تعیین می‌شود.

---

## ۱۵. DTC Read

```text
Main Firmware → READ_DTC → Diagnostic → RUNNING → ECU Communication → DTC Result → SUCCESS/FAILED
```

Main Firmware هرگز Frameهای Diagnostic را مستقیم تفسیر نمی‌کند.

---

## ۱۶. DTC Clear

Lifecycle کامل مثل بقیه عملیات؛ `SUCCESS` فقط وقتی اعلام می‌شود که Diagnostic طبق Contract خودش اجرای موفق Clear را تأیید کرده باشد.

---

## ۱۷. ECU-specific Files

```text
External NOR → DIAG Partition → ECU Files → Diagnostic Management
```

برای تغییر ECU هدف: `New ECU Files → DIAG Partition + Configuration Change`، بدون Rebuild Firmware.

**نکته هماهنگی با `file-transfer-infrastructure.md`:** اگر این فایل‌ها قرار است از راه دور (نه فقط تولید) به‌روزرسانی شوند، مسیر منطقی همان الگوی مثال بخش ۱۶ آن سند است: `Server → File Transfer Infrastructure → DIAG Partition → Diagnostic Management (تفسیر)`. این جزئیات هنوز به‌صورت رسمی بین دو سند Cross-Reference نشده و باید در تکمیل بعدی اضافه شود.

---

## ۱۸. مالکیت فایل‌های Diagnostic

Format، Version، Consistency، Compatibility، Interpretation، ECU Mapping، Parameter/DTC Definition — همه با تیم Diagnostic است. Memory/Storage Infrastructure فقط سرویس ذخیره‌سازی می‌دهد.

---

## ۱۹. CAN / K-Line — اختصاصی، بدون Arbitration

```text
Diagnostic → Hardware Proxy Contract → CAN/K-Line Driver → Hardware
```

Diagnostic بدون Contract مشخص مستقیماً GPIO، CAN Register، Peripheral Register، DMA یا Interrupt را کنترل نمی‌کند. **هیچ Management دیگری در Firmware از CAN استفاده نمی‌کند** — این یک تصمیم قطعی است، نه یک محدودیت موقت.

---

## ۲۰. جلوگیری از تداخل با سایر Firmware

Non-Blocking · Super Loop Compatible · زمان اجرای محدود هر `Process()` · بدون انتظار Blocking برای ECU · بدون Delay · بدون تصاحب دائمی CPU · بدون دستکاری منابع سایر Managementها · از طریق Hardware Proxy Contract.

---

## ۲۱. Power Mode

Power Management تعیین‌کننده Mode است؛ Diagnostic مسئول رفتار خودش در هر Mode. جزئیات این‌که کدام Operation در `LOW_POWER` مجاز است، در Contract عملیاتی Diagnostic مشخص می‌شود. Diagnostic خودش Power Mode را تغییر نمی‌دهد.

---

## ۲۲. Fault و Failure

```text
Diagnostic Failure → Local Recovery → Operation FAILED → Diagnostic Response
```

در صورت نیاز به Recovery سطح Module/System: `Diagnostic → Reset Request → System Management → Reset`. Diagnostic مستقیماً MCU/سیستم را Reset نمی‌کند.

---

## ۲۳. Timeout و Retry

متعلق به تیم Diagnostic؛ تعداد Retry و Timeoutهای Protocol در این Contract تعیین نمی‌شوند، اما نتیجه نهایی همیشه به شکل `SUCCESS`/`FAILED` قابل مشاهده است.

---

## ۲۴. State داخلی

FSM داخلی تیم Diagnostic (مثل `START→SEND REQUEST→WAIT RESPONSE→VALIDATE→RETRY→COMPLETE`) Contract عمومی Firmware نیست.

> **Architecture رفتار قابل مشاهده را تعریف می‌کند؛ Diagnostic Implementation روش رسیدن به آن رفتار را تعیین می‌کند.**

---

## ۲۵. Event

```text
Diagnostic → Event/Fault (مثل DIAGNOSTIC_FAILURE, ECU_UNAVAILABLE) → Event Policy
```

Diagnostic خودش Event Policy را اجرا نمی‌کند (مثلاً تصمیم «DTC Failure → Send SMS» متعلق به Event Policy است، نه Diagnostic).

---

## ۲۶. موارد باز برای OPEN-ITEMS.md

| # | مورد |
|---|---|
| 1 | Cross-Reference رسمی بین DIAG Partition (این سند) و File Transfer Infrastructure برای به‌روزرسانی از راه دور ECU Files |
| 2 | نیاز احتمالی به Timeout خودکار روی Result مصرف‌نشده (جلوگیری از Starvation Pending) — تصمیم با تیم Diagnostic |

~~۳. Header کد باید به‌روزرسانی شود~~ — **انجام و کامیت شد**: `firmware/include/managements/diagnostic_management.h`/`diagnostic_management.c` طبق همین مدل (Current+Pending، `DiagnosticSubmitResult{DIAG_SUBMIT_ACCEPTED, DIAG_SUBMIT_PENDING, DIAG_SUBMIT_BUSY}`) پیاده و با ۲۲ تست (`test_diagnostic_management.c`) تأیید شده‌اند؛ CAN از طریق Domain جدید Hardware Proxy (`hardware_proxy/hardware_proxy_can.h`) - نگاه کنید `docs/03-infrastructure-contracts/hardware-proxy.md`.
