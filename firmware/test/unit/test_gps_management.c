#include "unity.h"
#include "managements/gps_management.h"
#include "managements/gps_management_config.h"
#include "Mockec200u_manager.h"
#include "Mockpower_management.h"
#include "Mockreset_request.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"

TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")

/*
 * GPS Management's only real dependencies are the EC200U Manager Capability
 * Contract, Power Mode, and the Reset Request path - all three are mocked
 * here (CMock), same pattern as test_power_management.c. This keeps the
 * test isolated both from the on-board EC200U simulator
 * (infrastructure/ec200u_manager.c) and from System Management's own
 * (stub) Reset handling - GPS Management only ever sees the Contracts,
 * never a specific implementation, exactly like production code.
 *
 * Every mocked call is wired via *_StubWithCallback (not *_ExpectAndReturn)
 * because GPS_Process() calls several of these conditionally, in an order
 * that depends on internal state; StubWithCallback lets each test just set
 * a handful of static "scenario" variables and call GPS_Process(), same
 * technique test_hardware_input_management.c uses for the Hardware Proxy.
 */

#define TELEMETRY_MIRROR_FIELDS int32_t latitude; int32_t longitude; uint32_t timestamp
typedef struct { TELEMETRY_MIRROR_FIELDS; } GpsTelemetryMirror;

static EC200U_ModuleState s_stub_module_state;
static EC200U_Result      s_stub_start_result;
static EC200U_Result      s_stub_stop_result;
static EC200U_Result      s_stub_get_latest_result;
static bool                s_stub_fix_valid;
static int32_t              s_stub_lat;
static int32_t              s_stub_lon;

/* Counts every EC200U_GNSS_Start() invocation (initial session start plus
 * every Local Recovery attempt) - used by the regression test below to
 * observe how many Recovery attempts actually happened, without needing a
 * dedicated internal getter for s_recovery_attempts. */
static uint32_t s_start_call_count;

static PowerMode s_stub_power_mode;

static int         s_reset_request_count;
static ResetRequest s_last_reset_request;

static EC200U_ModuleState stub_get_module_state(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_module_state;
}

static EC200U_Result stub_gnss_start(int cmock_num_calls) {
    (void)cmock_num_calls;
    s_start_call_count++;
    return s_stub_start_result;
}

static EC200U_Result stub_gnss_stop(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_stop_result;
}

static EC200U_Result stub_gnss_get_latest(EC200U_GnssData* data, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (data) {
        data->latitude  = s_stub_lat;
        data->longitude = s_stub_lon;
        data->timestamp = Time_GetTick();
        data->fix_valid = s_stub_fix_valid;
    }
    return s_stub_get_latest_result;
}

static PowerMode stub_power_get_mode(int cmock_num_calls) {
    (void)cmock_num_calls;
    return s_stub_power_mode;
}

static void stub_submit_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_request_count++;
    if (request) {
        s_last_reset_request = *request;
    }
}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

void setUp(void) {
    Time_Init();
    Queues_Init();
    GPS_Init();

    s_stub_module_state      = EC200U_MODULE_READY;
    s_stub_start_result      = EC200U_OK;
    s_stub_stop_result       = EC200U_OK;
    s_stub_get_latest_result = EC200U_OK;
    s_stub_fix_valid         = false;
    s_stub_lat               = 0;
    s_stub_lon               = 0;

    s_stub_power_mode = POWER_NORMAL;

    s_reset_request_count = 0;
    s_start_call_count    = 0;

    EC200U_GetModuleState_StubWithCallback(stub_get_module_state);
    EC200U_GNSS_Start_StubWithCallback(stub_gnss_start);
    EC200U_GNSS_Stop_StubWithCallback(stub_gnss_stop);
    EC200U_GNSS_GetLatest_StubWithCallback(stub_gnss_get_latest);
    Power_GetMode_StubWithCallback(stub_power_get_mode);
    System_SubmitResetRequest_StubWithCallback(stub_submit_reset_request);
}

void tearDown(void) {}

/* --- Position / Fix cycle --- */

