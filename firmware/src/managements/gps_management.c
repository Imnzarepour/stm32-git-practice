#include "managements/gps_management.h"
#include "managements/gps_management_config.h"
#include "managements/power_management.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "shared/reset_request.h"

/* Contract: docs/02-management-contracts/gps-management.md
 * State/Operation pattern: docs/02-management-contracts/state-operation-contract.md
 *
 * GPS Management only ever talks to the EC200U Manager Capability Contract
 * (infrastructure/ec200u_manager.h) - never AT Command/UART/GNSS protocol
 * detail. That Contract is still a draft (OPEN-ITEMS.md, red, #1), so only
 * the functions already exposed there are used here.
 *
 * Fix State interpretation (the Contract's section 5/10 only gives the two
 * end states NO_FIX/FIX_AVAILABLE as an example, but gps_management.h
 * already commits to three: NO_FIX, FIX_ACQUIRING, FIX_VALID):
 *   - FIX_ACQUIRING = a GNSS session was started but has never produced a
 *     valid fix yet.
 *   - Losing a fix after having had one goes straight to NO_FIX (matches
 *     the Contract's FIX_AVAILABLE -> NO_FIX -> GPS_FIX_LOST diagram
 *     exactly), not back to ACQUIRING - the GNSS session keeps running in
 *     the background and does not need to be restarted to try again.
 * This is a documented implementation judgment call, not a re-opened
 * architecture decision.
 *
 * Position is never cleared on fix loss (shared-data-and-state.md section
 * 7.1: "may have a previous Position while the current Fix is invalid");
 * Consumers combine GPS_GetFixState() (is it live right now) with the
 * Position's timestamp (how fresh is it) per gps-management.md section 9.
 *
 * Latitude/Longitude scale: the Contract explicitly leaves the Fixed-Point
 * scale/unit undecided ("باید در Contract رسمی مشخص شود"). Microdegrees
 * (value = degrees * 1e6) is used here as a placeholder - a very common
 * embedded-GPS fixed-point convention - matching the scale the on-board
 * EC200U simulator (infrastructure/ec200u_manager.c) currently emits. If
 * the real EC200U component or a formal Contract update picks a different
 * scale, only this pairing needs to change, not the rest of the system
 * (no other Management currently consumes Position).
 */

typedef enum {
    OP_NOT_STARTED,
    OP_STARTED
} GnssSessionState;

/* TODO(open-item): the system-wide Telemetry payload format is still open
 * (OPEN-ITEMS.md, red, #3). This struct is a provisional, local shape for
 * Position telemetry only, mirroring the pattern already used in
 * hardware_input_management.c's BatteryTelemetryRecord - not a system-wide
 * Telemetry Contract. */
typedef struct {
    int32_t  latitude;
    int32_t  longitude;
    uint32_t timestamp;
} GpsTelemetryRecord;

static GpsPosition      s_position;
static GpsFixState      s_fix_state;
static GnssSessionState s_session_state;

static bool     s_start_attempted_once;
static TimeTick s_last_start_attempt_tick;

static TimeTick s_last_ok_tick;          /* last time EC200U_GNSS_GetLatest() returned EC200U_OK */
static uint32_t s_recovery_attempts;
static bool     s_reset_request_sent;    /* latch: submit a Reset Request at most once per unresponsive episode */

static bool     s_telemetry_started;
static TimeTick s_last_telemetry_tick;
static bool     s_have_telemetry_baseline; /* true once a Telemetry push has actually happened - see has_moved_since_last_telemetry()'s own comment for why this must gate it */
static int32_t  s_last_telemetry_latitude;
static int32_t  s_last_telemetry_longitude;

static uint32_t s_next_event_id;

static void emit_event(EventType type) {
    Event e;
    e.event_type = type;
    e.event_id   = s_next_event_id++;
    e.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetEventQueue(), &e, sizeof(e));
}

/* Step 1: (re)issue EC200U_GNSS_Start() once the module reports READY,
 * retrying on a fixed interval if the module was not ready yet. Never
 * blocks/waits - just tries again on the next Process() where the retry
 * window has elapsed. */
