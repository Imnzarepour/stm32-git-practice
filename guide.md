# پلتفرم خودروی متصل سایپا — Firmware TCU

این فایل مرجع اصلی معماری پروژه است. هر کسی که روی این پروژه کار می‌کند باید قبل از هر اقدام، این فایل و مسیر مرتبط با کار فعلی را بخواند.

## قانون طلایی

> **اگر پاسخ سؤالی در اسناد `docs/` نیست، حدس نزن. بپرس.**

این پروژه از یک سری Interview فنی طولانی با مدیر فنی سایپایدک ساخته شده و هر Contract نتیجه تصمیمات دقیق و بحث‌شده است. حدس زدن API، اضافه کردن Feature یا تغییر مرز مسئولیت بین Managementها بدون تأیید صریح، معماری را خراب می‌کند.

**قبل از شروع هر کار، فایل `docs/04-open-items/OPEN-ITEMS.md` را بخوان.** اگر کاری که می‌خواهی انجام بدهی به یک مورد 🔴 مرتبط است (به‌خصوص Command Safety Policy برای فرمان‌های حساس مثل Engine Cut-off)، متوقف شو و از تیم فنی/مدیر پروژه بپرس.

## نقشه اسناد

```
docs/
├── 00-project/                     ← شروع از اینجا
│   ├── rfp-saipa-connected-car.md          RFP اصلی کارفرما (سایپایدک)
│   ├── project-spec-master.md              خلاصه تجمیعی کل پروژه (بک‌اند + Firmware)
│   └── delivery-roadmap.md                 توالی فازهای استقرار از زیرساخت پایه تا تحویل - وضعیت واقعی هر مورد را از README.md/OPEN-ITEMS.md بخوان، نه از این فایل
│
├── 01-architecture-notes/          ← روند شکل‌گیری معماری (تاریخی، برای Context)
│   شامل مصاحبه‌های اولیه، تعریف State Machine، الگوی Provider، Event/Queue، Health/Recovery
│   10-traceability-and-communication-contracts.md   نقشه ردیابی کلی + ۴ مسیر ارتباطی (Data/Event/Action/Fault)
│   نکته: این پوشه Contract نهایی نیست؛ بعضی تصمیمات اینجا بعداً در پوشه ۰۲ و ۰۳ دقیق‌تر شدند.
│
├── 02-management-contracts/        ← Contract نهایی هر یک از ۱۰ Management
│   system-management.md            Reset/Watchdog/LED/Fault Queue متمرکز
│   power-management.md             تعیین NORMAL/LOW_POWER بر اساس ACC
│   hardware-input-management.md    مالک GPIO/ADC (ACC, Door, Shock, Battery)
│   hardware-input-io-contract-DRAFT-A.md / DRAFT-B.md   ⚠️ دو Draft ناسازگار، نگاه کن به OPEN-ITEMS
│   diagnostic-management.md        Read Parameter / Read DTC / Clear DTC روی CAN - پیاده‌سازی واقعی گرفت (Current+Pending، CAN منحصر به این Management، بدون Arbitration)
│   gps-management.md               Position/Fix State از طریق EC200U Manager
│   server-management.md            ارتباط منطقی با Server (نه Cellular مستقیم)
│   sms-management.md               دریافت/ارسال/Parse SMS
│   call-management.md              تماس صوتی از طریق EC200U
│   firmware-update-management.md              دانلود/اعتبارسنجی/نصب Firmware
│   actuator-management.md          اجرای Engine Cut-off / Door Lock/Unlock / Alarm
│   reset-request-contract.md       قرارداد مشترک درخواست Reset (همه Managementها از این استفاده می‌کنند)
│   state-operation-contract.md     الگوی مشترک State/Operation/Process که همه Managementها باید از آن پیروی کنند
│
├── 03-infrastructure-contracts/    ← زیرساخت‌های مشترک (نه Management، فاقد Business Logic)
│   time-base.md                    Monotonic Tick + Calendar Time
│   queue-infrastructure.md         Event Queue / Telemetry Queue (Drop-Oldest-Insert-Newest)
│   memory-storage-infrastructure.md   EEPROM/NOR abstraction, Dual-Bank
│   nor-flash-infrastructure.md     ⚠️ NOR خارجی W25Q128 (16MB) - مدل ترکیبی File(LittleFS)+Raw(PROGRAM Staging)؛ ظرفیت ۱۶MB بلاک‌کننده و باز (OPEN-ITEMS)
│   driver-layer.md                 مرز نازک با GPIO/ADC/CAN/UART/Timer/EEPROM/NOR
│   management-infrastructure-dependency.md   نقشه کامل وابستگی هر Management به هر Infrastructure
│   common-infrastructure-overview.md   فهرست کلی زیرساخت‌ها
│   shared-data-and-state.md        جدول مالکیت هر داده مشترک سیستم (چه کسی Owner است)
│   hardware-proxy.md               لایه Hardware Proxy (تصمیم بسته‌شده) - تنها مرز مجاز نام‌بردن GPIO/ADC/CAN/UART/Register؛ تنها چیزی که در تست هر Management باید Mock شود
│   target-integration.md           یکپارچه‌سازی firmware/ با پروژه‌های CubeIDE هر بورد (MCU_F4/MCU_F1) - نقطه ورود مشترک Firmware_Main()
│   ec200u-manager.md               ⚠️ پیش‌نویس Contract EC200U Manager (GPS/Server/SMS/Call به آن وابسته‌اند) — طبق OPEN-ITEMS هنوز Final نشده. **برون‌سپاری این Component لغو شد (تصمیم صریح کارفرما) - پیاده‌سازی واقعی AT Command/UART حالا داخل همین Repo است** (`firmware/src/infrastructure/ec200u_manager.c` + `firmware/include/hardware_proxy/hardware_proxy_uart.h`)، نه پیمانکار؛ این سند همچنان مبنای معماری همان پیاده‌سازی است، فقط دیگر SOW پیمانکار نیست
│   bootloader.md                   Contract Bootloader (Dual-Bank/BOOT-PROGRAM/CRC Fail-Safe/STORAGE_RECOVERY) — مکانیزم اصلی بسته شده، ۲ ریز-مورد باز در OPEN-ITEMS مانده (مقصد Boot Error، رابطه محل دانلود Firmware Update با Inactive Bank)
│   configuration-infrastructure.md   Runtime Config مشترک، نه Management (لیست ۱۰تایی دست‌نخورده می‌مونه)
│   file-transfer-infrastructure.md   انتقال عمومی فایل بین Server و Storage (Download/Upload/Resume/Chunk) - Infrastructure جدید، نه Management (لیست ۱۰تایی همچنان دست‌نخورده، Firmware Update Management هم کامل باقی می‌ماند - فقط مکانیزم انتقال از آن جدا شد). پیاده‌سازی واقعی دارد: `firmware/include|src/infrastructure/file_transfer.h|.c` (فقط مسیر Download، روی هر StorageObject، بدون هیچ نام‌گذاری مخصوص Firmware Update یا هر Use Case خاص دیگر)
│   log-infrastructure.md           صف مستقل Log + Append/Rotation - Infrastructure جدید (نه Management، مالک معنا همان Management تولیدکننده می‌ماند). پیاده‌سازی واقعی دارد: `firmware/include|src/infrastructure/log.h|.c` + صف هفتم `Queues_GetLogQueue()` - Rotation واقعی چندفایلی هنوز نه (ناحیه دایره‌ای موقت، نگاه کن OPEN-ITEMS #۴)
│
├── 04-open-items/
│   └── OPEN-ITEMS.md                ⚠️ همیشه قبل از شروع کار بخوان
│
└── 05-outsourcing/                 ← اسپک/SOW برون‌سپاری (تاریخی - برون‌سپاری EC200U لغو شد، نگاه کن پایین)
    ec200u-component-outsourcing-spec.md   ⚠️ منسوخ به‌عنوان SOW فعال - برون‌سپاری این بخش لغو شد؛ پیاده‌سازی واقعی EC200U حالا داخل همین Repo است (`firmware/src/infrastructure/ec200u_manager.c`). فایل عمداً دست‌نخورده و به‌عنوان مرجع تاریخی نگه داشته شده - محتوایش (فهرست Capabilityهای مورد انتظار) هنوز به‌عنوان چک‌لیست چه‌چیزهایی باید پیاده شود مفید است
    nor-storage-outsourcing-spec.md        خروجی مورد انتظار از پیمانکار NOR/Memory (Partition، LittleFS، Power-Loss Recovery) - عمداً دست‌نخورده مانده (احتمالاً دست پیمانکار است)
    nor-storage-outsourcing-spec-addendum.md   ⚠️ الحاقیه - هماهنگی نام‌گذاری (FOTA→Firmware Update) و مفاهیم جدید (File Transfer/Log Infrastructure، PROGRAM Staging) + یک ناهماهنگی عددی ظرفیت که قبل از مذاکره با پیمانکار باید حل شود
```

