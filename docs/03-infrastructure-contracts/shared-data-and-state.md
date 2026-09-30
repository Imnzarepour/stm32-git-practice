# پیوست فنی — تعریف داده‌ها و وضعیت‌های مشترک سیستم

## 1. هدف

این بخش مجموعه داده‌ها و وضعیت‌هایی را تعریف می‌کند که بیش از یک Management در Firmware به آن‌ها نیاز دارد.

این داده‌ها باید:

* یک مالک مشخص داشته باشند.
* یک منبع معتبر داشته باشند.
* از طریق Contract قابل دسترسی باشند.
* توسط چند Management به‌صورت مستقل کپی و نگهداری نشوند.
* در صورت نیاز Timestamp و Validity داشته باشند.
* مستقیماً به‌صورت Global Variable قابل تغییر توسط همه Managementها نباشند.

اصل:

> **Shared Data یک منبع مشترک است، نه یک Global Variable آزاد.**

---

# 2. مدل مالکیت

```text
                Shared System Data
                       │
          ┌────────────┴────────────┐
          │                         │
      Owner Management          Infrastructure
          │                         │
          ▼                         ▼
      Latest State              System Time
          │
          ▼
      Read Contract
          │
     ┌────┼────┐
     ▼    ▼    ▼
   GPS Server Power
```

فقط Owner مجاز به تغییر داده است.

سایر Managementها فقط آن را Consume می‌کنند.

---

# 3. دسته‌بندی داده‌های مشترک

داده‌های مشترک سیستم در دسته‌های زیر قرار می‌گیرند:

```text
Shared Data
│
├── Power / Vehicle State
│
├── Location
│
├── Communication State
│
├── Device / System State
│
├── Hardware State
│
├── Time
│
├── Configuration
│
└── Operation / Fault Status
```

---

# 4. Power / Vehicle State

## 4.1 ACC State

مالک:

**Hardware Input Management**

```c
typedef struct
{
    bool     on;
    bool     valid;
    uint32_t timestamp;
} ACCState;
```

دسترسی:

```text
Hardware Input
      ↓
   ACC State
      ↓
Power Management
      ↓
Power Mode
```

مصرف‌کنندگان احتمالی:

* Power Management
* Server Management
* GPS Management
* SMS Management
* Call Management
* Firmware Update Management

---

## 4.2 Power Mode

مالک:

**Power Management**

```c
typedef enum
{
    POWER_NORMAL,
    POWER_LOW_POWER
} PowerMode;
```

این متغیر از مهم‌ترین Shared Stateهای کل سیستم است.

```text
ACC State
    ↓
Power Management
    ↓
Power Mode
    ↓
All Managements
```

Managementها فقط Power Mode را می‌خوانند.

هیچ Management دیگری مجاز به تغییر آن نیست.

---

# 5. Vehicle Input States

این داده‌ها توسط Hardware Input Management تولید می‌شوند.

### Digital Inputs

```c
typedef struct
{
    bool     active;
    bool     valid;
    uint32_t timestamp;
} DigitalInputState;
```

نمونه‌ها:

```text
Door State
Shock State
ACC State
```

هر Input دارای `InputId` مستقل است.

```c
DigitalInputState
    HardwareInput_GetState(InputId id);
```

---

# 6. Battery State

مالک:

**Hardware Input Management**

داده اصلی:

```c
typedef struct
{
    uint32_t millivolt;
    bool     valid;
    uint32_t timestamp;
} BatteryVoltageState;
```

در صورت نیاز:

```c
typedef enum
{
    BATTERY_NORMAL,
    BATTERY_LOW
} BatteryState;
```

اما مقدار ولتاژ و وضعیت Low Battery باید از هم تفکیک شوند.

```text
ADC
 ↓
Battery Voltage
 ↓
Low Battery Detection
 ├── State
 └── Event
```

بنابراین `LOW_BATTERY_DETECTED` یک Event است، نه جایگزین Battery State.

---

# 7. Location

مالک:

**GPS Management**

موقعیت یکی از مهم‌ترین Shared Dataهای سیستم است.

```c
typedef struct
{
    int32_t  latitude;
    int32_t  longitude;

    int32_t  altitude;

    uint32_t timestamp;

    bool     valid;
} PositionState;
```

