# پیوست فنی — قرارداد درخواست Reset در Firmware

## ۱. هدف

این پیوست، قرارداد معماری **Reset Request** در Firmware دستگاه TCU را مشخص می‌کند.

هدف، ایجاد یک مسیر استاندارد برای درخواست Reset توسط Managementها و متمرکز کردن **اجرای Reset** در System Management است.

این قرارداد در سطح Architecture و Interface تعریف می‌شود و وارد جزئیات Driver، Register، Sequence سخت‌افزاری یا الگوریتم Recovery نمی‌شود.

---

## ۲. اصل معماری

اصل حاکم بر Reset در Firmware:

> **هر Management مسئول تشخیص نیاز به Reset و انجام Recovery محلی است؛ در صورت نیاز به Reset، صرفاً Reset Request صادر می‌کند. System Management تنها مرجع مجاز برای اجرای System Reset و Module Reset است.**

بنابراین:

```text
Capability Management
        │
        │ Local Recovery
        ▼
   Recovery Failed
        │
        │ Reset Request
        ▼
┌──────────────────────┐
│  System Management   │
└──────────┬───────────┘
           │
           ▼
      Reset Execution
```

هیچ Management دیگری مجاز نیست مستقیماً Reset سخت‌افزاری MCU یا Module را اجرا کند.

---

# ۳. مسئولیت‌ها

## ۳-۱. Management درخواست‌کننده

Management مالک Capability مسئول است که:

1. مشکل را تشخیص دهد.
2. Recovery محلی خود را انجام دهد.
3. در صورت شکست Recovery، نیاز به Reset را تشخیص دهد.
4. Reset Request ایجاد کند.
5. درخواست را به System Management تحویل دهد.

Management درخواست‌کننده مسئول اجرای Reset نیست.

---

## ۳-۲. System Management

System Management مسئول:

* دریافت Reset Request
* اعتبارسنجی Request
* اعتبارسنجی Target
* کنترل مجاز بودن نوع Reset
* اجرای Reset

است.

System Management نباید منطق Recovery مربوط به Management درخواست‌کننده را اجرا یا جایگزین کند.

---

# ۴. ساختار Reset Request

ساختار پایه درخواست Reset به‌صورت زیر تعریف می‌شود:

```text
ResetRequest {
    ResetType
    Target
    Source
    Reason
    Timestamp
}
```

---

## ۴-۱. ResetType

نوع Reset مورد درخواست را مشخص می‌کند.

مقادیر پایه:

```text
SYSTEM_RESET
MODULE_RESET
```

### SYSTEM_RESET

درخواست راه‌اندازی مجدد کل MCU / Firmware.

### MODULE_RESET

درخواست Reset یک Module مشخص، مانند:

```text
EC200U
```

ساختار `ResetType` باید به‌صورت Enum یا مقدار کنترل‌شده تعریف شود و مقدار آزاد متنی نباشد.

---

## ۴-۲. Target

هدف Reset را مشخص می‌کند.

نمونه:

```text
ResetType = SYSTEM_RESET
Target    = MCU

ResetType = MODULE_RESET
Target    = EC200U
```

برای `SYSTEM_RESET` هدف به‌صورت پیش‌فرض MCU است.

برای `MODULE_RESET`، Target باید به‌صورت صریح مشخص شود.

Target باید از مجموعه Moduleهای قابل Reset سیستم انتخاب شود.

---

## ۴-۳. Source

Management ایجادکننده درخواست را مشخص می‌کند.

نمونه:

```text
GPS
SERVER
SMS
CALL
Firmware Update
DIAGNOSTIC
```

Source باید مقدار کنترل‌شده و قابل ردیابی باشد.

هدف از این فیلد:

* Traceability
* تشخیص مالک مشکل
* تحلیل Fault
* ثبت رخداد Reset

است.

---

## ۴-۴. Reason

علت درخواست Reset را مشخص می‌کند.

نمونه مقادیر:

```text
MODULE_UNRESPONSIVE
RECOVERY_FAILED
COMMUNICATION_STUCK
INITIALIZATION_FAILED
UNEXPECTED_STATE
```

Reason نیز باید به‌صورت Enum یا مجموعه مقادیر کنترل‌شده تعریف شود.

Reason نباید جایگزین توضیحات کامل Fault یا Log شود؛ تنها علت معماری Reset را مشخص می‌کند.

---

## ۴-۵. Timestamp

زمان ایجاد Reset Request را مشخص می‌کند.

Timestamp توسط Management درخواست‌کننده در زمان ایجاد Request ثبت می‌شود.

این فیلد برای:

* Trace
* Fault Analysis
* بررسی توالی رخدادها
* تحلیل Recovery

استفاده می‌شود.

---

# ۵. نمونه Request

برای مثال GPS پس از چند مرحله Recovery نتوانسته است Module مربوطه را به وضعیت عادی بازگرداند:

```text
ResetRequest {
    ResetType = MODULE_RESET
    Target    = EC200U
    Source    = GPS
    Reason    = RECOVERY_FAILED
    Timestamp = T
}
```

مسیر اجرا:

```text
GPS Management
      │
      ├── Detect Failure
      │
      ├── Local Recovery
      │
      └── Recovery Failed
              │
              ▼
        Reset Request
              │
              ▼
      System Management
              │
              ├── Validate
              ├── Validate Target
              │
              ▼
         Module Reset
```

