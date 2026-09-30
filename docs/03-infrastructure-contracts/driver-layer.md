# پیوست فنی — تعریف و قرارداد Driver Layer

## ۱. هدف

**Driver Layer** مرز استاندارد میان Firmware و سخت‌افزار MCU است.

هدف آن این است که Managementها و Infrastructureها بدون شناخت Registerها، Peripheralها و جزئیات سخت‌افزار، سرویس موردنیاز خود را دریافت کنند.

```text
Management
    ↓
Infrastructure / Contract
    ↓
Driver Layer
    ↓
MCU Peripheral / Hardware
```

---

# ۲. اصل معماری

Driver فقط **سخت‌افزار را کنترل می‌کند** و نباید منطق Product یا Business داشته باشد.

مثلاً:

```text
GPIO Driver
    → Read Pin
    → Write Pin
```

اما:

```text
Hardware Input Management
    → Debounce
    → Qualification
    → State
    → Event
```

یا:

```text
CAN Driver
    → Send Frame
    → Receive Frame
```

اما:

```text
Diagnostic Management
    → Diagnostic Protocol
    → Request/Response
    → Timeout
    → DTC
```

اصل:

> **Driver می‌داند سخت‌افزار چگونه کار می‌کند؛ Management می‌داند محصول چه کاری باید انجام دهد.**

---

# ۳. محدوده مسئولیت

Driver Layer مسئول موارد زیر است:

```text
Peripheral Initialization
Peripheral Access
Hardware Data Transfer
Hardware Status
Hardware Interrupt Registration
DMA Interface
Low-Level Hardware Error Reporting
```

اما مسئول موارد زیر نیست:

```text
Business Logic
Product Policy
Event Policy
Retry Policy
Diagnostic Protocol
Firmware Update Policy
Power Policy
Safety Decision
```

---

# ۴. Driverهای مورد نیاز

بر اساس سخت‌افزار و Managementهای فعلی، Driverهای احتمالی عبارت‌اند از:

```text
GPIO Driver
ADC Driver
CAN Driver
UART Driver
Timer Driver
EEPROM Driver
NOR Flash Driver
USB Driver
Watchdog Driver
PWM / Buzzer Driver
LED / Output Driver
```

وجود Driver جداگانه برای هر Peripheral باید بر اساس نیاز واقعی تعیین شود؛ هدف، ایجاد لایه‌بندی تمیز است نه افزایش مصنوعی تعداد فایل‌ها.

---

# ۵. GPIO Driver

GPIO Driver دسترسی خام به ورودی/خروجی دیجیتال را فراهم می‌کند.

نمونه:

```c
bool GPIO_Read(GPIO_Pin pin);

void GPIO_Write(
    GPIO_Pin pin,
    bool value
);
```

GPIO Driver نباید:

* Debounce کند
* Door State تشخیص دهد
* ACC_ON تولید کند
* Event ایجاد کند
* Policy اجرا کند

این موارد متعلق به Hardware Input Management یا Actuator Management هستند.

---

# ۶. ADC Driver

ADC Driver مسئول دریافت مقدار ADC است.

```c
uint16_t ADC_Read(ADC_Channel channel);
```

خروجی Driver می‌تواند مقدار Raw ADC باشد.

تبدیل:

```text
Raw ADC
   ↓
Engineering Value
   ↓
Battery Voltage
```

در Hardware Input Management انجام می‌شود.

بنابراین Driver نباید بداند:

```text
"این ADC مربوط به Battery Voltage است."
```

---

# ۷. CAN Driver

CAN Driver مسئول ارسال و دریافت Frame است.

نمونه:

```c
bool CAN_Send(const CAN_Frame* frame);

bool CAN_Receive(CAN_Frame* frame);
```

Driver نباید مفهوم:

```text
DTC
Diagnostic Request
Diagnostic Response
Vehicle Parameter
```

را بشناسد.

این مفاهیم متعلق به Diagnostic Management هستند.

---

# ۸. UART Driver

UART Driver انتقال Byte Stream را فراهم می‌کند.

```c
bool UART_Send(
    UART_Channel channel,
    const uint8_t* data,
    uint32_t length
);

uint32_t UART_Read(
    UART_Channel channel,
    uint8_t* buffer,
    uint32_t length
);
```

جزئیات:

```text
AT Command
SMS
GNSS
TCP
```

نباید وارد UART Driver شوند.

این موارد متعلق به EC200U Manager و لایه‌های بالاتر هستند.

---

# ۹. Timer Driver

Timer Driver منبع سخت‌افزاری Timer را فراهم می‌کند.

```text
Hardware Timer
      ↓
Timer Driver
      ↓
Time Base
```

Time Base مسئول مفهوم زمان Firmware است.

Timer Driver فقط سخت‌افزار Timer را ارائه می‌کند.

