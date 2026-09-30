#include "unity.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/ec200u_manager_config.h"
#include "infrastructure/time_base.h"
#include "Mockhardware_proxy_uart.h"
#include <string.h>

TEST_SOURCE_FILE("infrastructure/time_base.c")

/*
 * Tests the REAL AT Command driver (src/infrastructure/ec200u_manager.c) -
 * explicit user decision: the EC200U Component is no longer outsourced,
 * this file implements the actual AT/UART protocol now. hardware_proxy/
 * hardware_proxy_uart.h is Mocked (the one Hardware Proxy Domain this file
 * actually touches, per docs/03-infrastructure-contracts/hardware-proxy.md's
 * own testing rule) and drives a scripted fake EC200U module: every test
 * below asserts the exact AT command this driver sent, then feeds back the
 * exact bytes a real module would reply with (including URCs), and checks
 * the resulting Capability-level state/result - the same "mock only the
 * Hardware Proxy boundary, not the business logic above it" discipline
 * test_diagnostic_management.c already uses for hardware_proxy_can.h.
 *
 * AT syntax used throughout mirrors the Quectel EC200U AT Commands Manual
 * as documented in ec200u_manager.c's own top comment - these tests
 * validate this driver's OWN state machine/parsing against that documented
 * syntax, they cannot validate the real module itself (no hardware in this
 * Repo, see OPEN-ITEMS.md).
 */

#define TEST_INIT_SETTLE_MS EC200U_INIT_POWERUP_SETTLE_MS

static char     s_last_sent[256];
static int      s_send_call_count;

static uint8_t  s_pending_rx[2048];
static uint32_t s_pending_rx_len;
static uint32_t s_pending_rx_offset;

static HWProxy_UartResult stub_uart_send(const uint8_t* data, uint32_t length, int cmock_num_calls) {
    (void)cmock_num_calls;
    uint32_t copy_len = (length < sizeof(s_last_sent) - 1u) ? length : sizeof(s_last_sent) - 1u;
    memcpy(s_last_sent, data, copy_len);
    s_last_sent[copy_len] = '\0';
    /* Strip the trailing "\r\n" at_send() always appends, so tests compare
     * against the plain command text ("AT+CSQ", not "AT+CSQ\r\n"). */
    uint32_t text_len = (uint32_t)strlen(s_last_sent);
    if (text_len >= 2u && s_last_sent[text_len - 2u] == '\r' && s_last_sent[text_len - 1u] == '\n') {
        s_last_sent[text_len - 2u] = '\0';
    }
    s_send_call_count++;
    return HWPROXY_UART_OK;
}

static uint32_t stub_uart_read(uint8_t* buffer, uint32_t buffer_size, int cmock_num_calls) {
    (void)cmock_num_calls;
    uint32_t remaining = s_pending_rx_len - s_pending_rx_offset;
    uint32_t n         = (remaining < buffer_size) ? remaining : buffer_size;
    if (n > 0u) {
        memcpy(buffer, &s_pending_rx[s_pending_rx_offset], n);
        s_pending_rx_offset += n;
    }
    return n;
}

static void hwproxy_uart_init_noop(int cmock_num_calls) { (void)cmock_num_calls; }

/* Appends raw bytes (a literal AT response, including any "\r\n" the test
 * writes explicitly) to what stub_uart_read() will serve on the next
 * EC200U_Process() call(s). */
static void queue_reply(const char* text) {
    uint32_t len = (uint32_t)strlen(text);
    TEST_ASSERT_TRUE_MESSAGE(s_pending_rx_len + len <= sizeof(s_pending_rx), "test fixture buffer too small - grow s_pending_rx");
    memcpy(&s_pending_rx[s_pending_rx_len], text, len);
    s_pending_rx_len += len;
}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) {
        Time_TickIncrement();
    }
}

/* One send-then-reply cycle: asserts the driver sends exactly
 * `expected_cmd` on the next Process() call, then feeds back `reply` (a
 * full response INCLUDING the trailing "OK\r\n"/"ERROR\r\n" - the test
 * author's job, since a real module always terminates with one) and lets
 * a second Process() call consume/resolve it. See this file's own top
 * comment for the exact two-Process()-calls-per-step trace this depends on. */