## اصول معماری غیرقابل‌مذاکره

این‌ها هیچ‌گاه نباید نقض شوند، حتی اگر پیاده‌سازی راحت‌تری به نظر برسد:

1. **Super Loop و Non-Blocking**: هیچ `Process()` نباید Block کند (نه `delay()`، نه Busy Wait، نه انتظار برای Hardware/Network). هر فراخوانی فقط یک گام کوچک پیش می‌رود و State را داخلی نگه می‌دارد.
2. **بدون فراخوانی مستقیم Management به Management.** ارتباط فقط از طریق پنج مسیر: Data (`Provider→Latest Value→Consumer`)، Event (`Detection→Event→Policy`)، Action (`Request→Queue→Owner→Execution`)، Fault (`Management→Fault→Local Recovery→[Reset Request]`)، Reset (`Reset Request→System Management→Reset`).
3. **بدون Result عمومی اجباری.** هر Management مسئولیت خودش (اجرا، Retry، Timeout، Fault) را تا انتها می‌بندد.
4. **Reset فقط متمرکز.** هیچ Management غیر از System Management مجاز به اجرای Reset نیست.
5. **مرز EC200U محکم است.** هیچ Management (غیر از EC200U Manager خودش) نباید AT Command، PDP، Socket یا جزئیات GNSS را بشناسد.
6. **Driver بدون Business Logic.** Driverها فقط سخت‌افزار خام را در اختیار می‌گذارند؛ Debounce، Qualification، Protocol، Policy همگی در Management هستند.
7. **دسترسی سخت‌افزار فقط از طریق Hardware Proxy.** هیچ Management و هیچ Infrastructure دیگری (به‌جز خودِ Hardware Proxy) مجاز نیست هدر `firmware/include/infrastructure/drivers/*.h` را include کند یا نام GPIO/ADC/CAN/UART/Register/Pin/Channel را در کد خودش ببرد؛ فقط `firmware/include/hardware_proxy/` صدا زده می‌شود (هم‌سطح `infrastructure/` و `managements/`، نه زیرشاخه Infrastructure؛ هر Domain سخت‌افزاری - Digital I/O، Analog I/O، بعداً UART/CAN/Interrupt - فایل مستقل خودش را دارد). جزئیات و دلیل (مشکل Multiple Definition در تست، و اینکه پیاده‌سازی واقعی HAL هر بورد با پسوند نام Target مثل `_stm32f4.c` در همان `firmware/hardware_proxy/` جدا می‌شود، نه با `#ifdef` و نه با پوشه‌بندی جدا در ریشه Repo): `docs/03-infrastructure-contracts/hardware-proxy.md`. **در تست هر Management، فقط Domainهایی از این لایه که همان Management واقعاً استفاده می‌کند Mock/Fake می‌شوند، نه Driverهای زیرین.**
8. **Shared Data یک Owner دارد.** فقط Owner می‌نویسد؛ بقیه فقط می‌خوانند. لیست کامل در `shared-data-and-state.md`.