static void ensure_gnss_started(void) {
    if (s_session_state == OP_STARTED) {
        return;
    }

    if (EC200U_GetModuleState() != EC200U_MODULE_READY) {
        return; /* module still booting/unavailable - try again later */
    }

    if (s_start_attempted_once && !Time_Elapsed(s_last_start_attempt_tick, GNSS_START_RETRY_INTERVAL_MS)) {
        return;
    }

    s_start_attempted_once   = true;
    s_last_start_attempt_tick = Time_GetTick();

    if (EC200U_GNSS_Start() == EC200U_OK) {
        s_session_state = OP_STARTED;
        s_fix_state     = GPS_FIX_ACQUIRING;
        s_last_ok_tick  = Time_GetTick();
    }
}

/* Step 2: one non-blocking read of the latest GNSS data. Never creates a
 * new acquisition itself (that is ensure_gnss_started's job) and never
 * waits for a fix - exactly GPS_GetPosition()'s own contract, just applied
 * to the internal poll as well. */
static void poll_position(void) {
    EC200U_GnssData data;
    EC200U_Result   result;

    if (s_session_state != OP_STARTED) {
        return;
    }

    result = EC200U_GNSS_GetLatest(&data);
    if (result != EC200U_OK) {
        return; /* communication problem - handled by check_fault_and_recover() via staleness of s_last_ok_tick */
    }

    s_last_ok_tick       = Time_GetTick();
    s_recovery_attempts  = 0;
    s_reset_request_sent = false; /* communication recovered - re-arm the Reset Request latch for any future episode */

    if (data.fix_valid) {
        s_position.latitude  = data.latitude;
        s_position.longitude = data.longitude;
        s_position.timestamp = Time_GetTick(); /* GPS Management owns the Timestamp (section 9), not the modem's own clock */
        s_position.valid     = true;

        if (s_fix_state != GPS_FIX_VALID) {
            emit_event(EVENT_GPS_FIX_ACQUIRED);
        }
        s_fix_state = GPS_FIX_VALID;
    } else if (s_fix_state == GPS_FIX_VALID) {
        emit_event(EVENT_GPS_FIX_LOST);
        s_fix_state = GPS_NO_FIX;
    }
    /* else: still ACQUIRING (never had a fix yet) - nothing to change */
}

/* Step 3: Fault detection + Local Recovery + (only as a last resort)
 * Reset Request - per gps-management.md section 13 and
 * reset-request-contract.md. GPS Management never resets anything itself. */
static void check_fault_and_recover(void) {
    ResetRequest req;

    if (s_session_state != OP_STARTED) {
        return; /* nothing to recover before a GNSS session has ever been requested */
    }

    if (!Time_Elapsed(s_last_ok_tick, GNSS_UNRESPONSIVE_TIMEOUT_MS)) {
        return;
    }

    /* Local Recovery: restart the GNSS session.
     *
     * s_last_ok_tick is reset here unconditionally, regardless of whether
     * EC200U_GNSS_Start() itself reports success. At this point its job is
     * no longer "remember the last successful GetLatest()" (that meaning is
     * poll_position()'s, on the EC200U_OK path) - it is "space out Recovery
     * attempts by a full GNSS_UNRESPONSIVE_TIMEOUT_MS window", per
     * gps-management.md section 13 ("ابتدا Recovery محلی را انجام می‌دهد")
     * and reset-request-contract.md section 12 Rule 1/2 (local recovery
     * with a real chance before a Reset Request is issued).
     *
     * If Start() is left out of this update (i.e. only updated on OK), a
     * module that is genuinely unresponsive will very likely fail Start()
     * too - not just GetLatest() - and s_last_ok_tick would never advance.
     * Time_Elapsed(s_last_ok_tick, GNSS_UNRESPONSIVE_TIMEOUT_MS) would then
     * stay true forever, so every subsequent GPS_Process() call (even back
     * to back within the same Super Loop pass) would re-enter this block:
     * all GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS attempts - meant to be spaced at
     * least one full timeout window apart - would collapse into a handful
     * of consecutive ticks, issuing the Reset Request far earlier than
     * designed and hammering EC200U with rapid Stop/Start commands. */
    s_recovery_attempts++;
    (void)EC200U_GNSS_Stop();
    (void)EC200U_GNSS_Start();
    s_last_ok_tick = Time_GetTick();

    if (s_recovery_attempts >= GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS && !s_reset_request_sent) {
        req.type      = RESET_TYPE_MODULE_RESET;
        req.target    = "EC200U";
        req.source    = RESET_SOURCE_GPS;
        req.reason    = RESET_REASON_MODULE_UNRESPONSIVE;
        req.timestamp = Time_GetTick();
        System_SubmitResetRequest(&req);
        s_reset_request_sent = true; /* do not resubmit every cycle while still unresponsive */
    }
}

