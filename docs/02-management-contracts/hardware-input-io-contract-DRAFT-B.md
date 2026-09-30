# پیوست فنی — قرارداد خروجی‌های Hardware Input Management

## ۱. هدف

این پیوست، قرارداد قابل پیاده‌سازی خروجی‌های **Hardware Input Management** را مشخص می‌کند.

مدل خروجی بر پایه یک مفهوم یکپارچه است:

> **هر Input یک وضعیت معتبر جاری دارد؛ تغییر یا برقرار شدن یک وضعیت مشخص می‌تواند منجر به تولید Event شود.**

بنابراین State و Event دو مدل مستقل نیستند؛ Event از وضعیت و تغییرات آن استخراج می‌شود.

---

# ۲. مدل یکپارچه Input

مسیر عمومی پردازش Input:

```text
GPIO / ADC
     ↓
Acquisition
     ↓
Qualification / Conversion
     ↓
Current Input State
     │
     ├──────────────→ Consumer
     │
     └── State Change / Condition
                         ↓
                       Event
```

در این مدل:

* **State** وضعیت فعلی Input را نشان می‌دهد.
* **Event** وقوع یک تغییر یا Condition مشخص روی Input را نشان می‌دهد.
* Consumer برای دانستن وضعیت فعلی، State را می‌خواند.
* Event برای اطلاع از رخداد به Event Queue ارسال می‌شود.

---

# ۳. Input State Contract

هر Input پس از پردازش توسط Hardware Input Management دارای یک **Current State** است.

این State باید آخرین مقدار معتبر Input را نگهداری کند.

مثال:

```text
ACC    = ON
DOOR   = OPEN
SHOCK  = INACTIVE
```

برای Analog Input نیز State شامل مقدار مهندسی‌شده است:

```text
Battery Voltage = 12.4 V
```

---

# ۴. Digital Input State

برای Digital Input:

```c
typedef struct
{
    bool     value;
    bool     valid;
    uint32_t timestamp;
} DigitalInputState;
```

### `value`

وضعیت فعلی Input را مشخص می‌کند.

```text
0 = Inactive
1 = Active
```

معنای دقیق Active/Inactive برای هر Input در Contract همان Input مشخص می‌شود.

### `valid`

مشخص می‌کند آیا State فعلی معتبر است یا خیر.

### `timestamp`

زمانی که State فعلی معتبر شده یا آخرین تغییر معتبر آن ثبت شده است.

---

# ۵. دسترسی به Digital State

Consumer از Interface خواندنی استفاده می‌کند:

```c
DigitalInputState Input_GetDigitalState(DigitalInputId id);
```

این Interface باید:

* Read Only باشد.
* Non-Blocking باشد.
* بدون Side Effect باشد.
* باعث Acquisition جدید نشود.
* مستقیماً Hardware را نخواند.
* آخرین State معتبر را برگرداند.

مثال:

```c
DigitalInputState acc;

acc = Input_GetDigitalState(DIGITAL_INPUT_ACC);
```

---

# ۶. Analog Input State

برای Analog Input نیز آخرین مقدار مهندسی‌شده نگهداری می‌شود.

مثال:

```c
typedef struct
{
    uint32_t millivolt;
    bool     valid;
    uint32_t timestamp;
} BatteryVoltageState;
```

مثال مقدار:

```text
millivolt = 12400
valid     = true
timestamp = T
```

مقدار خام ADC در این Interface قابل مشاهده نیست.

Consumer نباید از موارد زیر اطلاع داشته باشد:

```text
ADC Resolution
Reference Voltage
Resistor Divider
Calibration
ADC Raw Value
```

---

# ۷. State Change

Hardware Input Management تغییر State را تشخیص می‌دهد.

برای مثال:

```text
ACC = OFF
     ↓
ACC = ON
```

این یک **State Transition** است.

در صورتی که این Transition در Event Catalog تعریف شده باشد، Event مربوطه ایجاد می‌شود:

```text
ACC OFF
   ↓
ACC ON
   ↓
ACC_ON Event
```

بنابراین:

> **Event نتیجه قابل مشاهده یک State Change یا Condition است؛ Event جایگزین State نیست.**

---

# ۸. Event Contract

Event تولیدشده مطابق Contract عمومی Event سیستم خواهد بود:

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
EventId   = ACC_ON
Timestamp = T
```

---

# ۹. ارتباط State و Event

رابطه این دو باید به‌صورت زیر باشد:

```text
Input
  ↓
Current State
  │
  ├── State remains unchanged
  │       ↓
  │   No Event
  │
  └── State changes / Condition occurs
          ↓
        Event
```

مثلاً:

```text
Door = CLOSED
       ↓
GPIO remains CLOSED
       ↓
Current State = CLOSED
       ↓
No Event
```

اما:

```text
Door = CLOSED
       ↓
Door becomes OPEN
       ↓
Current State = OPEN
       ↓
DOOR_OPENED Event
```

---

# ۱۰. Condition-Based Event

همه Eventها الزاماً ناشی از تغییر ساده `ON/OFF` نیستند.

یک Condition نیز می‌تواند Event ایجاد کند.

مثال Battery:

```text
Battery Voltage
      ↓
Current State = 11.7 V
      ↓
Threshold Condition
      ↓
LOW_BATTERY_DETECTED
```

در این حالت:

* مقدار Voltage همچنان State است.
* `LOW_BATTERY_DETECTED` یک Event است.

---

# ۱۱. Eventهای Hardware Input

نمونه Eventها:

```text
ACC_ON
ACC_OFF

