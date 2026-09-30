#ifndef MGMT_GPS_H
#define MGMT_GPS_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/gps-management.md
 * وابسته به EC200U Manager (که Contract نهایی‌اش هنوز باز است).
 * Fixed-Point برای Lat/Long، نه float. */

typedef struct {
    int32_t  latitude;
    int32_t  longitude;
    uint32_t timestamp;
    bool     valid;
} GpsPosition;

typedef enum {
    GPS_NO_FIX,
    GPS_FIX_ACQUIRING,
    GPS_FIX_VALID
} GpsFixState;

void        GPS_Init(void);
void        GPS_Process(void);
GpsPosition GPS_GetPosition(void); /* Read-Only, Non-Blocking, بدون Acquisition جدید, منتظر Fix نمی‌ماند */
GpsFixState GPS_GetFixState(void);

#endif
