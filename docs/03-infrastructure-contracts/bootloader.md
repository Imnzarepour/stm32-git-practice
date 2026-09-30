# پیوست فنی — Bootloader و مدیریت Firmware

## 0. یادداشت جایگاه

Bootloader بخشی از Application Firmware نیست؛ طبق `management-infrastructure-dependency.md` («خارج از Application Loop: Bootloader، EC200U Manager») همیشه به‌صورت مستقل و پیش از شروع Super Loop اجرا می‌شود. این پیوست در کنار سایر Infrastructureها نگهداری می‌شود، نه در کنار Managementها.

**اتصال به زیرساخت موجود:** این پیوست اولین محتوای واقعی `StorageObject.STORAGE_RECOVERY` (تعریف‌شده در `memory-storage-infrastructure.md`) را مشخص می‌کند: Active Bank، Boot Action، و Boot Error Record.

---

## 1. هدف

* Firmware معتبر را شناسایی و اجرا کند.
* امکان Programming بانک غیرفعال را فراهم کند.
* قبل از فعال‌سازی Firmware جدید، صحت آن را بررسی کند.
* در صورت شکست Update، Firmware قبلی دستگاه قابل اجرا باقی بماند.
* خطاهای Boot و Programming را به‌صورت پایدار ثبت کند.

---

## 2. معماری Dual Bank

```text
Flash
├── Bank 1  (ممکن است Active یا Inactive باشد)
└── Bank 2  (ممکن است Active یا Inactive باشد)
```

> **Programming همیشه روی بانک غیرفعال انجام می‌شود؛ بانک فعال تا زمان موفقیت کامل Update تغییر نمی‌کند.**

---

## 3. اطلاعات کنترلی (در `STORAGE_RECOVERY`)

```text
STORAGE_RECOVERY
├── Active Bank     { BANK_1, BANK_2 }
└── Boot Action     { BOOT, PROGRAM }
```

---

## 4. حالت BOOT

```text
Boot → Read Active Bank → Calculate CRC → Compare
```

- **CRC معتبر:** `Jump to Application`
- **CRC نامعتبر:** `Stay in Bootloader` (Firmware نامعتبر هرگز اجرا نمی‌شود)

---

## 5. حالت PROGRAM

بانک مقصد همیشه **بانک غیرفعال** است (مثلاً Active=Bank1 → Target=Bank2).

---

## 6. دریافت Firmware

Bootloader باید Firmware را از محل تعیین‌شده برای Update پیدا کند. نبودن/ناقص‌بودن/خراب‌بودن فایل = شکست Programming → مکانیزم Retry (تعداد Retry پارامتر پیاده‌سازی است، در این Contract ثابت نمی‌شود).

**🔴 یادداشت باز:** رابطه محل ذخیره‌سازی دانلودشده توسط Firmware Update Management (`firmware-update-management.md` → Storage → External NOR) با «بانک غیرفعال» مشخص نیست — آیا Firmware Update مستقیماً داخل Inactive Bank دانلود می‌کند، یا در یک ناحیه Staging جدا و Bootloader خودش آن را داخل Bank کپی/Decrypt/Program می‌کند؟ این تصمیم مستقیماً روی برنامه‌ریزی ظرفیت NOR اثر دارد و باید قبل از طراحی تفصیلی Storage Layout مشخص شود.

**به‌روزرسانی (پس از `docs/03-infrastructure-contracts/nor-flash-infrastructure.md`):** آن سند برای همین ناحیه یک نام مشخص می‌گذارد — **PROGRAM Staging** روی External NOR، Raw (بدون LittleFS)، فقط Bootloader آن را می‌خواند (بخش ۴-۳ همان سند). این هنوز **بسته‌شدن این یادداشت باز نیست** — خود آن سند هم صراحتاً باز می‌گذارد که آیا اصلاً به این ناحیه واسط نیاز است یا Firmware Update باید مستقیماً به یک "Programming Interface" تحویل دهد (بخش ۴-۲)، و ظرفیت ۱۶MB با نقشه فعلی احتمالاً کافی نیست (بخش ۴-۱، بلاک‌کننده). فقط اسم/شکل این ناحیه روشن‌تر شده، نه تصمیم نهایی.

---

## 7. Decryption و Programming

```text
Firmware File → Decrypt → Program Inactive Bank
```

جزئیات الگوریتم Decryption در این سند تعریف نمی‌شود.

---

## 8. بررسی Firmware جدید

```text
Programming → CRC → { OK | FAIL }
```

