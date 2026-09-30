#ifndef DRIVER_LED_H
#define DRIVER_LED_H
#include <stdbool.h>
/* Contract source: docs/03-infrastructure-contracts/driver-layer.md §13
 * چه‌زمانی/چرا روشن شود مسئولیت System Management است، نه این Driver. */
typedef enum { LED_GSM, LED_GPS, LED_SIM, LED_POWER } LED_Id;
typedef enum { LED_OFF, LED_ON, LED_BLINK_SLOW, LED_BLINK_FAST } LED_State;
void LED_Set(LED_Id id, LED_State state);
void Buzzer_Set(bool on);
#endif