void test_fix_state_starts_at_no_fix_with_no_position(void) {
    TEST_ASSERT_EQUAL_INT(GPS_NO_FIX, GPS_GetFixState());
    TEST_ASSERT_FALSE(GPS_GetPosition().valid);
}

void test_first_process_starts_the_gnss_session_and_enters_acquiring(void) {
    GPS_Process();
    TEST_ASSERT_EQUAL_INT(GPS_FIX_ACQUIRING, GPS_GetFixState());
}

void test_gnss_start_is_retried_while_module_not_ready(void) {
    s_stub_module_state = EC200U_MODULE_INITIALIZING;

    GPS_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(GPS_NO_FIX, GPS_GetFixState(), "Must not start a session before the module is READY");

    s_stub_module_state = EC200U_MODULE_READY;
    GPS_Process();
    TEST_ASSERT_EQUAL_INT(GPS_FIX_ACQUIRING, GPS_GetFixState());
}

void test_fix_acquired_updates_position_and_emits_event(void) {
    GPS_Process(); /* issues GNSS_Start, enters ACQUIRING */

    s_stub_fix_valid = true;
    s_stub_lat = 35700000;
    s_stub_lon = 51400000;
    GPS_Process();

    GpsPosition pos = GPS_GetPosition();
    Event evt;
    uint32_t len;

    TEST_ASSERT_EQUAL_INT(GPS_FIX_VALID, GPS_GetFixState());
    TEST_ASSERT_TRUE(pos.valid);
    TEST_ASSERT_EQUAL_INT32(35700000, pos.latitude);
    TEST_ASSERT_EQUAL_INT32(51400000, pos.longitude);

    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &evt, sizeof(evt), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_GPS_FIX_ACQUIRED, evt.event_type);
}

void test_fix_lost_after_valid_emits_event_and_keeps_last_position(void) {
    GPS_Process();
    s_stub_fix_valid = true;
    s_stub_lat = 10;
    s_stub_lon = 20;
    GPS_Process(); /* now FIX_VALID */
    Queue_Clear(Queues_GetEventQueue()); /* isolate the LOST event from the earlier ACQUIRED one */

    s_stub_fix_valid = false;
    GPS_Process();

    GpsPosition pos = GPS_GetPosition();
    Event evt;
    uint32_t len;

    TEST_ASSERT_EQUAL_INT(GPS_NO_FIX, GPS_GetFixState());
    TEST_ASSERT_TRUE_MESSAGE(pos.valid, "Last known Position must be retained after Fix loss (shared-data-and-state.md 7.1)");
    TEST_ASSERT_EQUAL_INT32(10, pos.latitude);
    TEST_ASSERT_EQUAL_INT32(20, pos.longitude);

    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetEventQueue(), &evt, sizeof(evt), &len));
    TEST_ASSERT_EQUAL_INT(EVENT_GPS_FIX_LOST, evt.event_type);
}

void test_no_fix_yet_does_not_emit_any_event(void) {
    GPS_Process(); /* ACQUIRING, never had a fix */
    GPS_Process();
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetEventQueue()));
}

/* --- Telemetry cadence, Power Mode aware --- */

static void acquire_a_fix(void) {
    GPS_Process();
    s_stub_fix_valid = true;
    s_stub_lat = 1;
    s_stub_lon = 2;
    GPS_Process();
}

void test_telemetry_is_pushed_at_the_normal_interval_once_position_is_valid(void) {
    acquire_a_fix();
    Queue_Clear(Queues_GetTelemetryQueue());

    advance_ms(POSITION_TELEMETRY_INTERVAL_MS);
    GPS_Process();

    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetTelemetryQueue()));

    GpsTelemetryMirror record;
    uint32_t len;
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetTelemetryQueue(), &record, sizeof(record), &len));
    TEST_ASSERT_EQUAL_UINT32(sizeof(record), len);
    TEST_ASSERT_EQUAL_INT32(1, record.latitude);
    TEST_ASSERT_EQUAL_INT32(2, record.longitude);
}