static void expect_and_reply(const char* expected_cmd, const char* reply) {
    /* Some internal state transitions (e.g. the Power-Settle delay handing
     * off to the first "AT" probe) consume one Process() call just to move
     * to the next step, per this project's own "one small step per call"
     * discipline - retried a few times so this helper does not need to
     * know which steps happen to need the extra tick. */
    int before = s_send_call_count;
    for (int attempt = 0; attempt < 5 && s_send_call_count == before; attempt++) {
        EC200U_Process();
    }
    TEST_ASSERT_EQUAL_STRING(expected_cmd, s_last_sent);
    queue_reply(reply);
    EC200U_Process();
}

static void boot_module(void) {
    advance_ms(TEST_INIT_SETTLE_MS);
    expect_and_reply("AT", "OK\r\n");
    expect_and_reply("ATE0", "OK\r\n");
    expect_and_reply("AT+CPIN?", "+CPIN: READY\r\nOK\r\n");
    expect_and_reply("AT+CREG?", "+CREG: 0,1\r\nOK\r\n");
    expect_and_reply("AT+CSQ", "+CSQ: 20,99\r\nOK\r\n");
    expect_and_reply("AT+CMGF=1", "OK\r\n");
    expect_and_reply("AT+CNMI=2,1,0,0,0", "OK\r\n");
    expect_and_reply("AT+QIACT=1", "OK\r\n");
}

void setUp(void) {
    Time_Init();

    s_last_sent[0]    = '\0';
    s_send_call_count = 0;
    s_pending_rx_len    = 0u;
    s_pending_rx_offset = 0u;

    HWProxy_UART_Init_StubWithCallback(hwproxy_uart_init_noop);
    HWProxy_UART_Send_StubWithCallback(stub_uart_send);
    HWProxy_UART_Read_StubWithCallback(stub_uart_read);

    EC200U_Init();
    EC200U_Debug_ClearCallDialOverride();
    EC200U_Debug_ClearDataSendOverride();
    EC200U_Debug_ClearMqttPublishOverride();
}

void tearDown(void) {}

/* ==========================================================================
 * Module Init / Cellular status
 * ========================================================================== */

void test_module_is_initializing_right_after_init(void) {
    TEST_ASSERT_EQUAL_INT(EC200U_MODULE_INITIALIZING, EC200U_GetModuleState());
}

void test_module_does_not_probe_before_the_settle_delay_elapses(void) {
    advance_ms(TEST_INIT_SETTLE_MS - 1u);
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT(0, s_send_call_count);
    TEST_ASSERT_EQUAL_INT(EC200U_MODULE_INITIALIZING, EC200U_GetModuleState());
}

void test_module_probe_is_retried_until_the_module_answers(void) {
    advance_ms(TEST_INIT_SETTLE_MS);
    EC200U_Process(); /* Power-Settle -> Probe-AT transition */
    EC200U_Process(); /* actually sends "AT" */
    TEST_ASSERT_EQUAL_STRING("AT", s_last_sent);

    /* No reply queued - the probe times out. */
    advance_ms(AT_DEFAULT_TIMEOUT_MS);
    EC200U_Process(); /* resolves the timeout (ERROR-equivalent) */
    TEST_ASSERT_EQUAL_INT(EC200U_MODULE_INITIALIZING, EC200U_GetModuleState());

    s_send_call_count = 0;
    advance_ms(EC200U_INIT_PROBE_RETRY_MS);
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_send_call_count, "must retry \"AT\" after the retry interval, not give up");
    TEST_ASSERT_EQUAL_STRING("AT", s_last_sent);
}

void test_module_becomes_ready_after_the_full_init_sequence(void) {
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_MODULE_READY, EC200U_GetModuleState());
    TEST_ASSERT_EQUAL_INT(EC200U_NETWORK_REGISTERED, EC200U_GetNetworkState());
    TEST_ASSERT_EQUAL_INT(EC200U_SIMCARD_READY, EC200U_GetSimCardState());
}