- **CRC موفق:** `Active Bank = New Bank` و `Boot Action = BOOT` (فقط بعد از موفقیت)
- **CRC ناموفق:** Active Bank بدون تغییر؛ Retry اجرا می‌شود

---

## 9. Retry

شکست مکرر → `Retry Limit` → ثبت خطا → حفظ Active Bank قبلی → Boot → اجرای Firmware قبلی. **شکست Update هرگز نباید Firmware قبلی را از بین ببرد.**

---

## 10. اصل Fail-Safe

> **تا زمانی که Firmware جدید به‌صورت کامل Programming نشده و CRC آن تأیید نشده است، Active Bank تغییر نمی‌کند.**

قطع برق یا شکست در هر مرحله از Programming نباید بانک فعال قبلی را خراب کند.

---

## 11. پایان موفق Programming

```text
Program Inactive Bank → Verify CRC → CRC OK → Active Bank = New Bank → Boot Action = BOOT
```

تغییر Active Bank همیشه **پس از** موفقیت Verification انجام می‌شود، نه قبل یا هم‌زمان.

---

## 12. خطای Programming

```text
Boot Error Record (در STORAGE_RECOVERY)
├── Error Type
├── Error Code
└── اطلاعات لازم برای Diagnosis
```

Bootloader مسئول ثبت پایدار خطاست، نه ثبت آن در «Log».

---

## 13. انتقال خطای Boot به Application

```text
Bootloader → EEPROM Error Record → Application → Read Boot Error → [مسیر گزارش] → Clear Boot Error
```

**🔴 یادداشت باز — اصلاح‌شده نسبت به نسخه اولیه:** سند اصلی این مسیر را «نوشتن به Log» توصیف کرده بود، اما پروژه مفهوم مستقلی به اسم «Log» ندارد. مسیر درست، احتمالاً تبدیل Boot Error Record به یک **Fault** (مثلاً نوع `BOOT_PROGRAMMING_FAILURE`) و ورود آن به مسیر استاندارد Fault در `System Management` (`system-management.md` → مدیریت Fault Queue) است، نه یک مکانیزم Logging جداگانه. این باید هنگام نوشتن کد Startup/`System_Init()` صریح تصمیم‌گیری شود.

---

## 14. پاک‌سازی خطا

یک تابع مشخص برای پاک‌سازی Boot Error Record، حتی اگر فعلاً کاربرد عملیاتی گسترده نداشته باشد، بخشی از Contract است.

---

## 15. رفتار پس از شکست Update

```text
Active=1, BootAction=PROGRAM → Program Bank2 → Failure → Retry×N → Failure
   → Save Error → Keep Active=1 → BootAction=BOOT → CRC Bank1 OK → Jump to Bank1
```

---

## 16. مرز مسئولیت Bootloader

تعیین BOOT/PROGRAM · شناسایی Active Bank · بررسی CRC · اجرای Firmware معتبر · انتخاب Inactive Bank · دریافت/پردازش Firmware · Decryption · Programming · Verification · Retry · تغییر Active Bank/Boot Action پس از موفقیت · ثبت خطا در `STORAGE_RECOVERY`.

---

## 17. خارج از مسئولیت Bootloader

اجرای Application، منطق Managementها، مدیریت Event، ارتباط با Server، Policyهای محصول، فایل‌های عمومی دستگاه، GPS/Telemetry، **Configuration معنایی محصول** (هماهنگ با `configuration-infrastructure.md`).

---

## 18. توالی کلی Boot

```text
                    Power On / Reset
                           │
                     Bootloader
                           │
                    Read Boot Action
                ┌──────────┴──────────┐
              BOOT                  PROGRAM
                │                     │
        Read Active Bank       Select Inactive Bank
                │                     │
             CRC Check          Find Firmware
          ┌─────┴─────┐         Decrypt / Program
         OK          FAIL             │
          │           │              CRC
          ▼           ▼         ┌────┴────┐
        Jump        Stay Boot   OK      FAIL
                                 │          │
                                 ▼          ▼
                           Switch Bank    Retry → Failure → Save Error
                                 │              │
                                 └──────┬───────┘
                                    Boot Active
                                        │
                                 Jump to Application
```

---

## 19. اصل نهایی

> **تشخیص Firmware معتبر، اجرای آن، و در صورت درخواست Programming، جایگزینی امن Firmware با استفاده از بانک غیرفعال؛ بدون آنکه شکست Update باعث از دست رفتن Firmware قبلی شود.**

> **Active Bank فقط بعد از Programming کامل و تأیید CRC تغییر می‌کند.**

> **هر Update ناموفق باید در نهایت امکان بازگشت و اجرای Active Bank قبلی را حفظ کند.**
