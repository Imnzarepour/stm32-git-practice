#ifndef MGMT_FIRMWARE_UPDATE_CONFIG_H
#define MGMT_FIRMWARE_UPDATE_CONFIG_H

/* Every tunable constant for Firmware Update Management lives in this one
 * file, same convention as gps_management_config.h/server_management_config.h/
 * sms_management_config.h. None of these are part of the Contract - this
 * Management's own internal pacing choice (explicit user decision,
 * OPEN-ITEMS.md: "همه تجهیزات باید در حالت کم‌مصرف نرخ دیگری داشته باشند -
 * با هدف کاهش تعداد پراسس و کم کردن مصرف"). */

/* How often FirmwareUpdate_Process() actually does work (calls
 * FileTransfer_StartDownload()/FileTransfer_GetStatus()) while a Download
 * is REQUESTED or DOWNLOADING, in POWER_NORMAL. A Download is not
 * latency-critical at sub-second granularity, so this does not need to be
 * 0 (every single Super Loop tick) even in NORMAL - same reasoning
 * CALL_IDLE_POLL_INTERVAL_MS (call_management_config.h) already uses for
 * Call Management's own background poll. */
#define FIRMWARE_UPDATE_CHECK_INTERVAL_MS 200u

/* POWER_LOW_POWER (ACC OFF): multiplies the interval above - same
 * *_LOW_POWER_*_MULTIPLIER shape already used by
 * GPS_LOW_POWER_TELEMETRY_RATE_MULTIPLIER (gps_management_config.h),
 * CALL_LOW_POWER_POLL_RATE_MULTIPLIER (call_management_config.h) and
 * LOW_POWER_TELEMETRY_RATE_MULTIPLIER (hardware_input_management_config.h) -
 * so this Management's own contribution to Super Loop work / EC200U
 * traffic drops in step with everything else, not left at the NORMAL rate. */
#define FIRMWARE_UPDATE_LOW_POWER_RATE_MULTIPLIER 4u

#endif /* MGMT_FIRMWARE_UPDATE_CONFIG_H */