void test_sim_not_ready_before_init_completes(void) {
    TEST_ASSERT_EQUAL_INT(EC200U_SIMCARD_NOT_READY, EC200U_GetSimCardState());
}

void test_module_still_becomes_ready_even_if_pdp_activation_fails(void) {
    advance_ms(TEST_INIT_SETTLE_MS);
    expect_and_reply("AT", "OK\r\n");
    expect_and_reply("ATE0", "OK\r\n");
    expect_and_reply("AT+CPIN?", "+CPIN: READY\r\nOK\r\n");
    expect_and_reply("AT+CREG?", "+CREG: 0,1\r\nOK\r\n");
    expect_and_reply("AT+CSQ", "+CSQ: 20,99\r\nOK\r\n");
    expect_and_reply("AT+CMGF=1", "OK\r\n");
    expect_and_reply("AT+CNMI=2,1,0,0,0", "OK\r\n");
    expect_and_reply("AT+QIACT=1", "ERROR\r\n"); /* no APN/no network data plan - not fatal, see module_process()'s own comment */

    TEST_ASSERT_EQUAL_INT_MESSAGE(EC200U_MODULE_READY, EC200U_GetModuleState(),
        "SMS/Call/GNSS must still work without a working PDP context");
}

void test_network_status_is_refreshed_periodically_once_ready(void) {
    boot_module();

    advance_ms(EC200U_STATUS_POLL_INTERVAL_MS);
    expect_and_reply("AT+CREG?", "+CREG: 0,0\r\nOK\r\n"); /* lost registration */

    TEST_ASSERT_EQUAL_INT(EC200U_NETWORK_LOST, EC200U_GetNetworkState());
}

/* ==========================================================================
 * GNSS
 * ========================================================================== */

void test_gnss_start_rejected_before_module_ready(void) {
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_GNSS_Start());
}

void test_gnss_get_latest_not_ready_before_module_ready(void) {
    EC200U_GnssData data;
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_GNSS_GetLatest(&data));
    TEST_ASSERT_FALSE(data.fix_valid);
}

void test_gnss_no_fix_immediately_after_start(void) {
    EC200U_GnssData data;
    boot_module();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_GNSS_Start());
    expect_and_reply("AT+QGPS=1", "OK\r\n");

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_GNSS_GetLatest(&data));
    TEST_ASSERT_FALSE_MESSAGE(data.fix_valid, "no AT+QGPSLOC has been polled yet");
}

void test_gnss_fix_available_after_a_successful_qgpsloc(void) {
    EC200U_GnssData data;
    boot_module();
    EC200U_GNSS_Start();
    expect_and_reply("AT+QGPS=1", "OK\r\n");

    advance_ms(EC200U_GNSS_POLL_INTERVAL_MS);
    expect_and_reply("AT+QGPSLOC=2", "+QGPSLOC: 101530.0,35.700000,51.400000,1.0,1200.0,2,0.0,0.0,0.0,200926,08\r\nOK\r\n");

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_GNSS_GetLatest(&data));
    TEST_ASSERT_TRUE(data.fix_valid);
    TEST_ASSERT_EQUAL_INT32(35700000, data.latitude);
    TEST_ASSERT_EQUAL_INT32(51400000, data.longitude);
}

void test_gnss_fix_lost_when_qgpsloc_reports_not_fixed(void) {
    EC200U_GnssData data;
    boot_module();
    EC200U_GNSS_Start();
    expect_and_reply("AT+QGPS=1", "OK\r\n");
    advance_ms(EC200U_GNSS_POLL_INTERVAL_MS);
    expect_and_reply("AT+QGPSLOC=2", "+QGPSLOC: 101530.0,35.700000,51.400000,1.0,1200.0,2,0.0,0.0,0.0,200926,08\r\nOK\r\n");
    EC200U_GNSS_GetLatest(&data);
    TEST_ASSERT_TRUE(data.fix_valid);

    advance_ms(EC200U_GNSS_POLL_INTERVAL_MS);
    expect_and_reply("AT+QGPSLOC=2", "+CME ERROR: 516\r\n");

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_GNSS_GetLatest(&data));
    TEST_ASSERT_FALSE_MESSAGE(data.fix_valid, "\"not fixed yet\" must clear the previous fix, not leave it stale");
}

