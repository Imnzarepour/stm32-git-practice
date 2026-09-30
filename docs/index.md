# پلتفرم خودروی متصل سایپا — مستندات فنی Firmware TCU

این سایت مرجع فنی کامل معماری و کد اسکلت Firmware واحد **TCU** (Telematics Control Unit) پروژه خودروی متصل سایپا است. کارفرمای پروژه سایپایدک است.

برای یک معرفی کلی و غیرفنی‌تر پروژه (مناسب برای هر کسی که می‌خواهد سریع دربارهٔ پروژه صحبت کند)، صفحهٔ **ویکی مخزن** را ببینید. این سایت جای تفصیل فنی/Contractهاست.

!!! warning "قانون طلایی"
    اگر پاسخ سؤالی در این اسناد نیست، حدس نزن. بپرس. هر Contract نتیجهٔ یک سری Interview فنی طولانی و تصمیمات دقیق و بحث‌شده است؛ حدس زدن API یا تغییر مرز مسئولیت بین Managementها بدون تأیید صریح، معماری را خراب می‌کند.

    **قبل از شروع هر کار روی یک Management، [موارد باز معماری](04-open-items/OPEN-ITEMS.md) را بخوانید.**

## محدوده کارکردی TCU

* دریافت وضعیت خودرو (ACC، درب، ضربه، ولتاژ باتری) از طریق ورودی‌های سخت‌افزاری
* موقعیت‌یابی (GPS) و ارتباط با سرور بک‌اند از طریق ماژول سلولار EC200U
* دریافت و ارسال پیامک، برقراری تماس صوتی
* اجرای فرمان‌های از راه دور (قطع موتور، قفل/باز کردن درب، آژیر)
* به‌روزرسانی Firmware از راه دور
* تشخیص عیب روی شبکه CAN خودرو

## نقشهٔ مستندات

| بخش | محتوا |
| --- | --- |
| [۰۰ - معرفی پروژه](00-project/rfp-saipa-connected-car.md) | RFP اصلی کارفرما و نقشهٔ راه استقرار |
| [۰۱ - یادداشت‌های معماری](01-architecture-notes/00-project-overview-notes.md) | روند شکل‌گیری معماری (تاریخی، برای Context — **Contract نهایی نیست**) |
| [۰۲ - قرارداد Managementها](02-management-contracts/state-operation-contract.md) | Contract نهایی هر یک از ۱۰ Management مستقل |
| [۰۳ - قرارداد Infrastructureها](03-infrastructure-contracts/common-infrastructure-overview.md) | زیرساخت‌های مشترک، فاقد Business Logic |
| [۰۴ - موارد باز](04-open-items/OPEN-ITEMS.md) | تصمیمات معماری هنوز بسته‌نشده — همیشه قبل از شروع کار بخوانید |
| [۰۵ - برون‌سپاری](05-outsourcing/ec200u-component-outsourcing-spec.md) | اسناد تاریخی برون‌سپاری (بخشی از آن‌ها منسوخ شده) |

## اصول معماری غیرقابل‌مذاکره

Firmware حول ۱۰ Management مستقل سازمان‌دهی شده است. هیچ Management به‌صورت مستقیم Management دیگری را فراخوانی نمی‌کند؛ ارتباط فقط از پنج مسیر مشخص انجام می‌شود:

* **Data** — یک Management آخرین مقدار خود را منتشر می‌کند؛ سایرین فقط می‌خوانند.
* **Event** — تشخیص یک رویداد وارد Event Queue می‌شود؛ Policy تصمیم می‌گیرد چه کسی مطلع شود.
* **Action** — درخواست اجرای یک عملیات از طریق Queue منتقل می‌شود؛ فقط مالک عملیات آن را اجرا می‌کند.
* **Fault** — هر Management خطای خود را تا حد امکان به‌صورت محلی مدیریت می‌کند؛ درخواست Reset تنها در صورت نیاز صادر می‌شود.
* **Reset** — هر درخواست Reset از طریق System Management و به‌صورت متمرکز اجرا می‌شود.

مدل اجرا Super Loop غیر-Blocking است؛ هیچ `Process()` نباید منتظر سخت‌افزار یا شبکه بماند یا از تأخیر (Delay) استفاده کند. دسترسی به سخت‌افزار (GPIO، ADC، CAN، UART) صرفاً از طریق لایهٔ Hardware Proxy مجاز است.

جزئیات کامل و فهرست هر ۸ اصل غیرقابل‌مذاکره در ریشهٔ مخزن، فایل `guide.md`، آمده است.

## وضعیت فعلی پیاده‌سازی

**پیاده‌سازی‌شده و تحت تست:** Time Base، Queue Infrastructure، Configuration Infrastructure، Storage (تنها `STORAGE_CONFIG`)، File Transfer Infrastructure (تنها مسیر Download)، Log Infrastructure، Power Management، Hardware Input Management، System Management، Diagnostic Management.

**در دست تکمیل:** GPS، Server، SMS، Call، Firmware Update، Actuator، و EC200U Manager. Contract هر یک از این بخش‌ها نهایی شده است؛ پیاده‌سازی داخلی در جریان است.

**منطق تعیین‌شده، سخت‌افزار تأیید‌نشده:** Bootloader — منطق تصمیم‌گیری نوشته و با ۱۳ تست واحد تأیید شده؛ پروژه‌های CubeIDE هر بورد هنوز ساخته نشده‌اند.

جزئیات کامل‌تر و به‌روز در `README.md` ریشهٔ مخزن.

## اجرای تست‌ها

```
cd firmware
ceedling test:all

cd bootloader
ceedling test:all
```

## ساخت و اجرای محلی این سایت

```
pip install mkdocs-material
mkdocs serve   # پیش‌نمایش محلی روی http://127.0.0.1:8000
mkdocs build   # خروجی استاتیک در پوشهٔ site/
```
