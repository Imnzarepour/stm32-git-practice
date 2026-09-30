# پیوست فنی — تعریف مسئولیت، ورودی و خروجی GPS Management

## ۱. هدف

این پیوست، محدوده مسئولیت، ورودی‌ها، خروجی‌ها و مرزهای **GPS Management** را در معماری Firmware دستگاه TCU مشخص می‌کند.

هدف، ارائه موقعیت مکانی معتبر به سایر بخش‌های سیستم بدون وابستگی آن‌ها به جزئیات GNSS، پروتکل، Parse و ارتباط با EC200U است.

---

# ۲. جایگاه GPS Management

GPS Management مالک Capability موقعیت‌یابی دستگاه است.

مسیر کلی:

```text
GNSS
  ↓
EC200U Manager
  ↓
GPS Management
  ↓
Position State
  │
  ├────────→ Consumer
  │
  └────────→ Telemetry
```

GPS Management نباید مستقیماً با Driverهای GNSS یا جزئیات AT Command کار کند.

---

# ۳. مسئولیت‌ها

GPS Management مسئول موارد زیر است:

```text
Position Acquisition
Position Validation
Position State Management
GPS Fix Status
Position Update Policy
GPS Fault Detection
Position Data Preparation
```

جزئیات ارتباط با GNSS در اختیار **EC200U Manager** است.

---

# ۴. Position State

GPS Management آخرین Position معتبر را نگهداری می‌کند.

نمونه Contract:

```c
typedef struct
{
    int32_t  latitude;
    int32_t  longitude;
    uint32_t timestamp;
    bool     valid;
} GpsPosition;
```

مقیاس و واحد Latitude/Longitude باید در Contract رسمی مشخص شود.

استفاده از Fixed-Point در Contract بر Floating-Point ترجیح دارد.

---

# ۵. GPS Fix State

موقعیت فقط با داشتن مختصات قابل اعتماد مشخص نمی‌شود؛ وضعیت Fix نیز باید قابل تشخیص باشد.

نمونه:

```text
NO_FIX
FIX_AVAILABLE
```

در صورت نیاز می‌توان وضعیت‌های دقیق‌تر تعریف کرد.

اصل مهم:

> **Consumer باید بتواند تشخیص دهد Position فعلی معتبر است یا خیر.**

---

# ۶. دریافت Position

GPS Management اطلاعات GNSS را از EC200U Manager دریافت می‌کند.

```text
EC200U Manager
       ↓
GNSS Data
       ↓
GPS Management
       ↓
Validation
       ↓
Position State
```

GPS Management مسئول تشخیص معتبر بودن داده دریافتی از دید Capability موقعیت‌یابی است.

---

# ۷. عدم دسترسی مستقیم به GNSS

GPS Management نباید مستقیماً:

```text
AT Command
UART
GNSS Driver
EC200U Register
```

را کنترل کند.

مرز صحیح:

```text
GPS Management
      ↓
EC200U Manager Contract
      ↓
EC200U
      ↓
GNSS
```

---

# ۸. Latest Position

Consumer برای دریافت موقعیت جاری از Interface خواندنی استفاده می‌کند:

```c
GpsPosition GPS_GetPosition(void);
```

این Interface:

* Non-Blocking است.
* Read Only است.
* Acquisition جدید ایجاد نمی‌کند.
* منتظر Fix نمی‌ماند.
* مستقیماً با EC200U ارتباط برقرار نمی‌کند.

مدل صحیح:

```text
GNSS
 ↓
GPS Process()
 ↓
Position State
 ↓
GPS_GetPosition()
```

---

# ۹. Position Timestamp

هر Position معتبر باید Timestamp داشته باشد.

این Timestamp امکان تشخیص تازگی داده را فراهم می‌کند.

مثال:

```text
Position:
    Latitude
    Longitude
    Timestamp = T
    Valid = true
```

Consumer می‌تواند بر اساس Timestamp درباره Freshness داده تصمیم بگیرد.