---

# ۶. اعتبارسنجی Request

System Management هنگام دریافت Request باید حداقل موارد زیر را بررسی کند:

### ۶-۱. اعتبار ResetType

آیا نوع Reset تعریف‌شده و مجاز است؟

### ۶-۲. اعتبار Target

آیا Target وجود دارد و قابلیت Reset شدن دارد؟

### ۶-۳. تطابق Type و Target

برای مثال:

```text
SYSTEM_RESET + MCU       → Valid

MODULE_RESET + EC200U   → Valid

SYSTEM_RESET + EC200U   → Invalid
```

### ۶-۴. اعتبار Source

آیا Management درخواست‌کننده مجاز به درخواست چنین Resetای است؟

در صورت Invalid بودن Request، System Management آن را اجرا نمی‌کند و Fault مناسب ایجاد می‌شود.

---

# ۷. اجرای Reset

پس از اعتبارسنجی موفق:

```text
Receive Request
      ↓
Validate
      ↓
Validate Target
      ↓
Execute Reset
```

System Management نباید قبل از اجرای Reset وارد Recovery اختصاصی Capability شود.

برای مثال اگر Server Management درخواست Module Reset مربوط به EC200U را صادر کرده است، System Management نباید خودش تصمیم بگیرد که:

* چند بار اتصال را Retry کند؛
* PDP را Restart کند؛
* Socket را ببندد؛
* یا Session را دوباره ایجاد کند.

این موارد متعلق به مالک Capability است.

---

# ۸. مدل Non-Blocking

ایجاد Reset Request باید سریع و Non-Blocking باشد.

Management درخواست‌کننده نباید برای دریافت نتیجه Reset متوقف شود.

```text
Management
    │
    │ Reset Request
    ▼
System Management
    │
    └── return
```

به‌خصوص برای `SYSTEM_RESET`، مفهوم Response معمولاً وجود ندارد؛ زیرا سیستم پس از Reset مجدداً از Boot شروع می‌کند.

در `MODULE_RESET` نیز Management درخواست‌کننده نتیجه Reset را از طریق یک Response مستقیم دریافت نمی‌کند؛ بلکه پس از ادامه اجرای Super Loop، وضعیت Module را از مسیرهای عادی خود مشاهده می‌کند.

---

# ۹. عدم وجود Generic Result / Ack

برای Reset Request، قرارداد عمومی زیر تعریف نمی‌شود:

```text
ResetRequest
     ↓
ResetResponse
```

و همچنین Management درخواست‌کننده نباید منتظر:

```text
ACK
SUCCESS
FAILURE
```

بماند.

زیرا:

* در System Reset، سیستم مجدداً Boot می‌شود.
* در Module Reset، وضعیت Module از طریق Status/Events/Capability مربوطه قابل مشاهده است.
* اضافه کردن Result/Ack عمومی باعث ایجاد وابستگی غیرضروری بین Managementها می‌شود.

---

# ۱۰. مرز مسئولیت Recovery و Reset

تفکیک مسئولیت به‌صورت زیر است:

```text
┌─────────────────────────────┐
│ Capability Management       │
│                             │
│ Detect                      │
│ Local Recovery              │
│ Retry                       │
│ Decide Need for Reset       │
└──────────────┬──────────────┘
               │
               │ Reset Request
               ▼
┌─────────────────────────────┐
│ System Management           │
│                             │
│ Validate Request            │
│ Validate Target             │
│ Execute Reset               │
└─────────────────────────────┘
```

بنابراین:

> **تصمیم نیاز به Reset متعلق به Management مالک مشکل است؛ اجرای Reset متعلق به System Management است.**

---

# ۱۱. Contract سطح معماری

```text
Reset Request

Input:
    ResetType
    Target
    Source
    Reason
    Timestamp

Requester:
    Capability Management

Executor:
    System Management

Reset Types:
    SYSTEM_RESET
    MODULE_RESET

Constraints:
    Non-Blocking
    No Direct Hardware Reset by Requester
    No Generic Result/Ack
    Target Must Be Valid
    Source Must Be Authorized
```

---

# ۱۲. قواعد استفاده

### Rule 1

هر Management ابتدا باید Recovery محلی خود را انجام دهد.

### Rule 2

Reset نباید اولین مرحله Recovery باشد، مگر اینکه Contract همان Capability چنین رفتاری را الزام کند.

### Rule 3

هیچ Managementی مستقیماً MCU یا Module را Reset نمی‌کند.

### Rule 4

تمام Resetهای سیستمی از مسیر System Management اجرا می‌شوند.

### Rule 5

System Management منطق Recovery سایر Managementها را نمی‌داند.

### Rule 6

Reset Request باید قابل Trace باشد.

### Rule 7

Reset Request نباید باعث Blocking شدن Super Loop شود.

---

# ۱۳. اصل نهایی

معماری Reset بر این اصل استوار است:

> **Recovery محلی غیرمتمرکز است، اما اجرای Reset متمرکز است.**

یعنی هر Management مالک مشکل خود است و می‌تواند درباره نیاز به Reset تصمیم بگیرد؛ اما هیچ Managementی مالک اجرای Reset نیست.

**System Management تنها نقطه اعمال Reset در Firmware است.**
