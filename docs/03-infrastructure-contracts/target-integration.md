# پیوست فنی — یکپارچه‌سازی Firmware با پروژه‌های CubeIDE هر بورد

## ۱. هدف

این سند نحوه یکپارچه‌سازی کد قابل‌حمل `firmware/` (Managementها، Infrastructure، Hardware Proxy) را با پروژه‌های STM32CubeIDE هر بورد (`MCU_F4/` فعلاً موجود، `MCU_F1/` در آینده برای همین پروژه) مشخص می‌کند.

مسئله: هر پروژه CubeIDE توسط CubeMX یک `Core/Src/main.c` خودش تولید می‌کند (شامل `HAL_Init`، `SystemClock_Config`، `MX_*_Init`) که خودش یک `int main(void)` دارد. `firmware/` هم منطق برنامه (Super Loop) را دارد. **نباید دو `main()` در یک Binary وجود داشته باشد.**

---

## ۲. راه‌حل: یک نقطه ورود قابل‌فراخوانی مشترک

`firmware/` دیگر خودش `main()` ندارد. به‌جای آن:

```c
// firmware/include/firmware_main.h
void Firmware_Main(void);   // هرگز برنمی‌گردد - خودش Super Loop است
```

```c
// firmware/src/firmware_main.c
void Firmware_Main(void) {
    Time_Init();
    ...
    for (;;) {
        HardwareInput_Process();
        ...
    }
}
```

هر Target (پروژه CubeIDE هر بورد، یا Build میزبان برای تست) فقط باید این تابع را از `main()` واقعی خودش صدا بزند.

---

## ۳. دو نوع `main()` واقعی

### ۳-۱. Build میزبان (Host/PC) - برای توسعه و تست محلی

```c
// firmware/src/host_main.c
int main(void) {
    Firmware_Main();
    return 0;
}
```

این فایل فقط در `CMakeLists.txt` (Build میزبان) استفاده می‌شود. **هرگز نباید در هیچ پروژه CubeIDE اضافه شود** - چون آنجا `main()` واقعی را CubeMX می‌سازد.

### ۳-۲. پروژه CubeIDE هر بورد (MCU_F4 اکنون، MCU_F1 در آینده)

`Core/Src/main.c` تولیدشده توسط CubeMX **دست‌نخورده باقی می‌ماند** (HAL/Clock/Peripheral Init همچنان مسئولیت خودش است). فقط در ناحیه‌های `USER CODE` (که CubeMX هنگام Regenerate حفظ می‌کند) دو خط اضافه می‌شود:

```c
/* USER CODE BEGIN Includes */
#include "firmware_main.h"
/* USER CODE END Includes */
```

```c
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  Firmware_Main();   /* هرگز برنمی‌گردد */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
```

`while(1)` تولیدشده توسط CubeMX بعد از آن هرگز اجرا نمی‌شود (چون `Firmware_Main()` برنمی‌گردد) - عمداً حذف نشده تا CubeMX بتواند روی همین فایل بدون تداخل Regenerate کند.

این تغییر برای `MCU_F4/Core/Src/main.c` از قبل انجام شده است.

---

## ۴. مرحله‌ای که باید داخل خود IDE انجام شود (این تغییر آن را انجام نداده)

اضافه‌کردن `#include "firmware_main.h"` و فراخوانی آن کافی نیست مگر این‌که Compiler پروژه CubeIDE بداند فایل‌های `firmware/include` و `firmware/src` کجا هستند. چون این یک تغییر ساختاری روی فایل پروژه Eclipse/CDT (`.project`/`.cproject`) است که بدون خود IDE به‌سختی قابل تأیید است، این بخش **باید یک‌بار به‌صورت دستی داخل STM32CubeIDE برای هر Target انجام شود**:

1. **افزودن Include Path:**
   Project Properties → C/C++ General → Paths and Symbols → تب Includes → GNU C → Add →
   مسیر workspace-relative به `firmware/include` (مثلاً از طریق "Workspace..." یا "File system..." با مسیر نسبی `../firmware/include`).

2. **افزودن Source Folder (Linked Folder):**
   راست‌کلیک روی پروژه → New → Folder → Advanced → گزینه "Link to alternate location (Linked Folder)" → مسیر `firmware/src` را انتخاب کنید (پوشه‌ای بیرون از پروژه، هم‌سطح `Core/` و `Drivers/`).
   این کار باعث می‌شود همه `.c` های داخل `firmware/src` (شامل `firmware_main.c`، همه `managements/`، `infrastructure/` و `hardware_proxy/`) در Build این Target کامپایل شوند.

3. **حذف `host_main.c` از Build این Target:**
   روی فایل `host_main.c` (داخل همان Linked Folder) راست‌کلیک → Resource Configurations → Exclude from Build → همه Configurationها را تیک بزنید.
   دلیل: این پروژه از قبل `main()` واقعی خودش را در `Core/Src/main.c` دارد؛ وجود دو `main()` در یک Link باعث خطای Linker می‌شود.

