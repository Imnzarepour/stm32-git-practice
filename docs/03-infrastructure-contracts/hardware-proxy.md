# پیوست فنی — Hardware Proxy Layer

## ۱. هدف

این سند یک لایه جدید معماری را تعریف می‌کند: **Hardware Proxy**.

این لایه توسط تصمیم صریح کاربر (نه یک تصمیم Draft/باز) اضافه شده و از این پس **غیرقابل‌مذاکره** است، دقیقاً هم‌ردیف با اصول `CLAUDE.md`.

هدف:

> **فقط و فقط پیاده‌سازی Hardware Proxy مجاز است نام GPIO، ADC، CAN، UART، Register، Pin یا Channel را در کد خودش ببرد. هیچ Management و هیچ Infrastructure دیگری اجازه ندارد هدرهای `infrastructure/drivers/` را مستقیم include کند.**

---

## ۲. چرا این لایه اضافه شد

پیش از این، `driver-layer.md` مرز میان Management و سخت‌افزار بود، اما هر Management مستقیماً هدر Driver مربوط به خودش را include می‌کرد (مثلاً Hardware Input Management هدرهای `gpio.h` و `adc.h` را مستقیم می‌شناخت).

این باعث دو مشکل شد:

1. **مرز اسمی پراکنده بود** — دانستن اینکه "GPIO فقط کجا مجاز است" نیاز به بررسی همه Managementها داشت، نه یک نقطه واحد.
2. **مشکل عملی در تست:** وقتی یک Management هدر Driver را مستقیم include می‌کرد، تست آن Management برای شبیه‌سازی سخت‌افزار مجبور بود همان Driver را (Fake دستی یا Mock) کنترل کند. چون پیاده‌سازی واقعی همان Driver هم در پروژه وجود دارد و توسط Build/Test-Runner به‌طور خودکار Link می‌شود، هر بار که هم یک Fake دستی برای همان تابع نوشته می‌شد هم پیاده‌سازی واقعی، Linker با خطای **Multiple Definition** متوقف می‌شد.

راه‌حل: یک لایه واحد بین Management و Driver که:

* تنها نقطه‌ای است که نام سخت‌افزار خام (GPIO/ADC/...) را می‌شناسد.
* تنها چیزی است که در تست هر Management باید Mock شود.

---

## ۳. جایگاه در معماری لایه‌ای

```text
Management         ┐
Infrastructure      ├─ Hardware Proxy هم‌سطح این دو است، نه زیرمجموعه Infrastructure
    ↓                ┘
Hardware Proxy   ← تنها نقطه‌ای که GPIO/ADC/CAN/UART/Register را می‌شناسد
    ↓
Driver Layer     ← همچنان طبق driver-layer.md، بدون تغییر مفهومی
    ↓
MCU / Hardware
```

Driver Layer حذف یا بازنویسی نشده — `driver-layer.md` هنوز Contract معتبر Registerهای خام است. Hardware Proxy فقط یک لایه اضافه‌شده روی آن است که آن را از بقیه سیستم **کاملاً پنهان** می‌کند.

---

## ۴. محل فایل‌ها

```text
firmware/
├── include/hardware_proxy/            ← هم‌سطح include/infrastructure و include/managements
│   ├── hardware_proxy_digital_io.h
│   ├── hardware_proxy_analog_io.h
│   ├── hardware_proxy_storage.h
│   ├── hardware_proxy_can.h           (Domain چهارم، بخش ۵-ج پایین‌تر)
│   └── ...                            (uart/interrupt وقتی لازم شدند)
└── src/hardware_proxy/                ← هم‌سطح src/infrastructure و src/managements
    ├── hardware_proxy_digital_io.c
    ├── hardware_proxy_analog_io.c
    ├── hardware_proxy_storage.c
    ├── hardware_proxy_can.c
    └── ...
```

دو تصمیم مهم درباره محل، هر دو صریحاً از کاربر:

1. **هم‌سطح `infrastructure/` و `managements/`، نه زیرپوشه `infrastructure/`.** چون این لایه از نظر اهمیت معماری (بند ۷ اصول غیرقابل‌مذاکره در `CLAUDE.md`) هم‌وزن خود Infrastructure است، نه یک زیرساخت معمولی مثل Time Base/Queue.
2. **داخل `firmware/` می‌ماند، نه در ریشه Repo.** چون برخلاف یک کتابخانه مستقل قابل‌استفاده در چند پروژه، این لایه کاملاً به Managementهای همین `firmware/` گره خورده؛ بردنش بیرون از `firmware/` فقط هزینه Build/CubeIDE-Wiring (نگاه کن `target-integration.md`) اضافه می‌کند بدون فایده واقعی.

**یک فایل به‌ازای هر دامنه سخت‌افزاری، نه یک فایل بزرگ برای همه.** دقیقاً مثل خود `drivers/` که gpio.c/adc.c/can.c/uart.c جدا هستند. دلیل: اگر همه در یک فایل/Mock باشند، تست هر Management که فقط GPIO لازم دارد مجبور می‌شود Mock UART/CAN را هم ببیند، و اضافه‌شدن یک Domain جدید فردا باعث Conflict/Diff روی فایلی می‌شود که اصلاً به آن Domain ربطی نداشت. وقتی Managementهای دیگر (Actuator برای Output، EC200U Manager برای UART، هر Interrupt مثل KEY_ON) به سخت‌افزار دیگری نیاز پیدا کنند، یک فایل جدید هم‌پوشه (`hardware_proxy_uart.h/.c`، `hardware_proxy_interrupt.h/.c`) اضافه می‌شود - **نه** یک فایل غول‌پیکر مشترک. Domain CAN دیگر فرضی نیست: طبق همین قاعده، `hardware_proxy_can.h/.c` ساخته و برای Diagnostic Management پیاده و تست شده - جزئیات در بخش ۵-ج. Domain UART هم دیگر فرضی نیست: `hardware_proxy_uart.h/.c` (Host-Stub) + `hardware_proxy_uart_stm32f4.c` (پیاده‌سازی واقعی HAL - RX/TX Interrupt-Driven، Ring Buffer، خودش NVIC را در Init فعال می‌کند چون USART2 Global Interrupt در `.ioc` فعلی فعال نبود) ساخته شد - تنها مصرف‌کننده‌اش EC200U Manager است (تصمیم صریح کاربر: برون‌سپاری این Component لغو شد، پیاده‌سازی واقعی AT Command/UART حالا داخلی است).

---

## ۵. Contract فعلی (Digital I/O + Analog I/O)

```c
// hardware_proxy_digital_io.h
typedef int HWProxy_DigitalLine;

typedef enum {
    HWPROXY_OK,
    HWPROXY_ERROR,
    HWPROXY_INVALID
} HWProxy_Result;

HWProxy_Result HWProxy_DigitalIo_Init(void);
HWProxy_Result HWProxy_ReadDigitalLine(HWProxy_DigitalLine line, bool* value);
HWProxy_Result HWProxy_WriteDigitalLine(HWProxy_DigitalLine line, bool value);
```

```c
// hardware_proxy_analog_io.h
typedef int HWProxy_AnalogChannel;

uint16_t HWProxy_ReadAnalogChannel(HWProxy_AnalogChannel channel);
```

نکات:

* `HWProxy_DigitalLine`/`HWProxy_AnalogChannel` اسم‌های Proxy-level هستند، نه `GPIO_Pin`/`ADC_Channel`. Management حتی نوع Driver را هم نمی‌بیند.
* نگاشت واقعی `line`/`channel` به Pin/Channel فیزیکی داخل `hardware_proxy_*.c` انجام می‌شود (خودش هم فعلاً Placeholder است، منتظر Board Config نهایی - نگاه کن OPEN-ITEMS).
* خروجی Analog همچنان Raw است؛ تبدیل به مقدار مهندسی‌شده مسئولیت Management مصرف‌کننده است (بدون تغییر نسبت به قبل).
* هیچ Business Logic (Debounce/Qualification/Policy) اینجا نیست - این‌ها هنوز مسئولیت Management هستند، دقیقاً طبق `driver-layer.md`.
* هر Domain آینده (`_uart`، `_can`، `_interrupt`) Contract مستقل خودش را می‌گیرد؛ مثلاً Interrupt شکل متفاوتی از API لازم دارد (Register Callback یا Pending-Flag، نه صرفاً Read/Write) که باید هنگام نیاز واقعی طراحی شود، نه از پیش حدس زده شود.

