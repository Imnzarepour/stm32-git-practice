# نقشه راه استقرار Firmware — از صفر تا تحویل

این سند توالی پیشنهادی مراحل ساخت Firmware را از زیرساخت پایه تا تحویل نهایی مشخص می‌کند. ترتیب فازها بر اساس وابستگی فنی بین بخش‌ها تعیین شده (مثلاً Queue/Time Base قبل از هر Management، EC200U قبل از GPS/Server/SMS/Call، Bootloader بعد از Storage).

**وضعیت واقعی هر مورد در این نقشه ثابت نیست و باید همیشه از `README.md` (بخش «وضعیت فعلی پیاده‌سازی») و `docs/04-open-items/OPEN-ITEMS.md` خوانده شود، نه از این فایل.** این سند فقط توالی و دسته‌بندی فازها را نگه می‌دارد؛ علامت‌گذاری اینکه کدام مورد امروز انجام شده، اینجا نگهداری نمی‌شود تا با پیشرفت واقعی پروژه از رده خارج نشود.

قاعده ثابت هر مرحله: **Contract → Implementation → Unit Test → Integration Test → Build/CI → Commit → تحویل مرحله.**

---

## فاز ۱ — زیرساخت پایه

1. Queue Infrastructure
2. Time Base
3. Error / Fault Infrastructure
4. Storage Interface و Stub
5. Driver Interface / Fake Infrastructure
6. Event Policy / Dispatcher

## فاز ۲ — هسته سیستم

7. System Management
8. Reset Management
9. Power Management

## فاز ۳ — ورودی و خروجی سخت‌افزار

10. Hardware Input Management
11. Actuator Management
12. ACC / Door / Shock / Battery Integration

## فاز ۴ — EC200U

13. UART / DMA Driver
14. EC200U AT Engine
15. Response Parser
16. URC Handler
17. EC200U State Management
18. SIM / Network Registration
19. Signal Quality
20. Data Connection
21. MQTT / HTTP
22. SMS Capability
23. Call Capability
24. GNSS Capability
25. EC200U Recovery
26. EC200U Power Control

## فاز ۵ — قابلیت‌های محصول

27. GPS Management
28. Server Management
29. SMS Management
30. Call Management
31. Event → Communication Integration
32. Telemetry
33. Offline Data / Storage Integration

## فاز ۶ — خودرو و عیب‌یابی

34. CAN Driver
35. K-Line Driver
36. Diagnostic Management
37. DTC
38. Vehicle Diagnostic Integration

## فاز ۷ — حافظه و داده

39. NOR Driver
40. Storage Engine
41. GPS Storage
42. Log Storage
43. Diagnostic Storage
44. Firmware Update Storage
45. Persistence / Recovery Tests

## فاز ۸ — Firmware Update و Boot

46. Bootloader
47. Firmware Image Validation
48. Firmware Update Management
49. Dual-Bank / Rollback
50. Firmware Update End-to-End

## فاز ۹ — یکپارچه‌سازی سیستم

51. Super Loop Integration
52. Management Interaction Tests
53. Event / Request End-to-End Tests
54. Power Mode Integration
55. Reset / Recovery Integration
56. Storage / Communication Integration

## فاز ۱۰ — اعتبارسنجی روی برد

57. Board Bring-Up
58. Hardware Integration Test
59. EC200U Real Hardware Test
60. GNSS Real Hardware Test
61. CAN / K-Line Test
62. Power Consumption Test
63. Sleep / Wake Test
64. Firmware Update Real Hardware Test

## فاز ۱۱ — تست محصول

65. Functional Test
66. Fault Injection Test
67. Recovery Test
68. Long-Run / Stability Test
69. Communication Loss Test
70. Power-Cycle Test
71. Offline / Online Transition Test
72. Vehicle Test

## فاز ۱۲ — تحویل

73. Regression Test
74. Release Candidate Build
75. Release Validation
76. Firmware Version / Traceability
77. Release Package
78. Documentation
79. Final Acceptance
