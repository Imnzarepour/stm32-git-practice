#ifndef EC200U_MANUAL_TEST_H
#define EC200U_MANUAL_TEST_H
#include <stdint.h>
#include <stdbool.h>

/* ابزار تست دستی EC200U روی سخت‌افزار واقعی (MCU_F4) - عمداً ساده و
 * BLOCKING (بر خلاف اصل غیرقابل‌مذاکره #۱ CLAUDE.md برای بقیه Firmware)،
 * چون هدفش اجرای یک اقدام و دیدن نتیجه‌اش پشت Debugger/در یک لحظه است، نه
 * اجرای تولیدی در Super Loop. هیچ‌کدام از این توابع را نباید از
 * firmware_main.c یا هیچ Management صدا زد - این‌ها فقط برای فراخوانی
 * دستی هستند (مثلاً از MCU_F4/Core/Src/main.c's USER CODE، یا مستقیم زیر
 * Debugger با Set Next Statement).
 *
 * هر تابع داخلش EC200U_Process() را مکرر صدا می‌زند تا وقتی نتیجه بیاید یا
 * Timeout بخورد - همان کاری که Super Loop طبیعتاً برای Managementهای واقعی
 * انجام می‌دهد، فقط اینجا در یک حلقه Blocking به‌جای Super Loop واقعی.
 *
 * پیش‌نیاز: `EC200UTest_Init()` باید یک‌بار در ابتدای `main()` صدا زده شود
 * (به‌جای `Firmware_Main()` کامل - یا قبل از آن، برای تست جدا). */

void EC200UTest_Init(void);

/* منتظر می‌ماند تا ماژول EC200U واقعاً READY شود (توالی کامل AT Init -
 * ec200u_manager.c). قبل از هر تابع دیگر این فایل باید صدا زده شود و
 * true برگرداند. */
bool EC200UTest_WaitModuleReady(uint32_t timeout_ms);

/* --- SMS --- */

/* یک پیامک متنی "Hello World" به شماره داده‌شده می‌فرستد و منتظر تأیید
 * واقعی می‌ماند (نه فقط Accept محلی - EC200U_SMS_GetSendStatus()). */
bool EC200UTest_SendHelloWorldSms(const char* destination_number);

/* منتظر یک پیامک ورودی می‌ماند (حداکثر تا timeout_ms). در صورت دریافت،
 * شماره فرستنده و متن را در بافرهای caller کپی می‌کند (همیشه NUL-ترمینیت،
 * کوتاه‌شده در صورت جاگیر نبودن) و true برمی‌گرداند. */
bool EC200UTest_WaitForIncomingSms(char* sender_out, uint32_t sender_out_size,
                                    char* text_out, uint32_t text_out_size,
                                    uint32_t timeout_ms);

/* --- تماس صوتی --- */

/* شماره را می‌گیرد، منتظر می‌ماند تماس ACTIVE شود، `hold_ms` نگه می‌دارد،
 * بعد قطع می‌کند. اگر شماره اصلاً وصل نشد (رد شد/بوق اشغال/Timeout قبل از
 * ACTIVE شدن) false برمی‌گرداند. */
bool EC200UTest_MakeCall(const char* number, uint32_t hold_ms);

/* منتظر یک تماس ورودی می‌ماند (حداکثر تا timeout_ms)، در صورت رسیدن جواب
 * می‌دهد، `hold_ms` نگه می‌دارد، بعد قطع می‌کند. */
bool EC200UTest_WaitForIncomingCallAndAnswer(uint32_t timeout_ms, uint32_t hold_ms);

/* --- ارتباط با سرور (MQTT Capability) --- */

/* به سرور وصل می‌شود و منتظر CONNECTED واقعی می‌ماند (نه فقط Accept محلی). */
bool EC200UTest_ConnectServer(const char* client_id, const char* host, uint16_t port,
                               uint32_t timeout_ms);

/* یک پیام "Hello World" روی topic داده‌شده Publish می‌کند - باید بعد از
 * یک EC200UTest_ConnectServer() موفق صدا زده شود. Fire-and-forget طبق
 * Contract خودِ EC200U_MQTT_Publish (نگاه کن ec200u_manager.h) - این تابع
 * فقط چند ثانیه EC200U_Process() را می‌چرخاند تا دنباله AT واقعاً از UART
 * خارج شود، منتظر تأیید Backend نمی‌ماند. */
bool EC200UTest_SendHelloWorldToServer(const char* topic);

#endif /* EC200U_MANUAL_TEST_H */