نوع داده دقیق Latitude/Longitude باید در طراحی Detail مشخص شود؛ برای Firmware ترجیحاً از Fixed-Point استفاده شود و از `float` در مسیر اصلی داده اجتناب شود.

---

## 7.1 GPS Fix State

این داده از Position جدا است.

```c
typedef enum
{
    GPS_NO_FIX,
    GPS_FIX_ACQUIRING,
    GPS_FIX_VALID
} GPSFixState;
```

بنابراین:

```text
GPS Management
│
├── Position
│
└── Fix State
```

ممکن است Position قبلی وجود داشته باشد ولی Fix فعلی معتبر نباشد.

---

# 8. Communication State

این بخش عمدتاً توسط **EC200U Manager** مالکیت می‌شود.

## 8.1 Module State

```c
typedef enum
{
    EC200U_OFF,
    EC200U_INITIALIZING,
    EC200U_READY,
    EC200U_DEGRADED,
    EC200U_ERROR
} EC200UState;
```

---

## 8.2 Network State

```c
typedef enum
{
    NETWORK_UNKNOWN,
    NETWORK_SEARCHING,
    NETWORK_REGISTERED,
    NETWORK_LOST
} NetworkState;
```

این State برای چند Management قابل استفاده است:

```text
Server
SMS
Call
GPS
```

اما مالک آن EC200U Manager است.

---

## 8.3 Data Connection State

برای ارتباط Server:

```c
typedef enum
{
    DATA_DISCONNECTED,
    DATA_CONNECTING,
    DATA_CONNECTED,
    DATA_ERROR
} DataConnectionState;
```

مالک:

**EC200U Manager**

Server Management فقط آن را Consume می‌کند.

---

## 8.4 SIM State

```c
typedef enum
{
    SIM_UNKNOWN,
    SIM_NOT_READY,
    SIM_READY,
    SIM_ERROR
} SIMState;
```

مالک:

**EC200U Manager**

---

# 9. System State

مالک:

**System Management**

داده‌هایی مانند:

```c
typedef enum
{
    SYSTEM_STARTUP,
    SYSTEM_RUNNING,
    SYSTEM_DEGRADED,
    SYSTEM_ERROR
} SystemState;
```

این State نباید تبدیل به یک Health Manager بزرگ شود.

System Management فقط وضعیت کلی سطح سیستم را در حدی که واقعاً نیاز است ارائه می‌کند.

---

# 10. Firmware State

این بخش عمدتاً متعلق به Bootloader / Firmware Update است.

نمونه:

```c
typedef enum
{
    FW_UNKNOWN,
    FW_VALID,
    FW_UPDATE_PENDING,
    FW_UPDATE_IN_PROGRESS,
    FW_UPDATE_FAILED
} FirmwareState;
```

اما باید توجه شود که:

> وضعیت Boot Image و وضعیت Firmware Update Operation یکی نیستند.

بنابراین بهتر است در Implementation این دو جدا نگه داشته شوند.

---

# 11. Operation Status

هر Management می‌تواند Operation جاری خود را داشته باشد.

این داده **Global مشترک نیست**.

مثلاً:

```text
SMS Management
    └── Current Operation

Firmware Update Management
    └── Current Operation

Call Management
    └── Current Operation
```

بنابراین نباید یک:

```text
GlobalOperationState
```

برای کل Firmware ایجاد شود.

---

# 12. Fault Status

Fault نیز Shared Data است ولی مالکیت باید مشخص باشد.

هر Management Faultهای خودش را دارد:

```text
GPS
 └── GPS Fault

Server
 └── Server Fault

EC200U
 └── Modem Fault

Storage
 └── Storage Fault
```

System Management می‌تواند **خلاصه وضعیت Fault سیستم** را ارائه کند:

```c
SystemFaultStatus
```

ولی مالکیت Fault اصلی با Component ایجادکننده آن باقی می‌ماند.

---

# 13. System Time

زمان یکی از معدود داده‌هایی است که مالک آن Management نیست.

مالک:

**Time Base**

داده‌های اصلی:

```text
System Tick
Monotonic Time
Calendar Time
```

هر Component در صورت نیاز زمان را از Time Base می‌گیرد.

```text
Time Base
   ↓
Timestamp
   ├── Event
   ├── GPS Position
   ├── Fault
   ├── Telemetry
   └── Operation
```

