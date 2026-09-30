# پلتفرم خودروی متصل سایپا — Firmware TCU

این مخزن شامل معماری و کد اسکلت Firmware واحد TCU (Telematics Control Unit) پروژه خودروی متصل سایپا است. کارفرمای پروژه سایپایدک است.

## محدوده کارکردی TCU

* دریافت وضعیت خودرو (ACC، درب، ضربه، ولتاژ باتری) از طریق ورودی‌های سخت‌افزاری
* موقعیت‌یابی (GPS) و ارتباط با سرور بک‌اند از طریق ماژول سلولار EC200U
* دریافت و ارسال پیامک، برقراری تماس صوتی
* اجرای فرمان‌های از راه دور (قطع موتور، قفل/باز کردن درب، آژیر)
* به‌روزرسانی Firmware از راه دور
* تشخیص عیب روی شبکه CAN خودرو

## ساختار مخزن

| موضوع | مسیر |
| --- | --- |
| معماری، اصول الزامی، نقشه کامل اسناد | [`CLAUDE.md`](./CLAUDE.md) |
| RFP کارفرما و خلاصه تجمیعی پروژه | [`docs/00-project/`](./docs/00-project/) |
| Contract هر یک از ۱۰ Management (System، Power، Hardware Input، Diagnostic، GPS، Server، SMS، Call، Firmware Update، Actuator) | [`docs/02-management-contracts/`](./docs/02-management-contracts/) |
| زیرساخت‌های مشترک (Time، Queue، Storage، Hardware Proxy، EC200U Manager، Bootloader) | [`docs/03-infrastructure-contracts/`](./docs/03-infrastructure-contracts/) |
| موارد تصمیم‌گیری‌نشده | [`docs/04-open-items/OPEN-ITEMS.md`](./docs/04-open-items/OPEN-ITEMS.md) |
| کد Firmware، مستقل از سخت‌افزار، کامپایل‌شونده روی Host | [`firmware/`](./firmware/) |
| کد Bootloader، پروژه‌ای مستقل از `firmware/` | [`bootloader/`](./bootloader/) |
| پروژه‌های STM32CubeIDE هر بورد هدف | `MCU_F4/`، و برای Bootloader به‌صورت جداگانه `bootloader/mcu_f4_boot/` |

## اصول معماری

Firmware حول ۱۰ Management مستقل سازمان‌دهی شده است. هیچ Management به‌صورت مستقیم Management دیگری را فراخوانی نمی‌کند؛ ارتباط از پنج مسیر مشخص انجام می‌شود:

* **Data** — یک Management آخرین مقدار خود را منتشر می‌کند؛ سایرین فقط می‌خوانند.
* **Event** — تشخیص یک رویداد وارد Event Queue می‌شود؛ Policy تصمیم می‌گیرد چه کسی مطلع شود.
* **Action** — درخواست اجرای یک عملیات از طریق Queue منتقل می‌شود؛ فقط مالک عملیات آن را اجرا می‌کند.
* **Fault** — هر Management خطای خود را تا حد امکان به‌صورت محلی مدیریت می‌کند؛ درخواست Reset تنها در صورت نیاز صادر می‌شود.
* **Reset** — هر درخواست Reset از طریق System Management و به‌صورت متمرکز اجرا می‌شود.

مدل اجرا Super Loop غیر-Blocking است؛ هیچ `Process()` نباید منتظر سخت‌افزار یا شبکه بماند یا از تأخیر (Delay) استفاده کند.

دسترسی به سخت‌افزار (GPIO، ADC، CAN، UART) صرفاً از طریق لایه Hardware Proxy مجاز است. جزئیات کامل این اصول در [`CLAUDE.md`](./CLAUDE.md) آمده است.

## وضعیت فعلی پیاده‌سازی

**پیاده‌سازی‌شده و تحت تست:** Time Base، Queue Infrastructure، Configuration Infrastructure، Storage (تنها `STORAGE_CONFIG`)، File Transfer Infrastructure (تنها مسیر Download)، Log Infrastructure، Power Management، Hardware Input Management، System Management (Reset متمرکز، Watchdog، LED، Fault Queue، مسیر Boot Error به Fault و Log)، Diagnostic Management (مدل Current+Pending، دسترسی انحصاری به CAN، خواندن دوره‌ای پارامتر با رفتار مشخص در LOW_POWER).

**در دست تکمیل:** GPS، Server، SMS، Call، Firmware Update، Actuator، و EC200U Manager. Contract هر یک از این بخش‌ها نهایی شده است؛ پیاده‌سازی داخلی در جریان است.

**منطق تعیین‌شده، سخت‌افزار تأیید‌نشده:** Bootloader (مسیر [`bootloader/`](./bootloader/))، پروژه‌ای مستقل از `firmware/`. منطق تصمیم‌گیری (BOOT/PROGRAM، Retry، Fail-Safe) نوشته و با ۱۳ تست واحد (با Mock در مرز سخت‌افزار) تأیید شده است. پروژه‌های CubeIDE مربوط به هر بورد هنوز ساخته نشده‌اند. دو نقطه از کد به‌صورت صریح Placeholder علامت‌گذاری شده‌اند (فرمت Image و منبع دریافت نسخه جدید) تا تصمیمات سخت‌افزاری/معماری مرتبط در [`docs/04-open-items/OPEN-ITEMS.md`](./docs/04-open-items/OPEN-ITEMS.md) نهایی شوند.

کل کد در مسیر `firmware/` با `gcc -std=c99 -Wall -Wextra` بدون خطا یا هشدار کامپایل و Link می‌شود.

## اجرای تست‌ها

پروژه‌های `firmware/` و `bootloader/` مجموعه تست‌های واحد مستقل با [Ceedling](https://www.throwtheswitch.org/ceedling) (Unity + CMock) دارند:

```
cd firmware
ceedling test:all

cd bootloader
ceedling test:all
```

## اصل راهنمای توسعه

هر تصمیم معماری یا مرز مسئولیت که در اسناد `docs/` مشخص نشده، نباید بدون تأیید صریح فرض گرفته شود. Contractهای موجود نتیجه بررسی فنی مرحله‌به‌مرحله هستند؛ تغییر یک مرز مسئولیت بدون هماهنگی می‌تواند سازگاری بخش‌های دیگر را مختل کند.