void test_gnss_stop_clears_the_fix(void) {
    EC200U_GnssData data;
    boot_module();
    EC200U_GNSS_Start();
    expect_and_reply("AT+QGPS=1", "OK\r\n");
    advance_ms(EC200U_GNSS_POLL_INTERVAL_MS);
    expect_and_reply("AT+QGPSLOC=2", "+QGPSLOC: 101530.0,35.700000,51.400000,1.0,1200.0,2,0.0,0.0,0.0,200926,08\r\nOK\r\n");
    EC200U_GNSS_GetLatest(&data);
    TEST_ASSERT_TRUE(data.fix_valid);

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_GNSS_Stop());
    expect_and_reply("AT+QGPSEND", "OK\r\n");

    EC200U_GNSS_GetLatest(&data);
    TEST_ASSERT_FALSE(data.fix_valid);
}

/* ==========================================================================
 * SMS - Send
 * ========================================================================== */

static EC200U_SmsData make_sms(const char* destination, const char* payload) {
    EC200U_SmsData sms;
    sms.destination     = destination;
    sms.payload          = payload;
    sms.payload_length   = (uint32_t)strlen(payload);
    return sms;
}

void test_sms_send_rejected_before_module_ready(void) {
    EC200U_SmsData sms = make_sms("+15550001", "hello");
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_SMS_Send(&sms));
}

void test_sms_send_full_handshake_reports_ok(void) {
    EC200U_SmsData sms = make_sms("+15550001", "hello");
    boot_module();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_SMS_Send(&sms));
    TEST_ASSERT_EQUAL_INT_MESSAGE(EC200U_BUSY, EC200U_SMS_GetSendStatus(), "accepted, not yet confirmed");

    EC200U_Process();
    TEST_ASSERT_EQUAL_STRING("AT+CMGS=\"+15550001\"", s_last_sent);

    queue_reply("> ");
    EC200U_Process(); /* consumes the prompt, sends body + Ctrl-Z */
    queue_reply("+CMGS: 12\r\nOK\r\n");
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_SMS_GetSendStatus());
}

void test_sms_send_reports_error_when_module_rejects_it(void) {
    EC200U_SmsData sms = make_sms("+15550001", "hello");
    boot_module();
    EC200U_SMS_Send(&sms);

    expect_and_reply("AT+CMGS=\"+15550001\"", "+CMS ERROR: 21\r\n"); /* invalid destination address - rejected before ever prompting */

    TEST_ASSERT_EQUAL_INT(EC200U_ERROR, EC200U_SMS_GetSendStatus());
}

void test_sms_send_busy_while_a_send_is_already_in_flight(void) {
    EC200U_SmsData first  = make_sms("+15550001", "hello");
    EC200U_SmsData second = make_sms("+15550002", "world");
    boot_module();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_SMS_Send(&first));
    TEST_ASSERT_EQUAL_INT(EC200U_BUSY, EC200U_SMS_Send(&second));
}

/* ==========================================================================
 * SMS - Receive (URC-triggered)
 * ========================================================================== */

void test_sms_get_received_not_ready_when_nothing_pending(void) {
    EC200U_SmsData sms;
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_SMS_GetReceived(&sms));
}

void test_incoming_sms_urc_triggers_read_and_delete(void) {
    EC200U_SmsData sms;
    boot_module();

    queue_reply("+CMTI: \"SM\",3\r\n");
    EC200U_Process(); /* dispatches the URC, then immediately notices the pending index and sends AT+CMGR */
    TEST_ASSERT_EQUAL_STRING("AT+CMGR=3", s_last_sent);

    queue_reply("+CMGR: \"REC UNREAD\",\"+15551234\",,\"26/09/20,10:00:00+14\"\r\nLOC\r\nOK\r\n");
    EC200U_Process(); /* resolves CMGR, immediately issues AT+CMGD */
    TEST_ASSERT_EQUAL_STRING("AT+CMGD=3", s_last_sent);

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_SMS_GetReceived(&sms));
    TEST_ASSERT_EQUAL_STRING("+15551234", sms.destination);
    TEST_ASSERT_EQUAL_STRING("LOC", sms.payload);

    queue_reply("OK\r\n");
    EC200U_Process(); /* resolves CMGD */
}