## تیم و تقسیم مسئولیت

- ۲ برنامه‌نویس سینیور امبدد + ۱ میدل، داخلی
- ماژول سیم‌کارت (EC200U): **برون‌سپاری لغو شد (تصمیم صریح کارفرما، دیگر پیمانکاری در کار نیست) - پیاده‌سازی واقعی AT Command/UART حالا داخلی است**، نگاه کن `docs/03-infrastructure-contracts/ec200u-manager.md` و بخش کد اسکلت پایین
- یک برون‌سپار رزرو (احتمالاً Memory/LittleFS)
- Diagnostic Engine داخلی (پروتکل CAN/K-Line) مسئولیت تیم Diagnostic جداست؛ این پروژه فقط Contract سطح بالا با آن‌ها را تعیین می‌کند، نه پیاده‌سازی داخلی‌اش.

## کد اسکلت (`firmware/`)

یک اسکلت C واقعی و کامپایل‌شونده از تمام Contractها ساخته شده:

```
firmware/
├── CMakeLists.txt
├── include/
│   ├── firmware_main.h   نقطه ورود مشترک Firmware_Main() - نگاه کن docs/03-infrastructure-contracts/target-integration.md
│   ├── shared/           Event.h، ResetRequest.h (مشترک بین همه)، event_policy.h (Event→SMS/Call/Server/Log Routing)، command_safety_policy.h (تنها «در» مجاز برای «آیا اجرای این Command امن است؟» - امروز Diagnostic، GPS Position Read، Firmware Download، Diagnostic Actuator Test و Actuator Door GPIO (این دو تای آخر هر دو Door Lock/Unlock را اجرا می‌کنند - دو مکانیزم مستقل، Diagnostic/CAN و GPIO مستقیم، هر دو تنها Domainهای واقعاً Actuating که تاکنون صریحاً باز شده‌اند) عبور می‌کنند، Fail-Closed برای بقیه (Engine Cut-off و خودِ نصب/Boot Action فریمور همچنان بسته)، نگاه کن OPEN-ITEMS بند 🔴 ایمنی - هر پنج Domain فعلی فقط Placeholder Pass-Through هستند نه Policy واقعی)، command_router.h (هم‌الگوی event_policy، مسیر Command ورودی Server/SMS→Command Safety Policy→Diagnostic/Firmware Update/Actuator Management)
│   ├── hardware_proxy/   هم‌سطح infrastructure/ و managements/ (نه زیرشاخه Infrastructure) - یک هدر به‌ازای هر Domain سخت‌افزاری: hardware_proxy_digital_io.h، hardware_proxy_analog_io.h، hardware_proxy_storage.h، hardware_proxy_can.h (Send/Receive/Init روی CAN، تنها مصرف‌کننده‌اش Diagnostic Management است طبق بخش ۰ Contract آن)، ...
│   ├── infrastructure/   Contract هر زیرساخت (structs/enums دقیقاً طبق سند) + ec200u_manager.h
│   └── managements/      Contract هر ۱۰ Management (structs/enums دقیقاً طبق سند) + hardware_input_management_config.h (همه ثابت‌های تنظیم‌پذیر Hardware Input یک‌جا - Gain، Debounce، آستانه Low Battery، Interval Telemetry، ضریب LOW_POWER)
├── src/
│   ├── infrastructure/time_base.c    ← پیاده‌سازی واقعی و کامل (نه Stub)
│   ├── infrastructure/queue.c        ← پیاده‌سازی واقعی و کامل (نه Stub)
│   ├── infrastructure/storage.c       ← STORAGE_CONFIG پیاده‌سازی واقعی دارد (از طریق Hardware Proxy Storage domain)؛ بقیه StorageObjectها (EVENT/TELEMETRY/FIRMWARE_UPDATE/RECOVERY) هنوز Stub هستند، وابسته به تصمیم Backend خودشان (Firmware Update روی NOR جداست)
│   ├── infrastructure/configuration.c ← پیاده‌سازی واقعی و کامل (نه Stub) - نگاه کن docs/03-infrastructure-contracts/configuration-infrastructure.md
│   ├── drivers/*.c                    ← همچنان Stub (وابسته به سخت‌افزار نهایی)
│   ├── hardware_proxy/hardware_proxy_digital_io.c، hardware_proxy_analog_io.c، hardware_proxy_storage.c، hardware_proxy_can.c، hardware_proxy_uart.c، ...   ← تنها فایل‌های غیر-Driver مجاز به include کردن drivers/*.h؛ Host-Stub برای Build میزبان (hardware_proxy_can.c فعلاً فقط Pass-Through روی infrastructure/drivers/can.h Host-Stub است - هنوز نسخه _stm32f4 واقعی ندارد؛ hardware_proxy_uart.c هم همین‌طور Pass-Through است، اما نسخه‌اش **واقعی** شد - نگاه کن ردیف بعدی)
│   ├── hardware_proxy/hardware_proxy_uart_stm32f4.c   ← پیاده‌سازی واقعی HAL برای MCU_F4 (USART2، RX/TX Interrupt-Driven، Ring Buffer) - تنها مصرف‌کننده EC200U Manager است؛ USART2 Global Interrupt در .ioc اصلاً فعال نبود، پس این فایل خودش NVIC را در Init فعال می‌کند و USART2_IRQHandler/HAL_UART_*Callback را به‌صورت Strong Symbol تعریف می‌کند - بدون دست‌زدن به هیچ فایل تولیدی CubeMX (هم‌الگوی hardware_proxy_analog_io_stm32f4.c که hadc1 را از داخل خودش دوباره پیکربندی می‌کند)
│   ├── hardware_proxy/hardware_proxy_digital_io_stm32f4.c   ← پیاده‌سازی واقعی HAL برای MCU_F4 (ACC/Door/Shock/reserve_input)؛ با پسوند نام Target از Host-Stub جدا می‌شود، CMakeLists.txt آن را Exclude می‌کند - نگاه کن docs/03-infrastructure-contracts/hardware-proxy.md بخش ۷
│   ├── hardware_proxy/hardware_proxy_storage_stm32f4.c   ← پیاده‌سازی واقعی برای MCU_F4، روی ST EEPROM Emulation (حافظه Flash داخلی)؛ MCU_F1 هنوز پروژه CubeIDE ندارد (فقط pins name.txt)، پس نسخه _stm32f1 مثل حالت Digital I/O فعلاً باز مانده
│   ├── infrastructure/ec200u_manager.c   ← **پیاده‌سازی واقعی AT Command Driver** (دیگر Stub/برون‌سپار نیست - برون‌سپاری این بخش لغو شد): موتور AT غیرمسدودکننده (صف تک‌مالکیتی مشترک بین ۶ Capability + URC Dispatch) روی `hardware_proxy/hardware_proxy_uart.h`؛ Module Init/Cellular Status، GNSS، SMS، Voice Call، MQTT، HTTP همه با AT Command واقعی Quectel EC200U پیاده شده‌اند - Contract عمومی (`ec200u_manager.h`) دقیقاً دست‌نخورده ماند، هیچ Managementی بالادست تغییر نکرد. جزئیات/محدودیت‌های شناخته‌شده (هرگز روی سخت‌افزار واقعی تست نشده، محدودیت Quoted-String برای Payload باینری MQTT، و...) در `docs/04-open-items/OPEN-ITEMS.md` ثبت شده. تست‌ها (`test_ec200u_manager.c`) با Mock روی `hardware_proxy_uart.h` و یک EC200U جعلی Script-شده کار می‌کنند.
│   ├── managements/*.c               ← Stub با Init()/Process() خالی + یادداشت TODO (hardware_input_management.c، power_management.c، diagnostic_management.c، sms_management.c، call_management.c، server_management.c، actuator_management.c و firmware_update_management.c پیاده‌سازی واقعی دارند، نه Stub - firmware_update_management.c فقط محدوده Download+Store دارد، Validate/Install/Boot Action عمداً پیاده نشده؛ actuator_management.c هر ۴ ActuatorId (شامل Door Lock/Unlock، تازه به GPIO وصل شده) را واقعاً روی HWProxy_WriteDigitalLine می‌برد، نگاه کن OPEN-ITEMS بند 🔴 ایمنی)
│   ├── firmware_main.c               ← Super Loop، همه Process()ها را صدا می‌زند (شامل EC200U_Process و EventPolicy_Process)؛ تابع Firmware_Main() هرگز برنمی‌گردد، نه main()
│   └── host_main.c                   ← main() واقعی فقط برای Build میزبان/تست؛ هرگز در پروژه CubeIDE هیچ Target اضافه نشود
└── test/fakes/            fake_gpio.c، fake_time_base.c، fake_ec200u_manager.c (طبق اصل Fake/Stub اسناد - برای تست خود Driver/Proxy، نه برای تست یک Management؛ تست هر Management باید Domain(های) لازم از Hardware Proxy را Mock کند، نگاه کن hardware-proxy.md)
```