void test_telemetry_interval_is_stretched_in_low_power(void) {
    s_stub_power_mode = POWER_LOW_POWER;
    acquire_a_fix();
    Queue_Clear(Queues_GetTelemetryQueue());

    advance_ms(POSITION_TELEMETRY_INTERVAL_MS);
    GPS_Process();
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "Must not fire yet at the NORMAL interval - LOW_POWER stretches it by the multiplier");

    advance_ms(POSITION_TELEMETRY_INTERVAL_MS * (GPS_LOW_POWER_TELEMETRY_RATE_MULTIPLIER - 1u));
    GPS_Process();
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetTelemetryQueue()));
}

void test_significant_displacement_triggers_immediate_telemetry(void) {
    acquire_a_fix(); /* lat=1, lon=2 */
    advance_ms(POSITION_TELEMETRY_INTERVAL_MS);
    GPS_Process(); /* normal interval push - establishes the (1,2) reference point */
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetTelemetryQueue()));
    Queue_Clear(Queues_GetTelemetryQueue());

    s_stub_lat = 1 + (int32_t)POSITION_DISPLACEMENT_THRESHOLD_MICRODEGREES + 10;
    GPS_Process(); /* well before the next interval is due */

    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "A position jump past the displacement threshold must trigger an immediate Telemetry push");
}

void test_small_position_noise_does_not_trigger_extra_telemetry(void) {
    acquire_a_fix(); /* lat=1, lon=2 */
    advance_ms(POSITION_TELEMETRY_INTERVAL_MS);
    GPS_Process(); /* establishes the (1,2) reference point */
    Queue_Clear(Queues_GetTelemetryQueue());

    s_stub_lat = 1 + 1; /* well under the threshold - GPS jitter, not real movement */
    GPS_Process();

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "Small position noise below the displacement threshold must not trigger an extra Telemetry push");
}

void test_first_ever_fix_does_not_trigger_a_false_displacement_push(void) {
    /* Bug fix regression: s_last_telemetry_latitude/longitude used to start
     * at (0,0) ("Null Island"), so the very first real-world-scale fix
     * (tens of millions of microdegrees away from (0,0)) always looked
     * like "moved past the threshold" and fired an immediate Telemetry
     * push - not because of real movement, just because no real baseline
     * existed yet. Use realistic-scale coordinates (unlike acquire_a_fix()'s
     * tiny lat=1/lon=2 fixture, which never happened to expose this) to
     * prove it is fixed: the first push must wait for the normal interval,
     * exactly like test_telemetry_is_pushed_at_the_normal_interval_once_position_is_valid. */
    GPS_Process(); /* ACQUIRING */
    s_stub_fix_valid = true;
    s_stub_lat = 35700000;  /* ~35.7 degrees, realistic microdegree scale */
    s_stub_lon = 51400000;  /* ~51.4 degrees */
    GPS_Process(); /* fix becomes valid */

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "The first real fix must not fire an immediate Telemetry push by itself - only a real prior baseline can trigger the displacement path");

    advance_ms(POSITION_TELEMETRY_INTERVAL_MS - 1);
    GPS_Process();
    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "Still not due - must wait out the full normal interval for the first push, same as any other push");

    advance_ms(1);
    GPS_Process();
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "Once the interval actually elapses, the first push must go through normally");
}

void test_telemetry_not_pushed_before_a_valid_position_exists(void) {
    GPS_Process(); /* ACQUIRING only, no fix ever */
    advance_ms(POSITION_TELEMETRY_INTERVAL_MS * 2u);
    GPS_Process();

    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetTelemetryQueue()));
}

/* --- Fault -> Local Recovery -> Reset Request --- */

void test_persistent_failure_triggers_local_recovery_then_reset_request(void) {
    GPS_Process(); /* session started */
    s_stub_get_latest_result = EC200U_ERROR;

    for (uint32_t attempt = 0; attempt < GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS; attempt++) {
        advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
        GPS_Process();
    }

    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_GPS, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_MODULE_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_MODULE_UNRESPONSIVE, s_last_reset_request.reason);
}

void test_reset_request_is_not_resubmitted_while_still_unresponsive(void) {
    GPS_Process();
    s_stub_get_latest_result = EC200U_ERROR;

    for (uint32_t attempt = 0; attempt < GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS; attempt++) {
        advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
        GPS_Process();
    }
    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);

    advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
    GPS_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_reset_request_count, "Must not spam a new Reset Request every cycle while still unresponsive");
}