/* ==========================================================================
 * Voice Call
 * ========================================================================== */

void test_call_dial_rejected_before_module_ready(void) {
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_Call_Dial("+15550001"));
}

void test_call_idle_before_any_dial_or_incoming_call(void) {
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_IDLE, EC200U_Call_GetState());
}

void test_call_dial_sends_atd_and_enters_dialing_on_ok(void) {
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_Call_Dial("+15550001"));

    expect_and_reply("ATD+15550001;", "OK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_DIALING, EC200U_Call_GetState());
}

void test_call_dial_rejected_busy_while_a_call_is_already_in_progress(void) {
    boot_module();
    EC200U_Call_Dial("+15550001");
    TEST_ASSERT_EQUAL_INT(EC200U_BUSY, EC200U_Call_Dial("+15550002"));
}

void test_call_progresses_to_active_via_clcc_poll(void) {
    boot_module();
    EC200U_Call_Dial("+15550001");
    expect_and_reply("ATD+15550001;", "OK\r\n");

    advance_ms(EC200U_CALL_POLL_INTERVAL_MS);
    expect_and_reply("AT+CLCC", "+CLCC: 1,0,3,0,0,\"+15550001\",145\r\nOK\r\n"); /* stat=3 alerting */
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_RINGING, EC200U_Call_GetState());

    advance_ms(EC200U_CALL_POLL_INTERVAL_MS);
    expect_and_reply("AT+CLCC", "+CLCC: 1,0,0,0,0,\"+15550001\",145\r\nOK\r\n"); /* stat=0 active */
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_ACTIVE, EC200U_Call_GetState());
}

void test_call_ends_when_clcc_reports_no_call(void) {
    boot_module();
    EC200U_Call_Dial("+15550001");
    expect_and_reply("ATD+15550001;", "OK\r\n");

    advance_ms(EC200U_CALL_POLL_INTERVAL_MS);
    expect_and_reply("AT+CLCC", "OK\r\n"); /* no +CLCC line at all - nothing in progress any more */
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_ENDED, EC200U_Call_GetState());
}

void test_call_hangup_sends_ath(void) {
    boot_module();
    EC200U_Call_Dial("+15550001");
    expect_and_reply("ATD+15550001;", "OK\r\n");

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_Call_Hangup());
    expect_and_reply("ATH", "OK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_ENDED, EC200U_Call_GetState());
}

void test_incoming_call_detected_via_ring_and_clip(void) {
    boot_module();

    queue_reply("RING\r\n+CLIP: \"+15559999\",145\r\n");
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_CALL_RINGING, EC200U_Call_GetState());

    char buffer[32];
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_Call_GetIncomingNumber(buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("+15559999", buffer);
}

void test_call_answer_sends_ata(void) {
    boot_module();
    queue_reply("RING\r\n+CLIP: \"+15559999\",145\r\n");
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_Call_Answer());
    expect_and_reply("ATA", "OK\r\n");
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_ACTIVE, EC200U_Call_GetState());
}

void test_no_carrier_urc_ends_an_active_call(void) {
    boot_module();
    queue_reply("RING\r\n+CLIP: \"+15559999\",145\r\n");
    EC200U_Process();
    EC200U_Call_Answer();
    expect_and_reply("ATA", "OK\r\n");

    queue_reply("NO CARRIER\r\n");
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT(EC200U_CALL_ENDED, EC200U_Call_GetState());
}

/* ==========================================================================
 * MQTT
 * ========================================================================== */

void test_mqtt_connect_rejected_before_module_ready(void) {
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_MQTT_Connect("tcu-001", "sandbox.example.com", 9095u));
}