4. **تکرار همین سه قدم برای `MCU_F1`** وقتی آن پروژه CubeIDE ساخته شد - دقیقاً همین الگو، بدون هیچ تفاوت مفهومی.

> این مراحل عمداً توسط این تغییر در `.project`/`.cproject` اعمال نشدند، چون این فایل‌ها ساختار XML بسیار حساس و مخصوص STM32CubeIDE دارند و بدون خود IDE قابل اعتبارسنجی/Build نیستند؛ ویرایش کورکورانه آن‌ها ریسک خراب‌کردن یک پروژه IDE از قبل کارکرده را دارد. انجام این ۳ قدم داخل خود IDE چند دقیقه بیشتر طول نمی‌کشد و با بازخورد بصری IDE قابل تأیید فوری است.

---

## ۵. نکته مهم: پیاده‌سازی واقعی HAL با پسوند نام Target مشخص می‌شود، نه با پوشه‌بندی جدا

`firmware/src/infrastructure/drivers/*.c` پیاده‌سازی Stub میزبان دارند (مقدار ثابت برمی‌گردانند)، نه HAL واقعی STM32. برای `firmware/src/hardware_proxy/` اما پیاده‌سازی واقعی از قبل نوشته شده - نگاه کن `hardware_proxy_digital_io_stm32f4.c`.

نکته مهم: نگرانی اولیه این بود که چون F4 و F1 به‌محض وصل‌شدن به HAL واقعی قطعاً کد متفاوتی نیاز دارند، این تفاوت نباید داخل `firmware/` مشترک حل شود (وگرنه دیر یا زود با `#ifdef STM32F4/STM32F1` پر می‌شود). اما دلیل اصلی که پیاده‌سازی واقعی نباید مستقیم جای Stub را بگیرد، **تست نیست** - تست از قبل با Mock کردن Contract حل شده (`hardware-proxy.md` بخش ۶) و مهم نیست پیاده‌سازی واقعی کجای Repo باشد. دلیل واقعی **Toolchain** است: همین `firmware/` هم با `gcc` عادی (این Build میزبان) و هم با `arm-none-eabi-gcc` (پروژه CubeIDE) کامپایل می‌شود؛ `HAL_GPIO_ReadPin` فقط با هدرهای HAL مخصوص ARM کامپایل می‌شود و روی Host اصلاً پیدا نمی‌شود.

راه‌حل پیاده‌شده: پیاده‌سازی واقعی همچنان **داخل** `firmware/src/hardware_proxy/` می‌ماند، فقط با پسوند `_stm32f4` (و بعداً `_stm32f1`) از فایل Host-Stub هم‌نام جدا می‌شود:

```text
firmware/src/hardware_proxy/hardware_proxy_digital_io.c          ← Host-Stub
firmware/src/hardware_proxy/hardware_proxy_digital_io_stm32f4.c  ← پیاده‌سازی واقعی با HAL_GPIO_*، فقط MCU_F4
```

`CMakeLists.txt` (Build میزبان) هر فایل با پسوند `_stm32f4.c`/`_stm32f1.c` را با `list(FILTER SOURCES EXCLUDE REGEX ...)` کنار می‌گذارد - همین الان انجام شده و تست شده (Build میزبان بدون این فایل‌ها Link می‌شود).

سمت پروژه CubeIDE (این بخش هنوز دستی و داخل خود IDE انجام می‌شود، طبق بخش ۴): وقتی `firmware/src` به‌صورت Linked Folder اضافه شد، باید فایل Host-Stub هم‌نام (`hardware_proxy_digital_io.c`، بدون پسوند) از Build آن Target Exclude شود - دقیقاً مثل کاری که با `host_main.c` کردیم - تا فقط نسخه `_stm32f4.c` کامپایل شود.

Pin/Port استفاده‌شده در `hardware_proxy_digital_io_stm32f4.c` (`ACC_Pin`/`ACC_GPIO_Port`، `DOOR_Pin`، `SHOCK_Pin`، `reserve_input_Pin`) مستقیماً از `MCU_F4/Core/Inc/main.h` (تولیدشده توسط CubeMX از `ConnectedCar.ioc` / `pins name.txt`) آمده، نه حدس زده شده.

---

## ۶. اصل نهایی

> **فقط یک `main()` واقعی در هر Binary وجود دارد: یا `host_main.c` (Build میزبان) یا `Core/Src/main.c` تولیدشده توسط CubeMX (هر Target). هر دو فقط `Firmware_Main()` را صدا می‌زنند؛ منطق برنامه یک‌بار در `firmware/` نوشته می‌شود و بین همه Targetها (F4 اکنون، F1 در آینده) مشترک است.**
