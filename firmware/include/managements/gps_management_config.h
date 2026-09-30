#ifndef MGMT_GPS_CONFIG_H
#define MGMT_GPS_CONFIG_H

/* Every tunable constant for GPS Management lives in this one file, same
 * convention as managements/hardware_input_management_config.h. None of
 * these are part of the Contract (docs/02-management-contracts/
 * gps-management.md) - the Contract explicitly leaves retry/telemetry
 * timing as this Management's internal design. Every value here is a
 * placeholder pending real GNSS TTFF/behavior data from the EC200U
 * contractor; changing one never requires touching gps_management.c. */

/* --- GNSS session start (see ensure_gnss_started()) ---
 * How often GPS Management retries EC200U_GNSS_Start() while the EC200U
 * Manager reports the module is not yet READY (e.g. still booting). */
#define GNSS_START_RETRY_INTERVAL_MS      5000u

/* --- Fault escalation (see check_fault_and_recover(), gps-management.md
 * section 13) ---
 * If EC200U_GNSS_GetLatest() has not returned EC200U_OK for this long
 * since the GNSS session was started, GPS Management treats the module as
 * unresponsive and attempts Local Recovery (Stop+Start). */
#define GNSS_UNRESPONSIVE_TIMEOUT_MS      60000u

/* After this many consecutive failed Local Recovery windows, GPS
 * Management gives up locally and submits a Reset Request for EC200U to
 * System Management (reset-request-contract.md), instead of retrying
 * forever. */
#define GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS  3u

/* --- Telemetry cadence (see process_telemetry()) ---
 * NOTE: the Telemetry payload format itself is still a system-wide open
 * item (OPEN-ITEMS.md, red, #3); only the cadence is decided here. */
#define POSITION_TELEMETRY_INTERVAL_MS    30000u

/* In LOW_POWER, the Telemetry cadence is slowed down by this multiplier -
 * same convention as hardware_input_management_config.h's
 * LOW_POWER_TELEMETRY_RATE_MULTIPLIER, kept as GPS's own named constant
 * so it can be tuned independently per Capability. */
#define GPS_LOW_POWER_TELEMETRY_RATE_MULTIPLIER 4u

/* --- Displacement-triggered Telemetry (see has_moved_since_last_telemetry()) ---
 * Part of "Position Update Policy" (gps-management.md sections 3/14, left
 * as this Management's internal design): if the vehicle moves by at least
 * this much (in the same microdegree units as GpsPosition) since the last
 * Telemetry push, a push fires immediately instead of waiting for the
 * normal interval - this applies in both NORMAL and LOW_POWER, since
 * movement while LOW_POWER (ACC off) is itself notable (e.g. towing).
 * 100 microdegrees is a placeholder (~11m at the equator) chosen to sit
 * above typical GPS fix noise so small jitter near a stationary vehicle
 * does not trigger extra sends; needs real calibration against the
 * contractor's actual GNSS accuracy. */
#define POSITION_DISPLACEMENT_THRESHOLD_MICRODEGREES 100

#endif /* MGMT_GPS_CONFIG_H */