void test_mqtt_connect_full_handshake_reaches_connected(void) {
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_MQTT_Connect("tcu-001", "sandbox.example.com", 9095u));
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_CONNECTING, EC200U_MQTT_GetState());

    EC200U_Process();
    TEST_ASSERT_EQUAL_STRING("AT+QMTOPEN=0,\"sandbox.example.com\",9095", s_last_sent);
    queue_reply("OK\r\n");
    EC200U_Process(); /* local accept only */
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_CONNECTING, EC200U_MQTT_GetState());

    queue_reply("+QMTOPEN: 0,0\r\n");
    EC200U_Process(); /* URC: socket opened -> immediately proceeds to QMTCONN */
    TEST_ASSERT_EQUAL_STRING("AT+QMTCONN=0,\"tcu-001\"", s_last_sent);
    queue_reply("OK\r\n");
    EC200U_Process();

    queue_reply("+QMTCONN: 0,0,0\r\n");
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_CONNECTED, EC200U_MQTT_GetState());
}

void test_mqtt_open_urc_failure_leaves_disconnected(void) {
    boot_module();
    EC200U_MQTT_Connect("tcu-001", "sandbox.example.com", 9095u);
    expect_and_reply("AT+QMTOPEN=0,\"sandbox.example.com\",9095", "OK\r\n");

    queue_reply("+QMTOPEN: 0,1\r\n"); /* 1 = wrong parameter/already opened/etc - not success */
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_DISCONNECTED, EC200U_MQTT_GetState());
}

void test_mqtt_disconnect_is_immediate(void) {
    boot_module();
    EC200U_MQTT_Connect("tcu-001", "sandbox.example.com", 9095u);
    expect_and_reply("AT+QMTOPEN=0,\"sandbox.example.com\",9095", "OK\r\n");
    queue_reply("+QMTOPEN: 0,0\r\n");
    EC200U_Process();
    expect_and_reply("AT+QMTCONN=0,\"tcu-001\"", "OK\r\n");
    queue_reply("+QMTCONN: 0,0,0\r\n");
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_CONNECTED, EC200U_MQTT_GetState());

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_MQTT_Disconnect());
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_DISCONNECTED, EC200U_MQTT_GetState());
}

static void connect_mqtt(void) {
    boot_module();
    EC200U_MQTT_Connect("tcu-001", "sandbox.example.com", 9095u);
    expect_and_reply("AT+QMTOPEN=0,\"sandbox.example.com\",9095", "OK\r\n");
    queue_reply("+QMTOPEN: 0,0\r\n");
    EC200U_Process();
    expect_and_reply("AT+QMTCONN=0,\"tcu-001\"", "OK\r\n");
    queue_reply("+QMTCONN: 0,0,0\r\n");
    EC200U_Process();
}

void test_mqtt_publish_not_ready_while_disconnected(void) {
    const uint8_t payload[] = { 1 };
    EC200U_MqttMessage message = { "tcu/001/telemetry", payload, sizeof(payload) };
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_MQTT_Publish(&message));
}

void test_mqtt_publish_sends_topic_then_payload_after_prompt(void) {
    const uint8_t payload[] = { 0xAA, 0xBB, 0xCC };
    EC200U_MqttMessage message = { "tcu/001/telemetry", payload, sizeof(payload) };
    connect_mqtt();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_MQTT_Publish(&message));

    EC200U_Process();
    TEST_ASSERT_EQUAL_STRING("AT+QMTPUB=0,1,0,0,\"tcu/001/telemetry\"", s_last_sent);

    queue_reply("> ");
    EC200U_Process(); /* consumes prompt, sends raw payload + Ctrl-Z */

    queue_reply("+QMTPUB: 0,1,0\r\nOK\r\n");
    EC200U_Process(); /* resolves - fire-and-forget, no separate getter (see EC200U_MQTT_Publish()'s own comment) */
}