void test_recovering_before_max_attempts_clears_the_failure_counter(void) {
    GPS_Process();
    s_stub_get_latest_result = EC200U_ERROR;

    advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
    GPS_Process(); /* 1st local recovery attempt (counter now at 1 of GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS) */

    s_stub_get_latest_result = EC200U_OK; /* communication recovers - must clear the counter back to 0 */
    GPS_Process();

    /* If the counter had NOT been cleared, one more attempt carried over
     * from before would combine with these to reach
     * GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS and fire early. Running only
     * GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS - 1 more failures must stay below
     * the threshold if (and only if) the counter really reset. */
    s_stub_get_latest_result = EC200U_ERROR;
    for (uint32_t attempt = 0; attempt < GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS - 1u; attempt++) {
        advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
        GPS_Process();
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count,
        "A prior recovery must reset the failure counter, so this second episode should not yet reach GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS");
}

/* Regression test: when the module is unresponsive enough that
 * EC200U_GNSS_Start() *also* fails during Local Recovery (not just
 * EC200U_GNSS_GetLatest()), each Recovery attempt must still be spaced out
 * by a full GNSS_UNRESPONSIVE_TIMEOUT_MS window - calling GPS_Process()
 * repeatedly without advancing time must not spawn extra attempts nor
 * fire the Reset Request early. Before the fix, s_last_ok_tick was only
 * refreshed when EC200U_GNSS_Start() returned EC200U_OK; with Start()
 * always failing here, s_last_ok_tick never advanced, so every subsequent
 * GPS_Process() call (even with zero elapsed time) re-entered the Recovery
 * block, and GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS were consumed in a handful
 * of back-to-back calls instead of GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS full
 * timeout windows. */
void test_recovery_attempts_are_spaced_out_even_when_start_also_fails(void) {
    GPS_Process(); /* initial session start succeeds */
    TEST_ASSERT_EQUAL_UINT32(1, s_start_call_count);

    s_stub_get_latest_result = EC200U_ERROR;
    s_stub_start_result      = EC200U_ERROR; /* the module is unresponsive even to Start() during Recovery */

    /* Calling Process() repeatedly before a full timeout window has
     * elapsed since the last recovery reference point must not attempt any
     * Recovery yet. */
    for (int i = 0; i < 5; i++) {
        GPS_Process();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s_start_call_count,
        "Must not attempt Local Recovery before GNSS_UNRESPONSIVE_TIMEOUT_MS has elapsed");
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);

    advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
    GPS_Process(); /* 1st Local Recovery attempt - Start() fails too */
    TEST_ASSERT_EQUAL_UINT32(2, s_start_call_count);
    TEST_ASSERT_EQUAL_INT(0, s_reset_request_count);

    /* The core regression check: several immediate, back-to-back
     * GPS_Process() calls (zero elapsed time) right after a failed
     * Recovery attempt must NOT be treated as new attempts, and must NOT
     * push the Reset Request out early. */
    for (int i = 0; i < 5; i++) {
        GPS_Process();
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2, s_start_call_count,
        "Recovery attempts must be spaced by a full GNSS_UNRESPONSIVE_TIMEOUT_MS window, "
        "not repeated on every consecutive Process() call even when Start() also fails");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count,
        "Must not reach GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS (and submit a Reset Request) earlier than designed");

    /* Complete the remaining attempts, each properly spaced a full window
     * apart, and confirm the Reset Request is only submitted once the
     * configured attempt count is genuinely reached. */
    for (uint32_t attempt = 1; attempt < GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS; attempt++) {
        advance_ms(GNSS_UNRESPONSIVE_TIMEOUT_MS);
        GPS_Process();
    }

    TEST_ASSERT_EQUAL_UINT32(GNSS_MAX_LOCAL_RECOVERY_ATTEMPTS + 1u, s_start_call_count);
    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_GPS, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_MODULE_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_MODULE_UNRESPONSIVE, s_last_reset_request.reason);
}