---

# ۱۰. EEPROM Driver

EEPROM Driver عملیات فیزیکی EEPROM را فراهم می‌کند:

```text
Read
Write
Status
```

اما:

```text
Configuration Version
CRC
Validity
Migration
```

در Storage Infrastructure یا صاحب داده مدیریت می‌شود.

---

# ۱۱. NOR Flash Driver

NOR Driver عملیات سخت‌افزاری NOR را فراهم می‌کند:

```text
Read
Program
Erase Sector
Read Status
```

اما:

```text
Firmware Package
Firmware Version
Image Validation
Bank Policy
```

متعلق به Firmware Update / Bootloader / Storage Architecture هستند.

---

# ۱۲. Watchdog Driver

Watchdog Driver فقط کنترل سخت‌افزار Watchdog را فراهم می‌کند.

نمونه:

```c
void Watchdog_Init(void);
void Watchdog_Refresh(void);
void Watchdog_Reset(void);
```

اما تصمیم اینکه **چه زمانی Watchdog باید Refresh شود** بخشی از System Architecture است.

System Management مالک Watchdog Policy است.

---

# ۱۳. LED / Output Driver

Driver خروجی فیزیکی را کنترل می‌کند.

```c
void LED_Set(
    LED_Id id,
    LED_State state
);
```

اما تصمیم:

```text
LED نشان‌دهنده Fault باشد
LED بعد از ACC OFF خاموش شود
LED وضعیت خاصی را نشان دهد
```

متعلق به System Management یا Policy مربوطه است.

---

# ۱۴. Driver و ISR

Driver می‌تواند ISR سخت‌افزاری را مدیریت کند، اما ISR باید بسیار کوچک باقی بماند.

مدل:

```text
Hardware
   ↓
ISR
   ↓
Register Data / Flag
   ↓
Return
   ↓
Super Loop
```

پردازش Protocol یا Business نباید داخل ISR انجام شود.

---

# ۱۵. DMA

در صورت استفاده از DMA:

```text
Peripheral
    ↓
DMA
    ↓
Buffer
    ↓
ISR / Completion Flag
    ↓
Super Loop
```

Driver مسئول مدیریت DMA است.

Management فقط نتیجه را از Contract بالاتر دریافت می‌کند.

---

# ۱۶. Non-Blocking

Driverهای مورد استفاده در مسیر Super Loop باید تا حد امکان Non-Blocking باشند.

مثلاً:

```c
CAN_Send(...)
```

نباید تا پایان ارسال فیزیکی CPU را متوقف کند، اگر Hardware امکان ارسال Asynchronous را فراهم می‌کند.

مدل مطلوب:

```text
Request
   ↓
Start Hardware Operation
   ↓
Return
   ↓
Hardware
   ↓
Completion
```

---

# ۱۷. عملیات Synchronous

Synchronous بودن همیشه ممنوع نیست.

اگر عملیات:

* بسیار سریع باشد
* زمان آن قابل پیش‌بینی باشد
* Super Loop را معنادار متوقف نکند

می‌تواند Synchronous باشد.

مثلاً:

```text
GPIO Read
ADC Register Read
Simple Status Read
```

اما عملیات طولانی مانند:

```text
NOR Erase
Large Flash Write
Communication Wait
```

نباید Blocking باشند.

---

# ۱۸. Hardware Abstraction

Driver باید Hardware Abstraction ایجاد کند.

غلط:

```c
GPIOA->IDR & (1 << 3)
```

در Management.

صحیح:

```c
GPIO_Read(INPUT_ACC);
```

این جداسازی باعث می‌شود تغییر MCU یا تغییر Pin Mapping کمترین اثر را بر لایه‌های بالاتر داشته باشد.

---

# ۱۹. Pin Mapping

Mapping فیزیکی Pin نباید در Management قرار گیرد.

مثلاً:

```text
ACC
 ↓
GPIO Driver
 ↓
Physical Pin PA3
```

Management فقط می‌داند:

```text
GPIO_Pin = ACC
```

یا از Mapping تعریف‌شده در Board Configuration استفاده می‌کند.

---

# ۲۰. Error Handling

Driver باید خطاهای Hardware را گزارش کند.

مثلاً:

```text
DRIVER_OK
DRIVER_BUSY
DRIVER_INVALID
DRIVER_ERROR
DRIVER_NOT_READY
```

اما Driver نباید درباره Recovery تصمیم بگیرد.

مدل:

```text
Driver Error
    ↓
Owner Infrastructure / Management
    ↓
Recovery Decision
```

---

# ۲۱. Reset

Driver مستقیماً MCU Reset نمی‌کند، مگر اینکه خودش **Watchdog Driver** باشد و چنین عملیات سخت‌افزاری در Contract آن تعریف شده باشد.