**زبان کامنت در کد C:** کامنت‌های داخل فایل‌های `.c`/`.h` به انگلیسی ساده نوشته می‌شوند (تصمیم صریح پروژه)؛ اسناد `docs/*.md` و مکالمات فنی همچنان فارسی می‌مانند. حجم زیاد کامنت مشکلی ندارد، مهم توضیح دلیل (Why) است نه فقط بازگویی کد.

## کد اسکلت Bootloader (`bootloader/`)

کاملاً جدا از `firmware/` (پروژه مستقل خودش، `project.yml`/Ceedling جدا، هرگز به `firmware/include/*` وابسته نمی‌شود - جزئیات در `bootloader/README.md`):

```
bootloader/
├── include/           8 هدر (4 تایپ پایه + boot_hal.h مرز سخت‌افزار + boot_image_header.h/boot_config.h Placeholder+تنظیمات + bootloader.h) - واقعی و تست‌شده
├── src/bootloader.c    منطق تصمیم‌گیری کامل (BOOT/PROGRAM، Retry محدود، Fail-Safe) - فقط از طریق boot_hal.h با سخت‌افزار صحبت می‌کند
├── test/unit/           test_smoke.c (5 تست) + test_bootloader.c (8 تست، CMock روی boot_hal.h) - جمعاً 13 تست PASS
├── mcu_f4_boot/, mcu_f1_boot/   پروژه‌های Cube مستقل Bootloader هر بورد - هنوز خالی (پیاده‌سازی واقعی boot_hal.h منتظر این‌هاست)
├── project.yml, .gitignore
└── (اجرای تست: از همین پوشه `ceedling test:all`)
```