/* Displacement check for process_telemetry(): compares the current
 * Position against the Position at the last Telemetry push. Plain
 * per-axis delta (not a real Haversine distance) - a deliberate
 * simplification consistent with this project's Fixed-Point-over-float
 * preference, adequate for "did it move" rather than precise distance.
 *
 * Bug fix: this must never be called before s_have_telemetry_baseline is
 * true (see its own comment and process_telemetry()'s call site) -
 * s_last_telemetry_latitude/longitude start at (0,0) ("Null Island"), not
 * a real prior position. Any real-world GNSS fix is tens of millions of
 * microdegrees away from (0,0), so without that guard the very first real
 * fix after boot always reads as "moved past the threshold" and fires an
 * immediate Telemetry push - not because the vehicle moved, but purely
 * because no real baseline existed yet to compare against. */
static bool has_moved_since_last_telemetry(void) {
    int32_t dlat = s_position.latitude  - s_last_telemetry_latitude;
    int32_t dlon = s_position.longitude - s_last_telemetry_longitude;

    if (dlat < 0) dlat = -dlat;
    if (dlon < 0) dlon = -dlon;

    return (dlat >= POSITION_DISPLACEMENT_THRESHOLD_MICRODEGREES)
        || (dlon >= POSITION_DISPLACEMENT_THRESHOLD_MICRODEGREES);
}

/* Step 4: Power-Mode-aware Telemetry push - fires either when the normal
 * (Power-Mode-scaled) interval elapses, or immediately when the vehicle
 * has moved past POSITION_DISPLACEMENT_THRESHOLD_MICRODEGREES since the
 * last push, whichever comes first (Position Update Policy - see
 * gps_management_config.h). Same periodic-push skeleton as
 * hardware_input_management.c's process_telemetry(). */
static void process_telemetry(void) {
    GpsTelemetryRecord record;
    bool interval_due;
    bool moved;
    uint32_t interval_ms = POSITION_TELEMETRY_INTERVAL_MS;

    if (Power_GetMode() == POWER_LOW_POWER) {
        interval_ms *= GPS_LOW_POWER_TELEMETRY_RATE_MULTIPLIER;
    }

    if (!s_telemetry_started) {
        s_last_telemetry_tick = Time_GetTick();
        s_telemetry_started    = true;
        return;
    }

    interval_due = Time_Elapsed(s_last_telemetry_tick, interval_ms);
    moved        = s_have_telemetry_baseline && s_position.valid && has_moved_since_last_telemetry();

    if (!interval_due && !moved) {
        return;
    }
    s_last_telemetry_tick = Time_GetTick(); /* reset the clock either way - a movement-triggered push also counts as "just reported" */

    if (!s_position.valid) {
        return; /* nothing real to send yet */
    }

    record.latitude  = s_position.latitude;
    record.longitude = s_position.longitude;
    record.timestamp = s_position.timestamp;
    (void)Queue_Push(Queues_GetTelemetryQueue(), &record, sizeof(record));

    s_last_telemetry_latitude  = s_position.latitude;
    s_last_telemetry_longitude = s_position.longitude;
    s_have_telemetry_baseline  = true;
}

void GPS_Init(void) {
    GpsPosition zero_position = {0};

    s_position      = zero_position;
    s_fix_state     = GPS_NO_FIX;
    s_session_state = OP_NOT_STARTED;

    s_start_attempted_once    = false;
    s_last_start_attempt_tick = 0;

    s_last_ok_tick        = Time_GetTick();
    s_recovery_attempts   = 0;
    s_reset_request_sent  = false;

    s_telemetry_started        = false;
    s_last_telemetry_tick      = 0;
    s_have_telemetry_baseline  = false;
    s_last_telemetry_latitude  = 0;
    s_last_telemetry_longitude = 0;

    s_next_event_id = 1;
}

void GPS_Process(void) {
    ensure_gnss_started();
    poll_position();
    check_fault_and_recover();
    process_telemetry();
}

GpsPosition GPS_GetPosition(void) {
    return s_position;
}

GpsFixState GPS_GetFixState(void) {
    return s_fix_state;
}
