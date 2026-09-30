# پیوست فنی — قرارداد خروجی‌های Hardware Input Management

## ۱. هدف

این پیوست، قرارداد قابل پیاده‌سازی خروجی‌های **Hardware Input Management** را مشخص می‌کند.

خروجی‌های این Management به سه گروه تقسیم می‌شوند:

* Digital Latest Value
* Analog Engineering Value
* Input Event

هدف این Contract آن است که سایر Managementها بدون دسترسی مستقیم به GPIO، ADC یا Driver، بتوانند داده‌های ورودی سیستم را مصرف کنند.

---

# ۲. اصل دسترسی

Hardware Input Management تنها مالک Inputهای فیزیکی است.

Consumerها فقط از طریق Interface تعریف‌شده به داده دسترسی دارند.

```text
GPIO / ADC
     ↓
Hardware Input Management
     ↓
┌───────────────┬────────────────┐
│ Latest Value  │ Event          │
└───────┬───────┴───────┬────────┘
        │               │
        ▼               ▼
    Consumer        Event Policy
```

هیچ Consumer مجاز نیست مستقیماً به:

```text
GPIO
ADC
GPIO Driver
ADC Driver
```

دسترسی داشته باشد.

---

# ۳. Digital Latest Value Contract

برای Digital Inputها، آخرین وضعیت معتبر با یک Interface خواندنی در اختیار Consumer قرار می‌گیرد.

## ۳-۱. Interface

```c
DigitalInputValue GetDigitalInput(DigitalInputId id);
```

یا معادل آن:

```c
GetDigitalInput(id, &value);
```

Interface باید:

* Non-Blocking باشد.
* Read Only باشد.
* بدون Side Effect باشد.
* باعث Acquisition جدید نشود.
* مستقیماً Hardware را نخواند.
* آخرین مقدار معتبر را برگرداند.

---

# ۴. DigitalInputId

هر Digital Input باید یک شناسه یکتا داشته باشد.

نمونه:

```c
typedef enum
{
    DIGITAL_INPUT_ACC,
    DIGITAL_INPUT_DOOR,
    DIGITAL_INPUT_SHOCK
} DigitalInputId;
```

لیست نهایی Inputها در Configuration سیستم تعیین می‌شود.

---

# ۵. DigitalInputValue

پیشنهاد می‌شود مقدار Digital فقط `bool` نباشد؛ زیرا Consumer به زمان آخرین تغییر و اعتبار داده نیز ممکن است نیاز داشته باشد.

```c
typedef struct
{
    bool     value;
    bool     valid;
    uint32_t timestamp;
} DigitalInputValue;
```

### فیلدها

**value**

وضعیت فعلی Input:

```text
0 = Inactive
1 = Active
```

معنای Active/Inactive برای هر Input در Contract همان Input تعریف می‌شود.

**valid**

مشخص می‌کند آیا مقدار فعلی معتبر است یا خیر.

**timestamp**

زمانی که این مقدار معتبر شده یا آخرین بار Update شده است.

---

# ۶. مثال ACC

```c
DigitalInputValue acc;

acc = GetDigitalInput(DIGITAL_INPUT_ACC);
```

نتیجه:

```text
value     = 1
valid     = true
timestamp = T
```

Power Management فقط وضعیت را مصرف می‌کند:

```text
Hardware Input Management
          ↓
       ACC Status
          ↓
   Power Management
```

---

# ۷. Analog Engineering Value Contract

برای Analog Input، خروجی باید مقدار مهندسی‌شده باشد.

Interface:

```c
AnalogInputValue GetAnalogInput(AnalogInputId id);
```

---

# ۸. AnalogInputId

هر Analog Input یک شناسه مشخص دارد.

مثال:

```c
typedef enum
{
    ANALOG_INPUT_BATTERY_VOLTAGE
} AnalogInputId;
```

---

# ۹. AnalogInputValue

مقدار Analog نباید الزاماً به صورت `float` در Contract تعریف شود.

پیشنهاد معماری:

```c
typedef struct
{
    int32_t  value;
    int32_t  scale;
    bool     valid;
    uint32_t timestamp;
} AnalogInputValue;
```

اما برای جلوگیری از پیچیدگی غیرضروری، **واحد و Scale بهتر است در Contract خود Input مشخص باشد**.

بنابراین برای Battery Voltage می‌توان Contract مشخص‌تری داشت:

```c
typedef struct
{
    uint32_t millivolt;
    bool     valid;
    uint32_t timestamp;
} BatteryVoltage;
```

مثال:

```text
millivolt = 12400
valid     = true
timestamp = T
```

در نتیجه Consumer نیازی به دانستن:

```text
ADC Raw
Reference Voltage
Resistor Divider
ADC Resolution
Calibration
```

ندارد.

---

# ۱۰. Low Battery Event

Low Battery توسط Hardware Input Management تشخیص داده می‌شود.

تشخیص:

```text
Battery Voltage
       ↓
Threshold / Qualification
       ↓
Low Battery Condition
       ↓
Event
```