نکته حیاتی هماهنگی: Offset بایتی `STORAGE_RECOVERY` (`bootloader/include/boot_control.h`/`boot_error.h`) با مصرف‌کننده سمت Application (`firmware/src/managements/system_management.c` → `check_boot_error()`) دستی هماهنگ نگه داشته می‌شود، نه از طریق هدر مشترک - چون این دو پروژه عمداً از هم مستقل‌اند. اگر یکی از دو طرف Layout را عوض کرد، طرف دیگر باید همزمان عوض شود (جزئیات در `bootloader/include/README.md` و OPEN-ITEMS #2).

`boot_hal.h`'s `NewImage_Read`/`NewImage_GetSize` (Placeholder موقت جای منبع واقعی Firmware جدید) اکنون با `docs/03-infrastructure-contracts/nor-flash-infrastructure.md` بخش ۴-۳ («PROGRAM Staging» روی NOR خارجی، Raw، فقط Bootloader می‌خواند) هم‌راستا هستند، اما آن سند هم صراحتاً باز می‌گذارد که آیا اصلاً این ناحیه واسط لازم است (بخش ۴-۲) و ظرفیت ۱۶MB کافی است یا نه (بخش ۴-۱، بلاک‌کننده) - پس این هنوز Placeholder می‌ماند، نه تصمیم نهایی.

## پروژه‌های CubeIDE هر بورد (`MCU_F4/`، بعداً `MCU_F1/`)

در ریشه Repo، هر بورد هدف یک پروژه کامل STM32CubeIDE (تولیدشده توسط CubeMX) دارد - مثلاً `MCU_F4/` برای STM32F407. این پروژه‌ها `Core/`, `Drivers/` (HAL) و `main.c` تولیدشده خودشان را دارند و **جدا از** `firmware/` نگه داشته می‌شوند.

از آنجا که هم `firmware/` منطق برنامه دارد و هم پروژه CubeIDE یک `main()` تولیدشده دارد، قرارداد یکپارچه‌سازی این‌طور بسته شده: `firmware/` دیگر `main()` ندارد، به‌جایش `Firmware_Main()` (در `firmware/include/firmware_main.h`) را Export می‌کند که هرگز برنمی‌گردد؛ `main()` واقعی هر پروژه CubeIDE (در ناحیه USER CODE، بدون دست‌زدن به کد تولیدی CubeMX) فقط همین تابع را صدا می‌زند. جزئیات کامل، از جمله مراحل دستی لازم داخل خود IDE (افزودن Include Path و Linked Source Folder)، در `docs/03-infrastructure-contracts/target-integration.md` آمده. **همین الگو باید عیناً برای `MCU_F1/` (نسخه بعدی همین پروژه) تکرار شود.**

چرا Time Base و Queue پیاده‌سازی واقعی دارند ولی بقیه Stub هستند: این دو Infrastructure کاملاً بدون Business Logic و در سندشان دقیقاً بسته شده‌اند؛ بقیه (Managementها و EC200U Manager) نیازمند تصمیمات محصولی یا برون‌سپاری هستند که باید توسط تیم مسئول هر بخش گرفته شود، و برخی صریحاً به موارد باز در OPEN-ITEMS.md گره خورده‌اند. Hardware Input Management و Power Management از این قاعده مستثنا شدند و پیاده‌سازی واقعی گرفتند، چون تصمیمات لازم (Contract هر دو + قانون ساده ACC→Mode) از قبل بسته بود؛ `Power_Process()` وضعیت ACC را از طریق مسیر Data (`GetDigitalInput`) می‌خواند، نه GPIO مستقیم - `Power Mode` هنوز مهم‌ترین Shared State سیستم است که بقیه Managementها بعداً باید برای رفتار خودشان در LOW_POWER بخوانند. `fake_ec200u_manager.c` یک Fake کامل‌تر است (State داخلی قابل کنترل با `FakeEC200U_*`)، چون GPS/Server/SMS/Call Management برای توسعه/تست موازی به آن نیاز دارند — بدون آن مجبورند AT Command Engine واقعی (`ec200u_manager.c`) را هم در تست هر Management دیگر شبیه‌سازی کنند؛ توجه: این Fake امروز در هیچ Build/Testی واقعاً Link نمی‌شود (فقط در کد نگه داشته شده، نه `project.yml`/`CMakeLists.txt`) - هر Managementی که به EC200U وابسته است (GPS/Server/SMS/Call) به‌جایش `Mockec200u_manager.h` (CMock) را در تست خودش استفاده می‌کند.

Configuration Infrastructure نیز پیاده‌سازی واقعی گرفت (Init/Load، Get، Update بدون Validation Callback - طبق تصمیم صریح پروژه؛ فهرست فیلدهای `DeviceConfig` فعلاً Provisional از مثال‌های خود سند است، نه فهرست نهایی). به همین مناسبت، تصمیم Backend فیزیکی `STORAGE_CONFIG` هم بسته شد: ST EEPROM Emulation روی Flash داخلی، مشترک بین MCU_F4 و MCU_F1، از طریق یک Domain جدید در Hardware Proxy (`hardware_proxy_storage.h`) - نگاه کن `docs/03-infrastructure-contracts/hardware-proxy.md`. بقیه StorageObjectها (از جمله `STORAGE_FIRMWARE_UPDATE`، برای دانلود/نصب Firmware، که طبق `memory-storage-infrastructure.md` باید روی NOR خارجی باشد) دست‌نخورده و همچنان Stub ماندند. مصرف‌کننده واقعی اول همین تغییر: لیست Hardcoded شماره‌های مجاز Call Management (`call_management_allowed_numbers.h`) حذف و به `DeviceConfig.access.allowed_phone_numbers` منتقل شد.

کل `firmware/` (شامل EC200U Manager، Configuration Infrastructure، File Transfer Infrastructure، Log Infrastructure و Diagnostic Management) با `gcc -std=c99 -Wall -Wextra` کامپایل و Link چک شده و بدون خطا/Warning است — یعنی نقطه شروع سالمی برای هر کسی است که کار روی آن را ادامه می‌دهد.

**File Transfer Infrastructure پیاده‌سازی واقعی گرفت** (فقط مسیر Download، Upload هنوز نه): `FileTransfer_StartDownload(StorageObject, expected_size)` + `FileTransfer_SupplyChunk()` + `FileTransfer_Process()` روی هر `StorageObject` کار می‌کند (امروز مصرف‌کننده منطقی `STORAGE_FIRMWARE_UPDATE` است، اما هیچ نام یا فرضی مخصوص یک Use Case خاص (مثل Firmware Update) در این فایل نیست - طبق تصریح `file-transfer-infrastructure.md` بخش ۰: «Firmware Update Management کاملاً دست‌نخورده باقی می‌ماند»، فقط مکانیزم انتقال عمومی شد). چون ماژول واقعی NOR دست پیمانکار است و هنوز نرسیده، `STORAGE_FIRMWARE_UPDATE` در `storage.c` همچنان Stub است (`Storage_Write` همیشه `STORAGE_ERROR`) - این فایل کاملاً روی Contract موجود `infrastructure/storage.h` نوشته شده (نه SPI/NOR مستقیم)، پس با آمدن Backend واقعی هیچ‌چیز اینجا عوض نمی‌شود. هر تابع عمومی یک `FileTransferResult`/`Status`/`Fault` مجزا (نه `bool`) برمی‌گرداند تا هم در تست، هم در Debugger زنده قابل ردیابی باشد - جزئیات در `firmware/include/infrastructure/file_transfer.h`.

**Diagnostic Management پیاده‌سازی واقعی گرفت** طبق Contract بازنویسی‌شده (`diagnostic-management.md`، CAN منحصر به این Management - بدون Arbitration، مدل Current+Pending نه یک Queue عمومی). `managements/diagnostic_management.c` یک ماشین‌حالت کاملاً Non-Blocking است: `Diagnostic_SubmitRequest()` یک `DiagnosticRequest` را یا فوری Accept می‌کند (Current خالی)، یا در تنها Slot Pending نگه می‌دارد، یا Busy برمی‌گرداند (هر دو Slot پر - یادآوری مهم Contract: یک نتیجه SUCCESS/FAILED مصرف‌نشده هم همچنان «اشغال» حساب می‌شود). تبادل واقعی با CAN از طریق Domain جدید Hardware Proxy انجام می‌شود (`hardware_proxy/hardware_proxy_can.h|.c` - Send/Receive/Init، فقط Pass-Through، بدون Business Logic). Read Parameter دوره‌ای داخلی (~۲ بار در دقیقه، `diagnostic_management_config.h`) فقط در `POWER_NORMAL` اجرا می‌شود (طبق همان قاعده‌ای که Hardware Input برای نرخ Telemetry خودش دارد) - یک Request صریح هرگز به‌خاطر LOW_POWER رد نمی‌شود. **مرز صریح با تیم Diagnostic جدا:** فریم واقعی CAN/UDS/K-Line (`build_request_frame()` در `diagnostic_management.c`) عمداً یک Placeholder ساده و به‌وضوح کامنت‌گذاری‌شده است - طبق CLAUDE.md («Diagnostic Engine داخلی مسئولیت تیم Diagnostic جداست») حدس‌زدن پروتکل واقعی اینجا مجاز نیست؛ فقط ماشین‌حالت Current+Pending/Send→Receive→Timeout (که واقعاً مسئولیت این پروژه است) واقعی و تست‌شده است. ۲۲ تست جدید در `test_diagnostic_management.c` (Mock روی `hardware_proxy_can.h` و `power_management.h`).

**Log Infrastructure پیاده‌سازی واقعی گرفت**: صف هفتم (`Queues_GetLogQueue()`) + `firmware/include|src/infrastructure/log.h|.c` (`Log_Init/Process`) روی `STORAGE_LOG` (Stub، مثل بقیه به‌جز CONFIG) کار می‌کند. دو مصرف‌کننده واقعی سیم‌کشی شدند: `event_policy.c` (مسیر `persist` - قبلاً اشتباه مستقیم `Storage_Write(STORAGE_EVENT, 0, ...)` می‌زد، حالا `LogRecord`→Log Queue) و `system_management.c`'s `check_boot_error()` (علاوه بر Fault Queue موجود، یک LogRecord هم می‌سازد). چون File API واقعی روی Storage هنوز نیست (همان دیواری که File Transfer/PROGRAM Staging هم به آن خورده)، `log.c` عمداً فقط یک ناحیه دایره‌ای تک‌بخشی (Wrap-Around ساده) پیاده کرده، نه Rotation چندفایلی واقعی سند - جزئیات در OPEN-ITEMS #۴.

## وضعیت پروژه

فاز طراحی و اسکلت اولیه بسته شده است. تکمیل، یکپارچه‌سازی سخت‌افزاری و تحویل نهایی از این پس توسط تیم مهندسی داخلی انجام می‌شود.

## قبل از شروع کار روی یک Management

قبل از نوشتن کد:
1. `docs/04-open-items/OPEN-ITEMS.md` را بخوان — آیا موردی مرتبط با این Management هنوز باز است؟
2. Contract همان Management در `docs/02-management-contracts/` را کامل بخوان.
3. `docs/02-management-contracts/state-operation-contract.md` را بخوان — الگوی مشترک State/Operation/Process که Contract این Management باید با آن سازگار باشد.
4. `docs/03-infrastructure-contracts/management-infrastructure-dependency.md` را چک کن — این Management به کدام Infrastructureها وابسته است؟
5. `docs/03-infrastructure-contracts/shared-data-and-state.md` را چک کن — کدام داده‌ها را این Management می‌خواند/می‌نویسد؟
6. اگر این Management به EC200U وابسته است (GPS/Server/SMS/Call)، `docs/03-infrastructure-contracts/ec200u-manager.md` را هم بخوان — توجه کن این هنوز پیش‌نویس است، نه Contract نهایی (OPEN-ITEMS #1).
7. اگر جایی از Contract مبهم بود یا با سند دیگری تناقض داشت (مثل تناقض‌های ثبت‌شده در OPEN-ITEMS)، **کد ننویس، بپرس.**
