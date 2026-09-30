# پیوست فنی — معماری وابستگی و ارتباط Management و Infrastructure

## 1. هدف

این پیوست ساختار نهایی وابستگی میان **Managementها** و **Infrastructureها** را مشخص می‌کند تا:

* هر Management بتواند مستقل توسعه داده شود.
* وابستگی مستقیم Managementها به یکدیگر حذف شود.
* جایگزینی زیرساخت‌های ناقص با Fake/Stub امکان‌پذیر باشد.
* تغییر سخت‌افزار یا Driver باعث تغییر منطق Management نشود.
* معماری برای پیاده‌سازی موازی چند تیم قابل اتکا باشد.

---

## 2. ساختار لایه‌ای

```text
┌──────────────────────────────────────────────────────┐
│                  MANAGEMENT LAYER                    │
│                                                      │
│ System   Power   Hardware Input   Diagnostic         │
│ GPS      Server  SMS              Call               │
│ Firmware Update  Actuator                            │
└──────────────────────────┬───────────────────────────┘
                           │
                           │ Contracts
                           ▼
┌──────────────────────────────────────────────────────┐
│                INFRASTRUCTURE LAYER                  │
│                                                      │
│ Time Base │ Queue │ Storage │ Driver Layer           │
└──────────────────────────┬───────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────┐
│                 HARDWARE / MCU                       │
│ GPIO │ ADC │ CAN │ UART │ Timer │ EEPROM │ NOR ...   │
└──────────────────────────────────────────────────────┘
```

**Bootloader** خارج از مسیر اجرای عادی Application است.

**EC200U Manager** نیز یک مرز زیرساختی/سیستمی ویژه برای امکانات Cellular/GNSS/SMS/Call است و در انتهای طراحی تکمیل می‌شود.

---

## 3. اصل Dependency

وابستگی باید فقط از بالا به پایین باشد:

```text
Management
    ↓
Contract / Infrastructure
    ↓
Driver
    ↓
Hardware
```

وابستگی معکوس ممنوع است.

به‌طور خاص:

```text
Management A ─X→ Management B
Management   ─X→ GPIO Register
Management   ─X→ ADC Register
Management   ─X→ UART Register
Management   ─X→ NOR Address
Driver       ─X→ Management
Storage      ─X→ Management
Queue        ─X→ Management
```

---

## 4. ارتباط Managementها

Managementها مستقیماً API یکدیگر را فراخوانی نمی‌کنند.

ارتباط از مسیرهای تعریف‌شده انجام می‌شود:

### Data

```text
Provider
   ↓
Latest Value
   ↓
Consumer
```

مثال:

```text
Hardware Input Management
        ↓
      ACC State
        ↓
Power Management
```

---

### Event

```text
Input / Condition
       ↓
Detection
       ↓
Event
       ↓
Policy / Consumer
```

مثال:

```text
Hardware Input
      ↓
ACC_ON Event
      ↓
Power Management
```

---

### Action

```text
Policy / Consumer
       ↓
Request
       ↓
Queue
       ↓
Owner Management
       ↓
Execution
```

مثال:

```text
Server Management
      ↓
Actuator Request
      ↓
Actuator Management
      ↓
Output Driver
```

---

### Fault

```text
Management
    ↓
Fault
    ↓
Local Recovery
    ↓
اگر لازم بود
    ↓
Reset Request
    ↓
System Management
```

---

## 5. Dependency Matrix

| Management     | Time |           Queue |  Storage |        Driver | EC200U |
| -------------- | ---: | --------------: | -------: | ------------: | -----: |
| System         |    ✓ |               ✓ | Optional |  Watchdog/LED |      — |
| Power          |    ✓ |               — |        — | از طریق Input |      — |
| Hardware Input |    ✓ |           Event |        — |             ✓ |      — |
| Diagnostic     |    ✓ |   Request/Event | Optional |           CAN |      — |
| GPS            |    ✓ | Event/Telemetry | Optional |             — |      ✓ |
| Server         |    ✓ |               ✓ | Optional |             — |      ✓ |
| SMS            |    ✓ |               ✓ | Optional |             — |      ✓ |
| Call           |    ✓ |               ✓ | Optional |             — |      ✓ |
| Firmware Update |    ✓ |        Optional |        ✓ |             — |      — |
| Actuator       |    ✓ |         Request |        — |        Output |      — |

`Optional` به این معنی است که وابستگی فقط در صورتی ایجاد می‌شود که Requirement آن Capability نیاز داشته باشد.

**یادداشت:** ستون Storage برای Firmware Update (`✓`) بعد از اضافه‌شدن File Transfer Infrastructure دیگر یک وابستگی مستقیم Read/Write نیست - Firmware Update Management برای دریافت فایل از طریق File Transfer Infrastructure عبور می‌کند (بالا را ببینید)؛ برای نوشتن Raw در PROGRAM Staging همچنان مستقیماً Storage را صدا می‌زند. جدول اینجا برای سادگی بازنویسی نشده - جزئیات دقیق در `file-transfer-infrastructure.md`.

---

## 6. مالکیت Hardware

هیچ Management مستقیماً مالک سخت‌افزار عمومی نیست.

مثال:

```text
ACC
 │
 └── GPIO Driver
        ↓
 Hardware Input Management
        ↓
     ACC State
        ↓
 Power Management
```

و:

```text
CAN
 │
 └── CAN Driver
        ↓
 Diagnostic Management
```

و:

```text
NOR
 │
 └── NOR Driver
        ↓
 Storage Infrastructure
        ↓
 Firmware Update Management
```

**به‌روزرسانی (بعد از `docs/03-infrastructure-contracts/file-transfer-infrastructure.md`):** مسیر دریافت فایل از Server دقیق‌تر شده - Firmware Update Management دیگر مستقیماً مکانیزم Download/Chunk/Resume را پیاده نمی‌کند:

```text
Server
   │
   └── File Transfer Infrastructure   (Download/Upload/Resume/Chunk - معنای فایل را نمی‌داند)
          ↓
   Memory / Storage Infrastructure  (File API روی LittleFS/NOR)
          ↓
   Firmware Update Management   (Validate/Version/Install Decision - همان مالکیت قبلی، فقط مکانیزم انتقال جدا شد)
```

دیاگرام قدیمی بالا («NOR → Storage → Firmware Update Management») هنوز به‌طور کلی درست است؛ فقط الان یک لایه میانی (File Transfer) بین Server و Storage اضافه شده که Firmware Update Management را از جزئیات Chunk/Resume/Session بی‌خبر می‌کند.

---

## 7. Super Loop

Super Loop فقط Orchestrator اجرای Managementها و Infrastructureهای لازم است.

نمونه ساختار:

```c
while (1)
{
    Time_Process();

    HardwareInput_Process();

    Power_Process();

    Diagnostic_Process();

    GPS_Process();

    Server_Process();

    SMS_Process();

    Call_Process();

    FirmwareUpdate_Process();

    Actuator_Process();

    System_Process();
}
```

ترتیب فوق الزام قطعی نیست؛ اما هر `Process()` باید:

* Non-Blocking باشد.
* کار محدود و کوچک انجام دهد.
* State خود را حفظ کند.
* در صورت نبود کار، سریع برگردد.
* در صورت Power Mode، سیاست فعالیت خودش را اعمال کند.

Infrastructureهایی مانند Queue و Storage الزاماً همه `Process()` مستقل ندارند؛ فقط موارد Stateful/Asynchronous مانند Storage یا Driver خاص در صورت نیاز Process خواهند داشت.

---

## 8. Rule مهم برای Fake / Stub

هر Dependency باید Contract مشخص داشته باشد.

در صورت آماده نبودن Implementation واقعی:

```text
Management
    ↓
Contract
    ↓
Fake / Stub
```

و بعداً بدون تغییر Management:

```text
Management
    ↓
Contract
    ↓
Real Infrastructure
    ↓
Hardware
```

بنابراین مثلاً Server Management می‌تواند بدون EC200U واقعی توسعه و Unit Test شود:

```text
Server Management
       ↓
EC200U Contract
       ↓
Fake EC200U
```

---

## 9. مرزهای ممنوع

موارد زیر در معماری ممنوع هستند:

### 9.1 دسترسی مستقیم Management به Hardware

```c
GPIOA->IDR
ADC1->DR
CAN1->...
```

در Management ممنوع است.

---

### 9.2 دسترسی مستقیم به Storage Physical Address

```c
0x08080000
0x60000000
EEPROM_ADDRESS_X
```

در Management ممنوع است.

---

### 9.3 قرار دادن Business Logic در Driver

Driver نباید بداند:

```text
ACC_ON یعنی Normal Mode
Low Battery یعنی SMS
Shock یعنی Call
```

این‌ها متعلق به Management/Policy هستند.

---

### 9.4 قرار دادن Product Logic در Infrastructure

مثلاً Storage نباید بداند:

```text
این رکورد GPS است
این DTC است
این پیام SMS است
```

Storage فقط مکانیزم ذخیره‌سازی را ارائه می‌کند.

---

### 9.5 ایجاد Manager برای هر مفهوم کوچک

هر قابلیت الزاماً Management مستقل نیست.

مثلاً:

```text
Telemetry
Event Detection
Policy
Latest Value
```

در حال حاضر Capability/Mechanism هستند، نه Management مستقل.

---

## 10. نتیجه معماری

ساختار نهایی فعلی:

```text
                    APPLICATION
┌─────────────────────────────────────────────┐
│                 MANAGEMENT                  │
│                                             │
│ System                                      │
│ Power                                       │
│ Hardware Input                              │
│ Diagnostic                                  │
│ GPS                                         │
│ Server                                      │
│ SMS                                         │
│ Call                                        │
│ Firmware Update                             │
│ Actuator                                    │
└──────────────────────┬──────────────────────┘
                       │
                 Contracts
                       │
┌──────────────────────▼──────────────────────┐
│              INFRASTRUCTURE                 │
│                                             │
│ Time Base                                   │
│ Queue                                       │
│ Memory / Storage                            │
│ Driver Layer                                │
└──────────────────────┬──────────────────────┘
                       │
┌──────────────────────▼──────────────────────┐
│                  HARDWARE                   │
└─────────────────────────────────────────────┘


             خارج از Application Loop
                       │
             ┌─────────┴─────────┐
             │                   │
         Bootloader         EC200U Manager
```

## 11. اصل نهایی

> **Management مالک Capability است.**
> **Infrastructure ارائه‌دهنده سرویس مشترک است.**
> **Driver مالک دسترسی سخت‌افزار است.**
> **هیچ لایه‌ای نباید مسئولیت لایه دیگر را بر عهده بگیرد.**

این ساختار، مبنای تعریف Contractهای قابل تحویل به تیم‌های مستقل و پیاده‌سازی Fake/Stub برای وابستگی‌های آماده‌نشده خواهد بود.