Hardware Input Management صرفاً Event را ایجاد می‌کند.

این Manager تصمیم نمی‌گیرد که با Event چه اقدامی انجام شود.

---

# ۱۱. Event Contract

Event خروجی باید مطابق Event Contract عمومی سیستم باشد:

```c
typedef struct
{
    EventType event_type;
    uint32_t  event_id;
    uint32_t  timestamp;
} Event;
```

مثال:

```text
EventType = INPUT_EVENT
EventId   = LOW_BATTERY_DETECTED
Timestamp = T
```

---

# ۱۲. Eventهای Hardware Input

نمونه Eventهای قابل تولید:

```c
ACC_ON
ACC_OFF

DOOR_OPENED
DOOR_CLOSED

SHOCK_DETECTED

LOW_BATTERY_DETECTED
```

این فهرست نمونه است و Eventهای نهایی باید در Event Catalog سیستم تعریف شوند.

---

# ۱۳. Event تولیدکننده، Policy نیست

Hardware Input Management فقط Event را تولید می‌کند:

```text
Input
  ↓
Detection
  ↓
Event
```

و سپس:

```text
Event
  ↓
Event Policy
  ├── Server
  ├── SMS
  ├── Call
  ├── Store
  └── None
```

بنابراین Hardware Input Management نباید مثلاً هنگام `SHOCK_DETECTED` مستقیماً:

```text
SendSMS()
Call()
SendToServer()
```

را اجرا کند.

---

# ۱۴. Event Delivery

Eventهای Hardware Input از طریق **Event Queue** عمومی سیستم تحویل داده می‌شوند.

```text
Hardware Input Management
          │
          │ Event
          ▼
     Event Queue
          │
          ▼
    Event Dispatcher
          │
          ▼
       Policy
```

Hardware Input Management مسئول اجرای Event Policy نیست.

---

# ۱۵. EventId

هر Event باید دارای `EventId` یکتا باشد.

`EventId` باید امکان Trace کردن رخداد را فراهم کند.

مثال:

```text
LOW_BATTERY_DETECTED
DOOR_OPENED
SHOCK_DETECTED
```

در صورت نیاز به Trace دقیق‌تر، `EventId` می‌تواند یک شناسه یکتای Instance نیز داشته باشد؛ اما این موضوع در Event Contract عمومی سیستم تعیین می‌شود و در این Management تکرار نمی‌شود.

---

# ۱۶. وضعیت در دسترس بودن داده

Consumer باید بتواند تشخیص دهد داده معتبر است یا خیر.

برای Digital:

```c
value.valid
```

برای Analog:

```c
value.valid
```

در صورت `valid == false`، Consumer نباید مقدار را به‌عنوان مقدار معتبر فیزیکی مصرف کند.

---

# ۱۷. رفتار زمانی

`Get...()` فقط Latest Value را برمی‌گرداند.

نباید:

```text
Get()
 ↓
Wait for ADC
 ↓
Read Hardware
 ↓
Return
```

اتفاق بیفتد.

مدل صحیح:

```text
Hardware
   ↓
Process()
   ↓
Acquire / Qualify / Convert
   ↓
Latest Value
   ↓
Get()
```

بنابراین:

> **Get API هرگز باعث Acquisition نمی‌شود.**

---

# ۱۸. Contract نهایی خروجی‌ها

```text
Hardware Input Management

Digital:
    GetDigitalInput(id)
        ↓
    DigitalInputValue

Analog:
    GetAnalogInput(id)
        ↓
    Engineering Value

Event:
    Input Detection
        ↓
    Event Queue

DigitalInputValue:
    value
    valid
    timestamp

Analog Value:
    engineering value
    valid
    timestamp

Rules:
    Read Only
    Non-Blocking
    No Hardware Access by Consumer
    No Acquisition on Get
    No Event Policy Execution
```

---

# ۱۹. نمونه استفاده توسط سایر Managementها

### Power Management

```c
DigitalInputValue acc;

acc = GetDigitalInput(DIGITAL_INPUT_ACC);

if (acc.valid)
{
    if (acc.value)
        /* NORMAL */;
    else
        /* LOW_POWER */;
}
```

### Server Management

```c
BatteryVoltage battery;

battery = GetBatteryVoltage();

if (battery.valid)
{
    /* Telemetry */
}
```

### Event Policy

```text
SHOCK_DETECTED
      ↓
Event Queue
      ↓
Event Policy
      ↓
Server / SMS / Call / Store
```

---

# ۲۰. اصل نهایی

خروجی Hardware Input Management باید برای Consumerها به سه شکل قابل دسترسی باشد:

```text
Digital Input
     ↓
Latest Value

Analog Input
     ↓
Engineering Value

Input Condition
     ↓
Event
```

و اصل مهم‌تر:

> **Consumer داده را می‌خواند؛ Acquisition توسط Hardware Input Management انجام می‌شود.**

بنابراین هیچ Consumerی نباید برای دریافت یک Input، باعث اجرای فرآیند سخت‌افزاری، انتظار، Blocking یا Acquisition جدید شود.