حتی در آن حالت، تصمیم استفاده از Reset باید در سطح بالاتر باقی بماند.

برای Resetهای Firmware:

```text
Management
   ↓
Reset Request
   ↓
System Management
```

---

# ۲۲. Power Mode

Driver نباید Policy مربوط به Power Mode را تعیین کند.

اما باید API لازم برای تغییر وضعیت سخت‌افزار را در اختیار لایه بالاتر قرار دهد.

مثلاً:

```text
EC200U Power Control
Clock Control
Peripheral Enable/Disable
ADC Mode
```

تصمیم:

```text
"در LOW_POWER چه چیزی کاهش پیدا کند؟"
```

متعلق به Management / Power Policy است.

---

# ۲۳. Fake / Stub

Driver باید قابل Fake شدن باشد.

مثلاً:

```text
Production
Hardware Input Management
        ↓
Real GPIO Driver
        ↓
MCU


Test
Hardware Input Management
        ↓
Fake GPIO Driver
        ↓
Simulated Pin
```

مثال Test:

```text
Fake GPIO:
ACC = OFF

→ Process()

Fake GPIO:
ACC = ON

→ Process()

Expected:
ACC_ON Event
```

این قابلیت برای Unit Test و توسعه بدون Hardware ضروری است.

---

# ۲۴. Dependency Injection

Management نباید به Implementation مشخص Driver وابسته باشد.

مدل:

```text
Hardware Input Management
          ↓
      GPIO Contract
       ↙        ↘
Real Driver   Fake Driver
```

در نتیجه Unit Test می‌تواند بدون MCU واقعی اجرا شود.

---

# ۲۵. Driver Contract

هر Driver باید حداقل موارد زیر را مشخص کند:

```text
Initialization
Input
Output
State
Error
Timing
ISR Safety
DMA Behavior
Power Behavior
Fake Interface
```

---

# ۲۶. نمونه Contract

برای GPIO:

```c
typedef enum
{
    GPIO_OK,
    GPIO_ERROR,
    GPIO_INVALID
} GPIO_Result;

GPIO_Result GPIO_Init(void);

GPIO_Result GPIO_Read(
    GPIO_Pin pin,
    bool* value
);

GPIO_Result GPIO_Write(
    GPIO_Pin pin,
    bool value
);
```

برای CAN:

```c
CAN_Result CAN_Init(void);

CAN_Result CAN_Send(
    const CAN_Frame* frame
);

CAN_Result CAN_Receive(
    CAN_Frame* frame
);
```

Contract دقیق هر Driver در زمان طراحی Driver مربوطه تکمیل می‌شود.

---

# ۲۷. Initialization

Driverها باید Initialization مشخص داشته باشند.

ترتیب کلی:

```text
Bootloader
   ↓
Application Start
   ↓
Driver Initialization
   ↓
Infrastructure Initialization
   ↓
Management Initialization
   ↓
Super Loop
```

Driver نباید Initialization سایر Managementها را انجام دهد.

---

# ۲۸. Driver Dependency

Driverها نباید به Management وابسته باشند.

غلط:

```text
GPIO Driver
   ↓
Power Management
```

صحیح:

```text
Power Management
   ↓
Hardware Input Management
   ↓
GPIO Driver
```

به این ترتیب Dependency یک‌طرفه باقی می‌ماند.

---

# ۲۹. مرز مسئولیت

| موضوع              | Driver | Infrastructure | Management |
| ------------------ | -----: | -------------: | ---------: |
| Register Access    |      ✓ |                |            |
| Peripheral Control |      ✓ |                |            |
| Raw Hardware Data  |      ✓ |                |            |
| Hardware Error     |      ✓ |                |            |
| Data Abstraction   |        |              ✓ |            |
| Storage Logic      |        |              ✓ |            |
| Time Logic         |        |              ✓ |            |
| Qualification      |        |                |          ✓ |
| Event Generation   |        |                |          ✓ |
| Business Policy    |        |                |          ✓ |
| Product Behavior   |        |                |          ✓ |
| Recovery Decision  |        |                |          ✓ |

---

# ۳۰. اصل نهایی

Driver Layer باید **نازک، قابل پیش‌بینی و فاقد منطق محصول** باشد.

```text
Management
     ↓
Infrastructure Contract
     ↓
Driver
     ↓
Hardware
```

و سه اصل اصلی:

> **Management نباید Register را بشناسد.**

> **Driver نباید Business Logic را بشناسد.**

> **تغییر Hardware نباید باعث بازنویسی منطق Management شود.**

این لایه همان مرزی است که امکان توسعه مستقل Firmware، استفاده از Fake/Stub و در نهایت تعویض یا اصلاح سخت‌افزار را با کمترین اثر روی لایه‌های بالاتر فراهم می‌کند.