---

# 14. Device Configuration

Configuration می‌تواند توسط چند Management مصرف شود ولی نباید چند مالک داشته باشد.

ساختار:

```text
Storage
   ↓
Configuration Data
   ↓
Configuration Provider / Owner
   ↓
Managements
```

مالک **معنای Configuration** با Management مربوطه است.

مثلاً:

```text
Server Configuration → Server Management
GPS Configuration    → GPS Management
SMS Configuration    → SMS Management
Power Configuration  → Power Management
```

Storage فقط آن‌ها را نگهداری می‌کند.

---

# 15. داده‌هایی که نباید Shared شوند

برخی داده‌ها باید کاملاً داخلی بمانند.

مثلاً:

```text
EC200U AT Parser State
Diagnostic FSM State
GPS Acquisition FSM
Firmware Update Download FSM
SMS Retry Counter
Call Internal State
Storage Write State
```

این‌ها:

> **Implementation State**

هستند و نباید به Shared Data تبدیل شوند.

---

# 16. جدول مالکیت نهایی

| Shared Data           | مالک                        |
| --------------------- | --------------------------- |
| ACC State             | Hardware Input              |
| Door State            | Hardware Input              |
| Shock State           | Hardware Input              |
| Battery Voltage       | Hardware Input              |
| Low Battery State     | Hardware Input              |
| Power Mode            | Power                       |
| Position              | GPS                         |
| GPS Fix State         | GPS                         |
| EC200U State          | EC200U                      |
| SIM State             | EC200U                      |
| Network State         | EC200U                      |
| Data Connection State | EC200U                      |
| System State          | System                      |
| System Time           | Time Base                   |
| Firmware/Image State  | Bootloader/Firmware Update             |
| Configuration         | Management مربوطه + Storage |
| Fault Status          | Owner Management            |
| Operation Status      | Owner Management            |

---

# 17. قاعده دسترسی

قاعده کلی:

```text
                 OWNER
                   │
             Update / Write
                   │
                   ▼
              Shared State
                   │
                Read Only
          ┌────────┼────────┐
          ▼        ▼        ▼
       Consumer Consumer Consumer
```

هیچ Global Variable عمومی با دسترسی Write برای همه Managementها ایجاد نشود.

---

# 18. State و Event در داده‌های مشترک

داده مشترک State است.

وقوع تغییر یا Condition خاص Event ایجاد می‌کند.

مثال:

```text
ACC State
    │
    ├── OFF
    │
    └── ON
          │
          └── State Change
                  ↓
               ACC_ON Event
```

یا:

```text
Position State
      │
      └── Latest Position

GPS Fix State
      │
      ├── VALID
      └── NO_FIX
              │
              └── State Change
                    ↓
               GPS_FIX_LOST
```

بنابراین Event جایگزین State نیست.

---

# 19. اصل Timestamp

هر داده‌ای که ممکن است با زمان تفسیر شود، در صورت نیاز باید Timestamp داشته باشد.

مثلاً:

```text
ACC State
Position
Battery Voltage
Door State
Shock
Network State
Fault
```

این موضوع برای Telemetry و Evidence نیز اهمیت دارد.

باید بین:

```text
Measurement Timestamp
```

و:

```text
Transmission Timestamp
```

تفاوت وجود داشته باشد.

---

# 20. نتیجه معماری

Shared Dataهای اصلی سیستم در این سطح عبارت‌اند از:

```text
                    SHARED STATE
                         │
 ┌───────────────────────┼────────────────────────┐
 │                       │                        │
 ▼                       ▼                        ▼
Vehicle/Input          Power                 Location
 ACC                   Mode                  Position
 Door                                        GPS Fix
 Shock
 Battery
 │
 └──────────────┐
                │
                ▼
          Communication
          EC200U
          SIM
          Network
          Data
                │
                ▼
             System
             State
                │
                ▼
              Time
```

و اصل نهایی:

> **هر داده مشترک یک Owner دارد؛ Owner تنها نویسنده است و سایر Managementها Consumer هستند.**

> **State فعلی در Shared Data قرار می‌گیرد؛ وقوع تغییر یا Condition مهم از طریق Event منتقل می‌شود.**

> **Stateهای داخلی FSM، Counterها، Retry State و جزئیات اجرای Operation نباید Shared شوند.**