---

## ۵-ب. Domain سوم: Storage (پشتوانه `STORAGE_CONFIG`)

تصمیم صریح کاربر: Device Configuration روی ST EEPROM Emulation (شبیه‌سازی EEPROM در Flash داخلی، همان میان‌افزار استاندارد ST برای هر دو خانواده F1/F4 - نقاط ورود یکسان `EE_Init`/`EE_ReadVariable`/`EE_WriteVariable`) ذخیره می‌شود، نه NOR خارجی. طبق همان قاعده این سند (بند ۱)، این یعنی یک Domain مستقل جدید، نه دسترسی مستقیم `infrastructure/storage.c` به Flash:

```c
// hardware_proxy_storage.h
typedef enum {
    HWPROXY_STORAGE_REGION_CONFIG
} HWProxy_StorageRegion;

typedef enum {
    HWPROXY_STORAGE_OK,
    HWPROXY_STORAGE_INVALID,
    HWPROXY_STORAGE_ERROR
} HWProxy_StorageResult;

HWProxy_StorageResult HWProxy_Storage_Init(void);
HWProxy_StorageResult HWProxy_Storage_Read(HWProxy_StorageRegion region, uint32_t offset, void* buffer, uint32_t length);
HWProxy_StorageResult HWProxy_Storage_Write(HWProxy_StorageRegion region, uint32_t offset, const void* data, uint32_t length);
HWProxy_StorageResult HWProxy_Storage_Erase(HWProxy_StorageRegion region);
```

نکات:

* `HWProxy_StorageRegion` فقط یک عضو دارد چون فقط `STORAGE_CONFIG` نیاز واقعی امروز است (بدون Feature فرضی، طبق `CLAUDE.md`)؛ `STORAGE_FIRMWARE_UPDATE` (NOR خارجی، `memory-storage-infrastructure.md` بخش ۹) و بقیه `StorageObject`ها عمداً از این Domain رد نمی‌شوند.
* `infrastructure/storage.c` تنها مصرف‌کننده این هدر است؛ Managementها هرگز مستقیم این Domain را نمی‌بینند - فقط Contract عمومی `infrastructure/storage.h` را.
* پیاده‌سازی واقعی همان قاعده پسوند نام Target را دنبال می‌کند: `hardware_proxy_storage.c` (Host-Stub، شبیه‌سازی با یک Buffer در RAM) و `hardware_proxy_storage_stm32f4.c` (واقعی، `EE_*`). میان‌افزار `eeprom.h/eeprom.c` خودِ ST است، داخل `firmware/` نیست - باید مثل خود `firmware/` با همان روش دستی `target-integration.md` (Include Path + Linked Folder) به پروژه CubeIDE اضافه شود.
* `hardware_proxy_storage_stm32f1.c` طبق همان الگوی `hardware_proxy_digital_io_stm32f1.c` فعلاً نوشته نشده، چون `MCU_F1/` هنوز پروژه CubeIDE واقعی ندارد (فقط `pins name.txt`) - وقتی آن پروژه ساخته شد، منطق تقریباً بدون تغییر قابل کپی است چون `EE_*` روی هر دو خانواده یکسان است.

---

## ۵-ج. Domain چهارم: CAN (پشتوانه Diagnostic Management)

طبق `docs/02-management-contracts/diagnostic-management.md` بخش ۰: CAN منحصراً مال Diagnostic Management است، بدون Arbitration با هیچ Management دیگری. طبق همان قاعده این سند (بند ۱)، یعنی یک Domain مستقل جدید، نه include مستقیم `infrastructure/drivers/can.h` از داخل `diagnostic_management.c`:

```c
// hardware_proxy_can.h
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} HWProxy_CanFrame;

typedef enum {
    HWPROXY_CAN_OK,
    HWPROXY_CAN_ERROR,  /* هم خطای واقعی، هم "هنوز داده‌ای برای Receive نرسیده" */
    HWPROXY_CAN_BUSY
} HWProxy_CanResult;

HWProxy_CanResult HWProxy_CAN_Init(void);
HWProxy_CanResult HWProxy_CAN_Send(const HWProxy_CanFrame* frame);
HWProxy_CanResult HWProxy_CAN_Receive(HWProxy_CanFrame* frame);
```

نکات:

* `HWProxy_CanFrame` فقط شکل خام یک Frame استاندارد (ID/DLC/۸ بایت Data) است - هیچ معنای UDS/Protocol/DTC اینجا نیست؛ همان‌طور که بند ۵ این سند برای Digital/Analog I/O می‌گوید، تفسیر محتوا مسئولیت لایه بالاتر است (اینجا: تیم Diagnostic، نه حتی خود Diagnostic Management که فقط یک Placeholder ساده برای فریم‌بندی دارد - نگاه کن `diagnostic_management.c`).
* `HWProxy_CAN_Receive` غیر-Blocking Poll است: اگر داده‌ای نرسیده باشد `HWPROXY_CAN_ERROR` برمی‌گرداند، نه انتظار. تشخیص «خطای واقعی باس» در برابر «هنوز چیزی نرسیده» فعلاً یکسان است - اگر بعداً لازم شد این دو از هم جدا شوند، Domain باید یک مقدار Enum جدید بگیرد.
* `diagnostic_management.c` تنها مصرف‌کننده این هدر است (طبق «CAN منحصر به Diagnostic»)؛ هیچ Management دیگری مجاز به Include این هدر نیست.
* پیاده‌سازی واقعی همان قاعده پسوند نام Target را دنبال می‌کند: `hardware_proxy_can.c` امروز فقط Pass-Through روی `infrastructure/drivers/can.h` (Host-Stub) است؛ نسخه `hardware_proxy_can_stm32f4.c` (HAL CAN/bxCAN واقعی) هنوز نوشته نشده - منتظر تصمیمات سخت‌افزاری باقی‌مانده (Bitrate، Filter، شکل دقیق فریم UDS) که مسئولیت تیم Diagnostic است، نه این پروژه.

---

## ۵-د. Domain پنجم: LED/Buzzer (پشتوانه نمایشگرهای وضعیت System Management)

طبق `system-management.md` بخش ۳-۴/۵-۳: «System Management مسئول نمایش وضعیت است، مالک منطق داخلی GPS/شبکه/SIM/Power نیست» - این Domain همان لایه دسترسی خام به LEDهای POWER/GPS/GSM/SIM و Buzzer را در اختیار System Management می‌گذارد، بدون اینکه GPIO واقعی را به آن نشان دهد. برخلاف بقیه Domainها، این یکی مدت‌ها قبل از این بخش مستند در کد وجود داشت (`hardware_proxy_led.h/.c`) بدون اینکه در این سند ثبت شود - این بخش فقط همان Contract موجود را مستند می‌کند، تصمیم جدیدی نیست:

```c
// hardware_proxy_led.h
typedef enum { HWPROXY_LED_GSM, HWPROXY_LED_GPS, HWPROXY_LED_SIM, HWPROXY_LED_POWER } HWProxy_LedId;
typedef enum { HWPROXY_LED_OFF, HWPROXY_LED_ON, HWPROXY_LED_BLINK_SLOW, HWPROXY_LED_BLINK_FAST } HWProxy_LedState;

void HWProxy_SetLed(HWProxy_LedId id, HWProxy_LedState state);
void HWProxy_Led_Process(void); /* Non-Blocking - هر Super Loop Lap صدا زده می‌شود، Blink واقعی را پیش می‌برد */
void HWProxy_SetBuzzer(bool on);
```

نکات:

* `HWProxy_SetLed()` فقط State مطلوب را ثبت می‌کند (و برای OFF/ON بلافاصله GPIO را می‌نویسد)؛ یک نوشتن تکی GPIO نمی‌تواند LED را واقعاً چشمک‌زن کند. `HWProxy_Led_Process()` تابعی است که این Domain را از بقیه (Digital/Analog/Storage/CAN) متمایز می‌کند: چون Blink یک مفهوم زمان‌محور است نه یک State خام سخت‌افزاری، این Domain (برخلاف بقیه) یک تابع Process دارد که باید هر Lap صدا زده شود - دقیقاً همان الگوی Time-Base-محور که در سراسر این Firmware برای رفتار دوره‌ای استفاده می‌شود (نه Timer/PWM سخت‌افزاری، چون هیچ Timer Channel‌ای برای هیچ LED در CubeMX پیکربندی نشده).
* System Management (`system_management.c`) تنها مصرف‌کننده این Domain است.
* پیاده‌سازی واقعی همان قاعده پسوند نام Target را دنبال می‌کند: `hardware_proxy_led.c` (Host-Stub، Pass-Through روی `infrastructure/drivers/led.h` - آن Driver همیشه Stub می‌ماند، هرگز روی سخت‌افزار واقعی کامپایل نمی‌شود) و `hardware_proxy_led_stm32f4.c` (واقعی - برخلاف Digital I/O، مستقیم `HAL_GPIO_*` صدا می‌زند، نه از طریق Driver Layer، چون آن لایه فقط برای Build میزبان است). Pin/Portها مستقیماً از `MCU_F4/Core/Inc/main.h` (بخش `led`/`output` خودِ `pins name.txt`: `LED_GSM`/`LED_GPS`/`LED_SIM`/`LED_POWER`/`BUZZER`) می‌آیند.
* نرخ Blink (`LED_BLINK_SLOW_HALF_PERIOD_MS`/`LED_BLINK_FAST_HALF_PERIOD_MS`) یک Placeholder محلی داخل همان فایل `_stm32f4.c` است، نه بخشی از این Contract - مثل Debounce/Gain در `hardware_input_management_config.h`.
* `HWProxy_SetBuzzer()` هیچ مفهوم Blink ندارد (فقط ON/OFF)؛ هیچ Trigger واقعی هنوز آن را صدا نمی‌زند - `system_management.c` صراحتاً این را TODO باز نگه داشته (نیازمند تصمیم محصولی، نه معماری).

---

## ۶. قانون تست

> **برای تست هر Management، فقط و فقط Domainهای Hardware Proxy که همان Management واقعاً استفاده می‌کند باید Mock/Fake شوند - نه همه Domainها، و هرگز خود Driver.**

با CMock (چون پروژه از قبل `:use_mocks: TRUE` دارد):

```c
#include "Mockhardware_proxy_digital_io.h"
#include "Mockhardware_proxy_analog_io.h"
...
HWProxy_ReadDigitalLine_StubWithCallback(my_stub);
```

هرگز در تست یک Management هدر `infrastructure/drivers/*.h` را include نکنید و هرگز `test/fakes/fake_gpio.c` یا مشابه آن را برای تست یک Management وایر نکنید — چون پیاده‌سازی واقعی همان Driver هم در `:source` هست و باعث Multiple Definition می‌شود. نمونه کامل: `firmware/test/unit/test_hardware_input_management.c`.

(فایل‌های `test/fakes/fake_gpio.c` و مشابه آن‌ها هنوز در پروژه باقی می‌مانند، اما برای تست سطح Driver/Proxy خودش هستند، نه برای تست یک Management بالادست.)

---

## ۷. پیاده‌سازی واقعی سخت‌افزار per-Target (نکته مهم برای MCU_F4/MCU_F1)

`firmware/src/hardware_proxy/hardware_proxy_digital_io.c` (و مشابه آن) فقط **Host-Stub** است (مقدار ثابت برمی‌گرداند، برای Build/تست PC). وقتی زمان وصل‌شدن به HAL واقعی STM32 برسد، پیاده‌سازی F4 و F1 قطعاً فرق می‌کنند (HAL/Register متفاوت بین خانواده‌های STM32).