GPS Management مسئول تولید Timestamp و نگهداری آن است.

---

# ۱۰. Position Event

تغییر وضعیت‌های مهم GPS می‌توانند Event ایجاد کنند.

نمونه:

```text
NO_FIX
   ↓
FIX_AVAILABLE
   ↓
GPS_FIX_ACQUIRED
```

یا:

```text
FIX_AVAILABLE
   ↓
NO_FIX
   ↓
GPS_FIX_LOST
```

این Eventها فقط در صورت تعریف شدن در Event Catalog سیستم تولید می‌شوند.

GPS Management مسئول Policy مربوط به Event نیست.

---

# ۱۱. Telemetry

Position می‌تواند به‌عنوان Telemetry در اختیار Server Management قرار گیرد.

```text
GPS Management
      ↓
Position State
      ↓
Telemetry
      ↓
Telemetry Queue
      ↓
Server Management
```

GPS Management مسئول ارسال مستقیم به Server نیست.

---

# ۱۲. Power Mode

GPS Management باید با Power Mode سازگار باشد.

در `NORMAL`:

```text
Normal Position Update
```

در `LOW_POWER`:

```text
Reduced Position Activity
```

کاهش فعالیت می‌تواند شامل کاهش نرخ Update یا کاهش استفاده از EC200U باشد.

اما تصمیم دقیق درباره نحوه کاهش مصرف داخل GPS Management انجام می‌شود.

Power Management فقط Mode را اعلام می‌کند.

---

# ۱۳. Fault

GPS Management خطاهای مربوط به Capability خود را تشخیص می‌دهد.

نمونه:

```text
GNSS Data Invalid
GNSS Unavailable
Fix Timeout
EC200U GNSS Failure
Unexpected GNSS State
```

GPS Management ابتدا Recovery محلی را انجام می‌دهد.

در صورت نیاز:

```text
GPS Management
      ↓
Reset Request
      ↓
System Management
```

GPS Management مستقیماً EC200U یا MCU را Reset نمی‌کند.

---

# ۱۴. Contract سطح معماری

```text
GPS Management

Responsibility:
    Position Acquisition
    Position Validation
    Position State
    Fix State
    Position Update Policy
    GPS Fault Detection
    Position Telemetry Preparation

Inputs:
    GNSS Data
    System Time
    Power Mode

Outputs:
    Current Position
    Fix State
    GPS Events
    GPS Fault
    Telemetry Data
    Reset Request (if required)

Dependencies:
    EC200U Manager
    Time Base
    Telemetry Queue

Constraints:
    Non-Blocking
    Super Loop Compatible
    Stateful
    No Direct GNSS Hardware Access
    No Direct Server Communication
    No Event Policy Execution
    No Direct Reset
```

---

# ۱۵. مرز با سایر اجزا

| جزء                       | رابطه با GPS Management     |
| ------------------------- | --------------------------- |
| EC200U Manager            | ارائه سرویس GNSS            |
| Power Management          | اعلام Power Mode            |
| Server Management         | ارسال Telemetry             |
| Event Policy              | تصمیم‌گیری درباره GPS Event |
| System Management         | اجرای Reset Request         |
| Hardware Input Management | مستقل                       |
| Diagnostic Management     | مستقل                       |
| SMS Management            | مستقل                       |
| Call Management           | مستقل                       |

---

# ۱۶. اصل نهایی

GPS Management مالک **Capability موقعیت‌یابی** است، نه مالک ارتباط Cellular یا Server.

```text
GNSS
 ↓
EC200U Manager
 ↓
GPS Management
 ↓
Position State
 ├── Consumer
 ├── Telemetry
 └── Event
```

اصل معماری:

> **GPS Management باید موقعیت معتبر و وضعیت Fix را به‌صورت Non-Blocking و مستقل از جزئیات GNSS در اختیار سیستم قرار دهد؛ ارتباط مستقیم با EC200U، شبکه و Server خارج از مسئولیت آن است.**