void test_mqtt_subscribe_sends_qmtsub(void) {
    connect_mqtt();
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_MQTT_Subscribe("tcu/001/cmd"));
    expect_and_reply("AT+QMTSUB=0,1,\"tcu/001/cmd\",1", "OK\r\n");
    queue_reply("+QMTSUB: 0,1,0,1\r\n");
    EC200U_Process();
}

void test_mqtt_get_received_reports_a_qmtrecv_urc(void) {
    EC200U_MqttMessage message;
    connect_mqtt();

    queue_reply("+QMTRECV: 0,0,\"tcu/001/cmd\",\"hello\"\r\n");
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_MQTT_GetReceived(&message));
    TEST_ASSERT_EQUAL_STRING("tcu/001/cmd", message.topic);
    TEST_ASSERT_EQUAL_UINT32(5u, message.payload_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hello", message.payload, 5u);
}

void test_mqtt_get_received_not_ready_when_nothing_pending(void) {
    EC200U_MqttMessage message;
    connect_mqtt();
    TEST_ASSERT_EQUAL_INT(EC200U_NOT_READY, EC200U_MQTT_GetReceived(&message));
}

void test_qmtstat_urc_disconnects(void) {
    connect_mqtt();
    queue_reply("+QMTSTAT: 0,1\r\n");
    EC200U_Process();
    TEST_ASSERT_EQUAL_INT(EC200U_MQTT_DISCONNECTED, EC200U_MQTT_GetState());
}

/* ==========================================================================
 * HTTP
 * ========================================================================== */

void test_http_start_get_full_handshake_completes(void) {
    boot_module();
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_HTTP_StartGet("http://example.com/fw.bin"));
    TEST_ASSERT_EQUAL_INT(EC200U_HTTP_IN_PROGRESS, EC200U_HTTP_GetState());

    EC200U_Process();
    TEST_ASSERT_EQUAL_STRING("AT+QHTTPURL=25,80", s_last_sent);
    queue_reply("CONNECT\r\n");
    EC200U_Process(); /* sends the raw URL bytes */
    queue_reply("OK\r\n");
    EC200U_Process();

    expect_and_reply("AT+QHTTPGET=80", "OK\r\n");
    queue_reply("+QHTTPGET: 0,200,11\r\n");
    EC200U_Process();

    expect_and_reply("AT+QHTTPREAD=80", "hello world\r\n+QHTTPREAD: 0\r\nOK\r\n");
    EC200U_Process(); /* one more small step: SENDING_READ notices the content is now fully read and moves to COMPLETE */

    TEST_ASSERT_EQUAL_INT(EC200U_HTTP_COMPLETE, EC200U_HTTP_GetState());

    uint8_t  buffer[64];
    uint32_t received_length;
    TEST_ASSERT_EQUAL_INT(EC200U_OK, EC200U_HTTP_GetChunk(buffer, sizeof(buffer), &received_length));
    TEST_ASSERT_EQUAL_UINT32(11u, received_length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hello world", buffer, 11u);

    TEST_ASSERT_EQUAL_INT_MESSAGE(EC200U_NOT_READY, EC200U_HTTP_GetChunk(buffer, sizeof(buffer), &received_length),
        "fully drained - nothing left to pop");
}

void test_http_start_get_fails_on_non_200_response(void) {
    boot_module();
    EC200U_HTTP_StartGet("http://example.com/fw.bin");
    expect_and_reply("AT+QHTTPURL=25,80", "CONNECT\r\n");
    EC200U_Process();
    queue_reply("OK\r\n");
    EC200U_Process();
    expect_and_reply("AT+QHTTPGET=80", "OK\r\n");

    queue_reply("+QHTTPGET: 0,404,0\r\n");
    EC200U_Process();

    TEST_ASSERT_EQUAL_INT(EC200U_HTTP_ERROR, EC200U_HTTP_GetState());
}

void test_http_start_get_busy_while_one_already_in_progress(void) {
    boot_module();
    EC200U_HTTP_StartGet("http://example.com/a.bin");
    TEST_ASSERT_EQUAL_INT(EC200U_BUSY, EC200U_HTTP_StartGet("http://example.com/b.bin"));
}