DOOR_OPENED
DOOR_CLOSED

SHOCK_DETECTED

LOW_BATTERY_DETECTED
```

این Eventها از State و Conditionهای مربوط به Inputها استخراج می‌شوند.

لیست نهایی Eventها در Event Catalog سیستم تعریف خواهد شد.

---

# ۱۲. Event Delivery

Hardware Input Management پس از تشخیص Event، آن را در **Event Queue** قرار می‌دهد.

```text
Hardware Input Management
          │
          │ State Change / Condition
          ▼
         Event
          │
          ▼
     Event Queue
          │
          ▼
    Event Dispatcher
          │
          ▼
        Policy
```

Hardware Input Management مسئول Event Policy نیست.

---

# ۱۳. عدم اجرای Action توسط Hardware Input Management

Hardware Input Management پس از تولید Event نباید مستقیماً Action انجام دهد.

برای مثال:

```text
SHOCK_DETECTED
```

نباید مستقیماً باعث اجرای:

```text
SendSMS()
MakeCall()
SendToServer()
```

در Hardware Input Management شود.

مسیر صحیح:

```text
SHOCK_DETECTED
       ↓
Event Queue
       ↓
Event Policy
       ├── Server
       ├── SMS
       ├── Call
       └── Store
```

---

# ۱۴. DigitalInputId

هر Digital Input دارای شناسه مشخص است:

```c
typedef enum
{
    DIGITAL_INPUT_ACC,
    DIGITAL_INPUT_DOOR,
    DIGITAL_INPUT_SHOCK
} DigitalInputId;
```

لیست نهایی Inputها بخشی از Configuration سیستم خواهد بود.

---

# ۱۵. AnalogInputId

هر Analog Input نیز دارای شناسه مشخص است:

```c
typedef enum
{
    ANALOG_INPUT_BATTERY_VOLTAGE
} AnalogInputId;
```

در صورت وجود Analog Inputهای دیگر، به همین مجموعه اضافه خواهند شد.

---

# ۱۶. رفتار زمانی

`GetState()` صرفاً State موجود را برمی‌گرداند.

نباید چنین رفتاری وجود داشته باشد:

```text
GetState()
    ↓
Wait
    ↓
Read GPIO / ADC
    ↓
Process
    ↓
Return
```

رفتار صحیح:

```text
Hardware
    ↓
Process()
    ↓
Acquire
    ↓
Qualify / Convert
    ↓
Update Current State
    ↓
GetState()
```

بنابراین:

> **خواندن State هیچ‌گاه باعث Acquisition جدید نمی‌شود.**

---

# ۱۷. State در برابر Event

برای جلوگیری از ابهام در مصرف Interface:

| مفهوم        | معنی                                          |
| ------------ | --------------------------------------------- |
| State        | وضعیت فعلی Input                              |
| State Change | تغییر وضعیت Input                             |
| Condition    | برقرار شدن یک شرط روی Input                   |
| Event        | رخداد قابل انتشار حاصل از Change یا Condition |
| Policy       | تصمیم درباره اقدام پس از Event                |

مثال:

```text
ACC = ON
        ↑
       State

ACC OFF → ACC ON
        ↑
   State Change

ACC_ON
        ↑
      Event

ACC_ON → NORMAL Policy
        ↑
      Policy
```

---

# ۱۸. Contract نهایی خروجی‌ها

```text
Hardware Input Management

Digital Input:
    Input_GetDigitalState(id)
        ↓
    DigitalInputState

Analog Input:
    Input_GetAnalogState(id)
        ↓
    Engineering Value State

State:
    Current Value
    Validity
    Timestamp

State Change / Condition:
        ↓
      Event
        ↓
    Event Queue

Rules:
    Read Only
    Non-Blocking
    No Hardware Access by Consumer
    No Acquisition on Get
    No Event Policy Execution
    No Action Execution
```

---

# ۱۹. نمونه استفاده توسط Power Management

Power Management وضعیت ACC را می‌خواند:

```c
DigitalInputState acc;

acc = Input_GetDigitalState(DIGITAL_INPUT_ACC);

if (acc.valid)
{
    if (acc.value)
    {
        /* NORMAL */
    }
    else
    {
        /* LOW_POWER */
    }
}
```

Power Management نیازی به دریافت `ACC_ON` یا `ACC_OFF` برای تعیین وضعیت جاری ندارد.

---

# ۲۰. نمونه استفاده از Event

برای رخداد Door:

```text
Door State
    ↓
CLOSED
    ↓
OPEN
    ↓
DOOR_OPENED Event
    ↓
Event Queue
```

Consumer دیگری که فقط وضعیت فعلی Door را نیاز دارد:

```c
door = Input_GetDigitalState(DIGITAL_INPUT_DOOR);
```

در نتیجه دو نیاز متفاوت با یک مدل State/Transition پوشش داده می‌شوند.

---

# ۲۱. اصل نهایی

مدل خروجی Hardware Input Management به‌صورت یکپارچه تعریف می‌شود:

```text
Physical Input
      ↓
Qualification / Conversion
      ↓
Current State
      │
      ├──────────────→ State Consumer
      │
      └── Change / Condition
                   ↓
                 Event
                   ↓
              Event Queue
```

و اصل معماری:

> **State پاسخ سؤال «الان چه وضعیتی داریم؟» است؛ Event پاسخ سؤال «چه رخدادی اتفاق افتاد؟» است. Event از State و تغییرات یا شرایط آن استخراج می‌شود، نه اینکه یک مسیر مستقل و بی‌ارتباط با State باشد.**