**تصمیم اولیه (اشتباه، اصلاح شد):** ابتدا تصور می‌شد پیاده‌سازی واقعی باید کاملاً بیرون از `firmware/` (مثلاً در `MCU_F4/HW/`) برود تا این تفاوت به کد مشترک سرایت نکند. اما دلیل واقعی نگرانی تست نبود - تست از قبل با Mock کردن همین Contract حل شده (بخش ۶) و هیچ فرقی نمی‌کند پیاده‌سازی واقعی کجای Repo باشد، چون هیچ‌وقت وارد Binary تست نمی‌شود. دلیل واقعی **Toolchain** است: `firmware/` هم با `gcc` عادی (Host Build، `CMakeLists.txt`) و هم با `arm-none-eabi-gcc` (پروژه CubeIDE) کامپایل می‌شود؛ `HAL_GPIO_ReadPin` و مشابه آن فقط با هدرهای HAL/CMSIS مخصوص ARM کامپایل می‌شوند و روی Host اصلاً پیدا/کامپایل نمی‌شوند.

**تصمیم نهایی:** پیاده‌سازی واقعی همچنان داخل `firmware/src/hardware_proxy/` می‌ماند، فقط با یک پسوند نام‌گذاری مشخص که Build میزبان آن را Exclude می‌کند:

```text
firmware/include/hardware_proxy/*.h                    ← Contract مشترک، پرتابل، هرگز per-Target عوض نمی‌شود

firmware/src/hardware_proxy/hardware_proxy_digital_io.c          ← Host-Stub (برای gcc/Ceedling)
firmware/src/hardware_proxy/hardware_proxy_digital_io_stm32f4.c  ← پیاده‌سازی واقعی با HAL_GPIO_* (فقط MCU_F4)
firmware/src/hardware_proxy/hardware_proxy_digital_io_stm32f1.c  ← (در آینده) پیاده‌سازی واقعی F1، مستقل از F4
```

قانون: هر فایلی با پسوند `_stm32f4.c` یا `_stm32f1.c`:

* در `CMakeLists.txt` (Build میزبان) صریحاً با `list(FILTER SOURCES EXCLUDE REGEX ...)` کنار گذاشته می‌شود.
* در پروژه CubeIDE همان بورد کامپایل می‌شود، و هم‌زمان فایل Host-Stub هم‌نام (بدون پسوند) از Build همان پروژه Exclude می‌شود (دقیقاً مثل کاری که با `host_main.c` در `target-integration.md` انجام شد).
* Pin/Port استفاده‌شده مستقیماً از `MCU_F4/Core/Inc/main.h` (تولیدشده توسط CubeMX از `ConnectedCar.ioc`) می‌آید، نه حدس زده می‌شود.

نمونه پیاده‌شده: `firmware/src/hardware_proxy/hardware_proxy_digital_io_stm32f4.c` (Digital I/O برای ACC/Door/Shock/reserve_input روی MCU_F4)، `firmware/src/hardware_proxy/hardware_proxy_analog_io_stm32f4.c` (ADC باتری)، `firmware/src/hardware_proxy/hardware_proxy_storage_stm32f4.c` (EEPROM Emulation)، و `firmware/src/hardware_proxy/hardware_proxy_led_stm32f4.c` (LED/Buzzer - بخش ۵-د بالا).

جزئیات کامل Wiring (Linked Folder، Include Path، Exclude) در `docs/03-infrastructure-contracts/target-integration.md`.

---

## ۸. اصل نهایی

> **Management فقط Hardware Proxy را می‌شناسد. Hardware Proxy تنها چیزی است که GPIO/ADC/CAN/UART/Register را می‌شناسد. Driver Layer زیر آن بدون تغییر باقی می‌ماند. پیاده‌سازی واقعی سخت‌افزار هر بورد داخل همان `firmware/hardware_proxy/` با پسوند نام Target مشخص می‌شود؛ تفکیک با Toolchain/Build انتخاب فایل انجام می‌شود، نه با پخش‌کردن `#ifdef` در کد مشترک و نه با پوشه‌بندی جدا در ریشه Repo.**

این قانون هم‌ردیف بند ۷ اصول معماری غیرقابل‌مذاکره در `CLAUDE.md` است و هیچ توسعه‌دهنده‌ای مجاز به نقض آن نیست.
