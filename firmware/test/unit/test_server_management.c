#include "unity.h"
#include "managements/server_management.h"
#include "managements/server_management_config.h"
#include "infrastructure/configuration.h"
#include "shared/command_router.h"
#include "Mockec200u_manager.h"
#include "Mockpower_management.h"
#include "Mockreset_request.h"
#include "Mockconfiguration.h"
#include "Mockdiagnostic_management.h"
#include "Mockfirmware_update_management.h"
#include "Mockactuator_management.h"
#include "infrastructure/time_base.h"
#include "shared/event.h"
#include "shared/queues.h"
#include "shared/log_record.h"
#include <string.h>
#include <stdio.h>

TEST_SOURCE_FILE("infrastructure/time_base.c")
TEST_SOURCE_FILE("infrastructure/queue.c")
TEST_SOURCE_FILE("shared/queues.c")
TEST_SOURCE_FILE("shared/command_router.c")
TEST_SOURCE_FILE("shared/command_safety_policy.c")

/* INBOUND path (server_management.c's handle_incoming_message()/
 * poll_diagnostic_response()) additionally depends on shared/command_router.c
 * (real, linked above - it is a light Component without an FSM, same
 * status as shared/event_policy.c) and, through it, Diagnostic Management's
 * Request/Response API - Mocked here (Mockdiagnostic_management.h) rather
 * than linking the real diagnostic_management.c, same reasoning
 * test_call_management.c already documents for its own Configuration
 * dependency: this test isolates Server Management's own behavior, not
 * Diagnostic Management's CAN/UDS state machine. */

/*
 * Server Management's real dependencies (EC200U Manager, Power Mode,
 * Reset Request) are mocked here, same pattern as test_gps_management.c -
 * this test is isolated from the on-board EC200U simulator and only ever
 * sees the Capability Contracts. Every mocked call uses
 * *_StubWithCallback so Server_Process()'s internal branching (connection
 * gating including the MQTT Session layer, three-way queue priority,
 * Power-Mode throttling) can be driven by simple static "scenario"
 * variables.
 *
 * Since docs/02-management-contracts/backend-contract-tcu-hekar.md was
 * adopted, Server Management no longer calls EC200U_Data_Send() directly -
 * every outbound item is wrapped in a Message Envelope, Binary-encoded
 * (docs/02-management-contracts/binary-protocol-final.md sections 2/7:
 * 8-byte Header + raw payload + CRC16-CCITT) and published over the EC200U
 * MQTT Capability (EC200U_MQTT_Connect/GetState/Publish). These tests mock
 * that Capability instead, and additionally decode the published Binary
 * frame to check the Envelope itself, not just "something was sent".
 */

static EC200U_ModuleState  s_stub_module_state;
static EC200U_NetworkState s_stub_network_state;
static PowerMode           s_stub_power_mode;

static EC200U_MqttState s_stub_mqtt_state;
static EC200U_Result    s_stub_mqtt_connect_result;
static EC200U_Result    s_stub_mqtt_publish_result;

static int      s_mqtt_connect_call_count;
static char     s_last_mqtt_connect_client_id[64];
static char     s_last_mqtt_connect_host[64];
static uint16_t s_last_mqtt_connect_port;

/* Controls Configuration_Get()'s stub - same "test owns a static fixture,
 * stub just returns a pointer to it" pattern test_call_management.c uses
 * for its own Configuration dependency. */
static DeviceConfig s_fixture_config;

static const DeviceConfig* stub_configuration_get(int cmock_num_calls) {
    (void)cmock_num_calls;
    return &s_fixture_config;
}

static int      s_data_send_call_count; /* counts EC200U_MQTT_Publish calls */
static uint8_t  s_last_sent_payload[SERVER_BINARY_FRAME_MAX_BYTES];
static uint32_t s_last_sent_length;
static char     s_last_sent_topic[64];

static int          s_reset_request_count;
static ResetRequest  s_last_reset_request;

static EC200U_ModuleState stub_get_module_state(int cmock_num_calls) { (void)cmock_num_calls; return s_stub_module_state; }
static EC200U_NetworkState stub_get_network_state(int cmock_num_calls) { (void)cmock_num_calls; return s_stub_network_state; }

static EC200U_Result stub_mqtt_connect(const char* client_id, const char* host, uint16_t port, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_mqtt_connect_call_count++;
    if (client_id) {
        strncpy(s_last_mqtt_connect_client_id, client_id, sizeof(s_last_mqtt_connect_client_id) - 1);
        s_last_mqtt_connect_client_id[sizeof(s_last_mqtt_connect_client_id) - 1] = '\0';
    }
    if (host) {
        strncpy(s_last_mqtt_connect_host, host, sizeof(s_last_mqtt_connect_host) - 1);
        s_last_mqtt_connect_host[sizeof(s_last_mqtt_connect_host) - 1] = '\0';
    }
    s_last_mqtt_connect_port = port;
    return s_stub_mqtt_connect_result;
}

static EC200U_MqttState stub_mqtt_get_state(int cmock_num_calls) { (void)cmock_num_calls; return s_stub_mqtt_state; }

static EC200U_Result stub_mqtt_publish(const EC200U_MqttMessage* message, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_data_send_call_count++;
    if (message) {
        s_last_sent_length = message->payload_length;
        if (message->payload_length > 0 && message->payload_length <= sizeof(s_last_sent_payload)) {
            memcpy(s_last_sent_payload, message->payload, message->payload_length);
        }
        if (message->topic) {
            strncpy(s_last_sent_topic, message->topic, sizeof(s_last_sent_topic) - 1);
            s_last_sent_topic[sizeof(s_last_sent_topic) - 1] = '\0';
        }
    }
    return s_stub_mqtt_publish_result;
}

static PowerMode stub_power_get_mode(int cmock_num_calls) { (void)cmock_num_calls; return s_stub_power_mode; }

static void stub_submit_reset_request(const ResetRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_reset_request_count++;
    if (request) s_last_reset_request = *request;
}

/* --- Inbound Command handling fixtures (EC200U_MQTT_Subscribe/GetReceived,
 * Diagnostic_SubmitRequest/GetResponse) - see server_management.c's file
 * header "INBOUND path" section. */
static int  s_mqtt_subscribe_call_count;
static bool s_mqtt_received_pending;   /* test owns exactly one queued inbound message, popped once */
static char s_mqtt_received_topic[64];
static uint8_t s_mqtt_received_payload[SERVER_BINARY_FRAME_MAX_BYTES];
static uint32_t s_mqtt_received_payload_length;

static EC200U_Result stub_mqtt_subscribe(const char* topic, int cmock_num_calls) {
    (void)topic;
    (void)cmock_num_calls;
    s_mqtt_subscribe_call_count++;
    return EC200U_OK;
}

static EC200U_Result stub_mqtt_get_received(EC200U_MqttMessage* message, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (!s_mqtt_received_pending) {
        return EC200U_NOT_READY;
    }
    message->topic          = s_mqtt_received_topic;
    message->payload        = s_mqtt_received_payload;
    message->payload_length = s_mqtt_received_payload_length;
    s_mqtt_received_pending = false; /* Pop, not Peek - same as the real EC200U Manager */
    return EC200U_OK;
}

static int                    s_diagnostic_submit_call_count;
static DiagnosticRequest      s_last_diagnostic_request;
static DiagnosticSubmitResult s_stub_diagnostic_submit_result;

static DiagnosticSubmitResult stub_diagnostic_submit_request(const DiagnosticRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_diagnostic_submit_call_count++;
    if (request) s_last_diagnostic_request = *request;
    return s_stub_diagnostic_submit_result;
}

static bool               s_stub_diagnostic_response_ready;
static DiagnosticResponse s_stub_diagnostic_response;

static bool stub_diagnostic_get_response(DiagnosticResponse* response, int cmock_num_calls) {
    (void)cmock_num_calls;
    if (!s_stub_diagnostic_response_ready) {
        return false;
    }
    *response = s_stub_diagnostic_response;
    return true;
}

/* --- Firmware Download fixtures (handle_incoming_message()'s own
 * COMMAND_DOMAIN_FIRMWARE_DOWNLOAD wire decode, and
 * CommandRouter_SubmitFirmwareDownload() - see command_router.c). The
 * actual file transfer no longer touches this MQTT channel at all now
 * that FOTA-over-HTTP was confirmed (OPEN-ITEMS.md TCU<->Backend #12) -
 * FirmwareUpdate_SubmitRequest() itself drives EC200U's HTTP Client
 * Capability, entirely inside firmware_update_management.c
 * (test_firmware_update_management.c's job, not this file's). --- */
static bool     s_stub_firmware_submit_result;
static int      s_firmware_submit_call_count;
static uint32_t s_last_firmware_expected_size;
static char     s_last_firmware_url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];

static bool stub_firmware_submit_request(uint32_t expected_size, const char* url, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_firmware_submit_call_count++;
    s_last_firmware_expected_size = expected_size;
    if (url) strncpy(s_last_firmware_url, url, sizeof(s_last_firmware_url) - 1u);
    return s_stub_firmware_submit_result;
}

static int             s_actuator_submit_call_count;
static ActuatorRequest s_last_actuator_request;
static bool             s_stub_actuator_submit_result;

static bool stub_actuator_submit_request(const ActuatorRequest* request, int cmock_num_calls) {
    (void)cmock_num_calls;
    s_actuator_submit_call_count++;
    if (request) s_last_actuator_request = *request;
    return s_stub_actuator_submit_result;
}

/* Mirrors server_management.c's own crc16_ccitt()/binary_encoder_encode()
 * framing exactly (docs/02-management-contracts/binary-protocol-final.md
 * sections 2/7) - needed here to construct well-formed inbound Command
 * frames these tests inject, symmetric to how parse_binary_frame() below
 * reads back an outbound one. Sequence Id/Timestamp are arbitrary fixed
 * values - no test below asserts on the inbound frame's own header fields. */
#define TEST_WIRE_MSG_TYPE_TELEMETRY 0x01u
#define TEST_WIRE_MSG_TYPE_EVENT     0x02u
#define TEST_WIRE_MSG_TYPE_COMMAND   0x10u
#define TEST_WIRE_MSG_TYPE_RESPONSE  0x11u

static uint16_t test_crc16_ccitt(const uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFFu;
    uint32_t i;
    int      bit;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t build_binary_frame(uint8_t version, uint8_t msg_type, const uint8_t* payload, uint32_t payload_len, uint8_t* out) {
    uint32_t header_and_payload_len = 8u + payload_len;
    uint16_t crc;

    out[0] = version;
    out[1] = msg_type;
    out[2] = 0x00u; out[3] = 0x01u; /* Sequence Id - arbitrary */
    out[4] = 0x00u; out[5] = 0x00u; out[6] = 0x00u; out[7] = 0x01u; /* Timestamp - arbitrary */
    if (payload_len > 0u) {
        memcpy(out + 8, payload, payload_len);
    }
    crc = test_crc16_ccitt(out, header_and_payload_len);
    out[header_and_payload_len]      = (uint8_t)((crc >> 8) & 0xFFu);
    out[header_and_payload_len + 1u] = (uint8_t)(crc & 0xFFu);
    return header_and_payload_len + 2u;
}

/* Parses an outbound frame this Management just published: fails the
 * calling test via TEST_ASSERT if it is not at least Header+CRC long, its
 * Version byte is wrong, or its CRC does not match - every outbound-
 * inspecting test below needs this to already hold before checking
 * anything else. Hands back the Message Type byte and a pointer to the raw
 * payload inside `frame` itself (no copy). */
static void parse_binary_frame(const uint8_t* frame, uint32_t frame_len, uint8_t* out_msg_type, const uint8_t** out_payload, uint32_t* out_payload_len) {
    uint32_t header_and_payload_len;
    uint16_t computed_crc;
    uint16_t received_crc;

    TEST_ASSERT_TRUE_MESSAGE(frame_len >= 10u, "frame must at least hold Header(8)+CRC(2)");
    TEST_ASSERT_EQUAL_MESSAGE(SERVER_PROTOCOL_VERSION, frame[0], "Version byte must be SERVER_PROTOCOL_VERSION");

    header_and_payload_len = frame_len - 2u;
    computed_crc = test_crc16_ccitt(frame, header_and_payload_len);
    received_crc = (uint16_t)(((uint16_t)frame[header_and_payload_len] << 8) | frame[header_and_payload_len + 1u]);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(computed_crc, received_crc, "CRC16-CCITT must match");

    *out_msg_type   = frame[1];
    *out_payload     = frame + 8u;
    *out_payload_len = header_and_payload_len - 8u;
}

/* Builds a well-formed inbound Command frame carrying one Diagnostic wire
 * payload, and queues it for the next EC200U_MQTT_GetReceived() call. */
static void queue_incoming_diagnostic_command(uint32_t operation, uint32_t target, uint32_t parameter) {
    uint32_t wire[4]; /* domain, operation, target, parameter - matches server_management.c's CommandWirePayload layout */

    wire[0] = (uint32_t)COMMAND_DOMAIN_DIAGNOSTIC;
    wire[1] = operation;
    wire[2] = target;
    wire[3] = parameter;

    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)wire, sizeof(wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;
}

static void queue_incoming_door_command(uint32_t target, uint32_t parameter) {
    uint32_t wire[4];

    wire[0] = (uint32_t)COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST;
    wire[1] = 0u; /* operation unused for this domain - see command_router.c */
    wire[2] = target;
    wire[3] = parameter;

    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)wire, sizeof(wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;
}

static void queue_incoming_door_gpio_command(uint32_t parameter) {
    uint32_t wire[4];

    wire[0] = (uint32_t)COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO;
    wire[1] = 0u; /* operation unused for this domain */
    wire[2] = 0u; /* target unused for this domain */
    wire[3] = parameter;

    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)wire, sizeof(wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;
}

/* Mirrors server_management.c's own (private) FirmwareDownloadWirePayload
 * byte-for-byte - COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's dedicated wire shape,
 * added once FOTA-over-HTTP was confirmed (OPEN-ITEMS.md TCU<->Backend
 * #12): the generic 4x-uint32_t CommandWirePayload has no room for a URL. */
typedef struct {
    uint32_t domain;
    uint32_t expected_size;
    char     url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];
} TestFirmwareDownloadWirePayload;

static void queue_incoming_firmware_download_command(uint32_t expected_size, const char* url) {
    TestFirmwareDownloadWirePayload wire;
    memset(&wire, 0, sizeof(wire));
    wire.domain        = (uint32_t)COMMAND_DOMAIN_FIRMWARE_DOWNLOAD;
    wire.expected_size = expected_size;
    strncpy(wire.url, url, sizeof(wire.url) - 1u);

    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)&wire, sizeof(wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;
}

static void advance_ms(uint32_t ms) {
    for (uint32_t i = 0; i < ms; i++) Time_TickIncrement();
}

/* Cellular network ready AND the MQTT Session already CONNECTED - used by
 * every test below that only cares about drain/throttle/fault behavior
 * once Server_GetConnectionState() == SERVER_CONNECTED, not about the
 * Connect handshake itself (covered separately by
 * test_mqtt_session_is_connected_after_network_becomes_ready below). */
static void go_connected(void) {
    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_REGISTERED;
    s_stub_mqtt_state    = EC200U_MQTT_CONNECTED;
}

void setUp(void) {
    Time_Init();
    Queues_Init();
    Server_Init();

    s_stub_module_state     = EC200U_MODULE_OFF;
    s_stub_network_state    = EC200U_NETWORK_UNKNOWN;
    s_stub_power_mode       = POWER_NORMAL;

    s_stub_mqtt_state           = EC200U_MQTT_DISCONNECTED;
    s_stub_mqtt_connect_result  = EC200U_OK;
    s_stub_mqtt_publish_result  = EC200U_OK;

    s_mqtt_connect_call_count = 0;
    memset(s_last_mqtt_connect_client_id, 0, sizeof(s_last_mqtt_connect_client_id));
    memset(s_last_mqtt_connect_host, 0, sizeof(s_last_mqtt_connect_host));
    s_last_mqtt_connect_port = 0;

    /* Fixture Sandbox Host/Port - mqtt_transport_connect() (server_management.c)
     * reads exactly these two fields via Configuration_Get(). */
    memset(&s_fixture_config, 0, sizeof(s_fixture_config));
    strncpy(s_fixture_config.communication.server_address_sandbox, "fixture.example.com",
            sizeof(s_fixture_config.communication.server_address_sandbox) - 1);
    s_fixture_config.communication.server_port_sandbox = 9095u;

    s_data_send_call_count = 0;
    s_last_sent_length     = 0;
    memset(s_last_sent_payload, 0, sizeof(s_last_sent_payload));
    memset(s_last_sent_topic, 0, sizeof(s_last_sent_topic));

    s_reset_request_count = 0;

    s_mqtt_subscribe_call_count = 0;
    s_mqtt_received_pending     = false;
    memset(s_mqtt_received_topic, 0, sizeof(s_mqtt_received_topic));
    memset(s_mqtt_received_payload, 0, sizeof(s_mqtt_received_payload));
    s_mqtt_received_payload_length = 0;

    s_diagnostic_submit_call_count  = 0;
    s_stub_diagnostic_submit_result = DIAG_SUBMIT_ACCEPTED;
    memset(&s_last_diagnostic_request, 0, sizeof(s_last_diagnostic_request));

    s_stub_diagnostic_response_ready = false;
    memset(&s_stub_diagnostic_response, 0, sizeof(s_stub_diagnostic_response));

    EC200U_GetModuleState_StubWithCallback(stub_get_module_state);
    EC200U_GetNetworkState_StubWithCallback(stub_get_network_state);
    EC200U_MQTT_Connect_StubWithCallback(stub_mqtt_connect);
    EC200U_MQTT_GetState_StubWithCallback(stub_mqtt_get_state);
    EC200U_MQTT_Publish_StubWithCallback(stub_mqtt_publish);
    EC200U_MQTT_Subscribe_StubWithCallback(stub_mqtt_subscribe);
    EC200U_MQTT_GetReceived_StubWithCallback(stub_mqtt_get_received);
    Power_GetMode_StubWithCallback(stub_power_get_mode);
    System_SubmitResetRequest_StubWithCallback(stub_submit_reset_request);
    Configuration_Get_StubWithCallback(stub_configuration_get);
    Diagnostic_SubmitRequest_StubWithCallback(stub_diagnostic_submit_request);
    Diagnostic_GetResponse_StubWithCallback(stub_diagnostic_get_response);

    s_stub_firmware_submit_result = true;
    s_firmware_submit_call_count  = 0;
    s_last_firmware_expected_size = 0;
    memset(s_last_firmware_url, 0, sizeof(s_last_firmware_url));
    FirmwareUpdate_SubmitRequest_StubWithCallback(stub_firmware_submit_request);

    s_actuator_submit_call_count  = 0;
    memset(&s_last_actuator_request, 0, sizeof(s_last_actuator_request));
    s_stub_actuator_submit_result = true;
    Actuator_SubmitRequest_StubWithCallback(stub_actuator_submit_request);
}

void tearDown(void) {}

/* --- Connection State --- */

void test_connection_state_starts_disconnected(void) {
    TEST_ASSERT_EQUAL_INT(SERVER_DISCONNECTED, Server_GetConnectionState());
}

void test_connection_state_tracks_module_and_network(void) {
    s_stub_module_state  = EC200U_MODULE_INITIALIZING;
    s_stub_network_state = EC200U_NETWORK_SEARCHING;
    Server_Process();
    TEST_ASSERT_EQUAL_INT(SERVER_CONNECTING, Server_GetConnectionState());

    go_connected();
    Server_Process();
    TEST_ASSERT_EQUAL_INT(SERVER_CONNECTED, Server_GetConnectionState());
}

/* --- MQTT Session layering (backend-contract-tcu-hekar.md section 3) --- */

void test_mqtt_connect_is_not_attempted_while_cellular_network_not_ready(void) {
    s_stub_module_state  = EC200U_MODULE_INITIALIZING;
    s_stub_network_state = EC200U_NETWORK_SEARCHING;
    Server_Process();
    TEST_ASSERT_EQUAL_INT(0, s_mqtt_connect_call_count);
}

void test_connection_state_stays_connecting_until_mqtt_session_is_connected(void) {
    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_REGISTERED;
    s_stub_mqtt_state     = EC200U_MQTT_DISCONNECTED;

    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(SERVER_CONNECTING, Server_GetConnectionState(),
        "Cellular network alone must not be reported as CONNECTED - the MQTT Session on top of it must also be up");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_mqtt_connect_call_count,
        "Server_Process() must call EC200U_MQTT_Connect() once the cellular network is ready");
    TEST_ASSERT_EQUAL_STRING(SERVER_MQTT_CLIENT_ID_PLACEHOLDER, s_last_mqtt_connect_client_id);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(s_fixture_config.communication.server_address_sandbox, s_last_mqtt_connect_host,
        "Host must come from Configuration Infrastructure (communication.server_address_sandbox), not a compile-time constant");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(s_fixture_config.communication.server_port_sandbox, s_last_mqtt_connect_port,
        "Port must come from Configuration Infrastructure (communication.server_port_sandbox), not a compile-time constant");

    s_stub_mqtt_state = EC200U_MQTT_CONNECTED; /* Session comes up asynchronously, observed on a later Process() call */
    Server_Process();
    TEST_ASSERT_EQUAL_INT(SERVER_CONNECTED, Server_GetConnectionState());
}

void test_mqtt_connect_uses_deployment_address_when_that_environment_is_active(void) {
    strncpy(s_fixture_config.communication.server_address_deployment, "deploy.fixture.example.com",
            sizeof(s_fixture_config.communication.server_address_deployment) - 1);
    s_fixture_config.communication.server_port_deployment = 5016u;
    s_fixture_config.communication.active_environment      = CONFIG_SERVER_ENV_DEPLOYMENT;

    s_stub_module_state  = EC200U_MODULE_READY;
    s_stub_network_state = EC200U_NETWORK_REGISTERED;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_mqtt_connect_call_count);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("deploy.fixture.example.com", s_last_mqtt_connect_host,
        "active_environment=DEPLOYMENT must select the Deployment address, not Sandbox");
    TEST_ASSERT_EQUAL_UINT16(5016u, s_last_mqtt_connect_port);
}

void test_mqtt_connect_not_called_again_once_session_already_connected(void) {
    go_connected();
    Server_Process();
    Server_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_mqtt_connect_call_count,
        "Connect() must not be re-issued once the Session is already CONNECTED");
}

void test_nothing_is_sent_while_disconnected(void) {
    Queue_Push(Queues_GetTelemetryQueue(), "x", 1);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(0, s_data_send_call_count);
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetTelemetryQueue())); /* item must stay queued, per Offline Behavior */
}

/* --- Draining / priority --- */

void test_telemetry_item_is_forwarded_once_connected(void) {
    uint8_t item[] = { 10, 20, 30, 40 };
    Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));

    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    TEST_ASSERT_TRUE_MESSAGE(s_last_sent_length > 0, "an Envelope, not the raw item, must be what is Published");
}

void test_telemetry_payload_roundtrips_through_binary_frame(void) {
    uint8_t        item[] = { 10, 20, 30, 40, 0xFF, 0x00 };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);

    TEST_ASSERT_EQUAL_UINT32(sizeof(item), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(item, payload, sizeof(item));
}

void test_envelope_reflects_version_and_type(void) {
    uint8_t        item[] = { 1, 2, 3 };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    /* parse_binary_frame() itself already asserts Version == SERVER_PROTOCOL_VERSION and CRC validity. */
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_MESSAGE(TEST_WIRE_MSG_TYPE_TELEMETRY, msg_type, "Message Type byte must be Telemetry (0x01)");
}

void test_server_event_is_forwarded_with_event_type(void) {
    Event          evt = { EVENT_ACC_ON, 1, 100 };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    Queue_Push(Queues_GetServerEventQueue(), &evt, sizeof(evt));
    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_MESSAGE(TEST_WIRE_MSG_TYPE_EVENT, msg_type, "Message Type byte must be Event (0x02)");
}

void test_server_event_takes_priority_over_telemetry(void) {
    uint8_t telemetry_item[] = { 1, 2, 3 };
    Event   evt = { EVENT_ACC_ON, 1, 100 };

    Queue_Push(Queues_GetTelemetryQueue(), telemetry_item, sizeof(telemetry_item));
    Queue_Push(Queues_GetServerEventQueue(), &evt, sizeof(evt));

    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetTelemetryQueue()),
        "Telemetry must wait behind the Server Event - section 9 (Event must not be starved, but Telemetry itself must not be starved of a turn either)");
}

void test_explicit_request_takes_priority_over_event_and_telemetry(void) {
    uint8_t        telemetry_item[] = { 1 };
    Event          evt = { EVENT_ACC_ON, 1, 100 };
    uint8_t        request_payload[] = { 0xAA, 0xBB };
    ServerRequest  req = { SERVER_REQUEST_SEND_DIAGNOSTIC_DATA, request_payload, sizeof(request_payload) };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    Queue_Push(Queues_GetTelemetryQueue(), telemetry_item, sizeof(telemetry_item));
    Queue_Push(Queues_GetServerEventQueue(), &evt, sizeof(evt));
    TEST_ASSERT_TRUE(Server_SubmitRequest(&req));

    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(request_payload), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(request_payload, payload, sizeof(request_payload));
}

void test_fault_data_request_is_mapped_to_event_message_type(void) {
    uint8_t        payload_in[] = { 0x01 };
    ServerRequest  req = { SERVER_REQUEST_SEND_FAULT_DATA, payload_in, sizeof(payload_in) };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    TEST_ASSERT_TRUE(Server_SubmitRequest(&req));
    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_MESSAGE(TEST_WIRE_MSG_TYPE_EVENT, msg_type, "SEND_FAULT_DATA must map to the Event wire type");
}

void test_message_id_increments_across_sends(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    uint16_t seq_id_1;
    uint16_t seq_id_2;

    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));

    go_connected();
    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    TEST_ASSERT_TRUE(s_last_sent_length >= 4u);
    seq_id_1 = (uint16_t)(((uint16_t)s_last_sent_payload[2] << 8) | s_last_sent_payload[3]);

    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);
    TEST_ASSERT_TRUE(s_last_sent_length >= 4u);
    seq_id_2 = (uint16_t)(((uint16_t)s_last_sent_payload[2] << 8) | s_last_sent_payload[3]);

    TEST_ASSERT_TRUE_MESSAGE(seq_id_1 != seq_id_2, "MessageID/SeqId must increment across sends (Trace/Duplicate Detection)");
}

void test_one_item_drained_per_process_call(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));

    go_connected();
    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    /* NORMAL Power-Mode spacing (section 18) now applies between sends -
     * see test_normal_mode_throttles_successive_sends - so this second
     * item needs the minimum interval to elapse first; that throttle is
     * covered separately, here we only care that a second call, once
     * allowed, still drains exactly one more item, not the whole queue. */
    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);
}

/* --- Server_SubmitRequest validation --- */

void test_submit_request_rejects_null(void) {
    TEST_ASSERT_FALSE(Server_SubmitRequest(NULL));
}

void test_submit_request_rejects_oversized_payload(void) {
    static uint8_t huge[SERVER_MAX_ITEM_BYTES + 1];
    ServerRequest req = { SERVER_REQUEST_SEND_FAULT_DATA, huge, sizeof(huge) };
    TEST_ASSERT_FALSE(Server_SubmitRequest(&req));
}

/* Regression guard: a request at exactly the allowed maximum
 * (SERVER_MAX_ITEM_BYTES) carries a ServerRequestType header on top of that
 * payload inside s_request_queue, so the queued item is larger than
 * SERVER_MAX_ITEM_BYTES alone. drain_one_item()'s own pop buffer must be
 * sized for header+payload together, or Queue_Pop() would safely refuse
 * (never overflow, but also never deliver) this item - permanently
 * blocking the head of the Request Queue instead of sending it. */
void test_max_sized_request_payload_is_not_stuck_behind_the_header(void) {
    static uint8_t max_payload[SERVER_MAX_ITEM_BYTES];
    ServerRequest  req = { SERVER_REQUEST_SEND_TELEMETRY, max_payload, sizeof(max_payload) };
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;
    uint32_t        i;

    for (i = 0; i < sizeof(max_payload); i++) {
        max_payload[i] = (uint8_t)(i & 0xFFu);
    }

    TEST_ASSERT_TRUE(Server_SubmitRequest(&req));
    go_connected();
    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count,
        "A max-sized Request payload must actually be delivered, not silently stuck behind its own header");
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_UINT32(sizeof(max_payload), payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(max_payload, payload, sizeof(max_payload));
}

/* --- Power Mode throttling --- */

void test_low_power_throttles_successive_sends(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));

    s_stub_power_mode = POWER_LOW_POWER;
    go_connected();

    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    Server_Process(); /* too soon - must be throttled */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count, "LOW_POWER must enforce a minimum spacing between sends");

    advance_ms(SERVER_LOW_POWER_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);
}

/* --- Fault -> Reset Request --- */

/* Every failed attempt now also arms the retry backoff
 * (SERVER_SEND_RETRY_DELAY_MS) before the next attempt is allowed - see
 * test_retry_backoff_delays_next_attempt_after_failure below. These
 * "persistent failures" tests must advance time past that backoff between
 * Process() calls, or the later attempts would simply be throttled rather
 * than actually re-attempted. */
void test_persistent_send_failures_trigger_reset_request(void) {
    uint32_t i;

    s_stub_mqtt_publish_result = EC200U_ERROR;
    go_connected();

    for (i = 0; i < SERVER_MAX_SEND_FAILURES_BEFORE_RESET; i++) {
        uint8_t item[] = { 1 };
        Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
        Server_Process();
        advance_ms(SERVER_SEND_RETRY_DELAY_MS);
    }

    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);
    TEST_ASSERT_EQUAL_INT(RESET_SOURCE_SERVER, s_last_reset_request.source);
    TEST_ASSERT_EQUAL_INT(RESET_TYPE_MODULE_RESET, s_last_reset_request.type);
    TEST_ASSERT_EQUAL_INT(RESET_REASON_COMMUNICATION_STUCK, s_last_reset_request.reason);
}

void test_reset_request_not_resubmitted_while_still_failing(void) {
    uint32_t i;

    s_stub_mqtt_publish_result = EC200U_ERROR;
    go_connected();

    for (i = 0; i < SERVER_MAX_SEND_FAILURES_BEFORE_RESET; i++) {
        uint8_t item[] = { 1 };
        Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
        Server_Process();
        advance_ms(SERVER_SEND_RETRY_DELAY_MS);
    }
    TEST_ASSERT_EQUAL_INT(1, s_reset_request_count);

    uint8_t item[] = { 1 };
    Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_reset_request_count, "Must not spam a new Reset Request every cycle while still failing");
}

void test_successful_send_clears_the_failure_counter(void) {
    uint32_t i;

    s_stub_mqtt_publish_result = EC200U_ERROR;
    go_connected();

    for (i = 0; i < SERVER_MAX_SEND_FAILURES_BEFORE_RESET - 1u; i++) {
        uint8_t item[] = { 1 };
        Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
        Server_Process();
        advance_ms(SERVER_SEND_RETRY_DELAY_MS);
    }

    s_stub_mqtt_publish_result = EC200U_OK;
    uint8_t ok_item[] = { 1 };
    Queue_Push(Queues_GetTelemetryQueue(), ok_item, sizeof(ok_item));
    Server_Process();
    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS);

    s_stub_mqtt_publish_result = EC200U_ERROR;
    for (i = 0; i < SERVER_MAX_SEND_FAILURES_BEFORE_RESET - 1u; i++) {
        uint8_t item[] = { 1 };
        Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
        Server_Process();
        advance_ms(SERVER_SEND_RETRY_DELAY_MS);
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count,
        "A successful send in between must reset the failure counter, so this second episode should not yet reach the threshold");
}

/* --- Fault Queue visibility (section 17) --- */

void test_single_send_failure_pushes_a_fault_before_reset_threshold(void) {
    uint8_t item[] = { 1 };

    s_stub_mqtt_publish_result = EC200U_ERROR;
    go_connected();

    TEST_ASSERT_TRUE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()), "sanity: nothing queued yet");

    Queue_Push(Queues_GetTelemetryQueue(), item, sizeof(item));
    Server_Process();

    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetFaultQueue()),
        "A single failed send must be visible in the Fault Queue immediately, not only once the Reset threshold is reached");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_reset_request_count, "Reset threshold is far from reached after just one failure");
}

/* --- NORMAL Power Mode spacing --- */

void test_normal_mode_throttles_successive_sends(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));

    s_stub_power_mode = POWER_NORMAL;
    go_connected();

    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    Server_Process(); /* too soon - must be throttled even in NORMAL */
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count, "NORMAL must also enforce a minimum spacing between sends");

    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);
}

/* --- Retry backoff after a failed send --- */

void test_retry_backoff_delays_next_attempt_after_failure(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));

    s_stub_mqtt_publish_result = EC200U_ERROR;
    s_stub_power_mode          = POWER_NORMAL;
    go_connected();

    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    /* NORMAL interval alone has elapsed, but the retry backoff after a
     * failure is longer - the next attempt must still be held back. */
    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count,
        "A failed send must be followed by SERVER_SEND_RETRY_DELAY_MS backoff, not just the Power-Mode interval");

    advance_ms(SERVER_SEND_RETRY_DELAY_MS - SERVER_NORMAL_MIN_SEND_INTERVAL_MS);
    Server_Process();
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);
}

/* Regression guard, same class of bug already found and fixed in
 * gps_management.c/call_management.c: the backoff reference timestamp
 * must advance on every attempt regardless of that attempt's own result,
 * or a chain of repeated failures would never actually wait out the
 * backoff window (each retry would be judged only against a stale
 * timestamp from before the last, also-failed, attempt). */
void test_retry_backoff_holds_even_when_retries_also_keep_failing(void) {
    uint8_t a[] = { 1 };
    uint8_t b[] = { 2 };
    uint8_t c[] = { 3 };
    Queue_Push(Queues_GetTelemetryQueue(), a, sizeof(a));
    Queue_Push(Queues_GetTelemetryQueue(), b, sizeof(b));
    Queue_Push(Queues_GetTelemetryQueue(), c, sizeof(c));

    s_stub_mqtt_publish_result = EC200U_ERROR;
    s_stub_power_mode          = POWER_NORMAL;
    go_connected();

    Server_Process(); /* attempt #1 - fails */
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    advance_ms(SERVER_SEND_RETRY_DELAY_MS); /* backoff elapses */
    Server_Process(); /* attempt #2 - also fails */
    TEST_ASSERT_EQUAL_INT(2, s_data_send_call_count);

    /* If the backoff reference had not advanced on this second, failed,
     * attempt, this next call (well before a fresh SERVER_SEND_RETRY_DELAY_MS
     * has elapsed since attempt #2) would incorrectly be allowed through. */
    advance_ms(1u);
    Server_Process();
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_data_send_call_count,
        "Backoff must be measured from the most recent attempt, even if that attempt also failed");

    advance_ms(SERVER_SEND_RETRY_DELAY_MS - 1u);
    Server_Process(); /* attempt #3 - backoff elapsed since attempt #2 */
    TEST_ASSERT_EQUAL_INT(3, s_data_send_call_count);
}

/* --- INBOUND path: Command receive/decode/route (server_management.c's
 * handle_incoming_message()/poll_diagnostic_response(), see its file
 * header "INBOUND path" section and OPEN-ITEMS.md for the scope of the
 * explicit user decision this exercises: Diagnostic-only commands flow
 * end-to-end, everything else stays fail-closed via
 * shared/command_safety_policy.c). --- */

void test_subscribe_is_called_once_after_connecting(void) {
    go_connected();
    Server_Process();
    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_mqtt_subscribe_call_count,
        "Subscribe must be idempotent - one call per Connect, not one per Process()");
}

void test_subscribe_is_reissued_after_a_reconnect(void) {
    go_connected();
    Server_Process();
    TEST_ASSERT_EQUAL_INT(1, s_mqtt_subscribe_call_count);

    s_stub_mqtt_state = EC200U_MQTT_DISCONNECTED; /* Session drops */
    Server_Process();
    s_stub_mqtt_state = EC200U_MQTT_CONNECTED; /* reconnects */
    Server_Process();

    TEST_ASSERT_EQUAL_INT(2, s_mqtt_subscribe_call_count);
}

void test_valid_diagnostic_command_is_routed_and_acked(void) {
    go_connected();
    queue_incoming_diagnostic_command(2u /* DIAG_OP_CLEAR_DTC */, 7u, 0u);

    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_diagnostic_submit_call_count,
        "Diagnostic_SubmitRequest() must be called exactly once for a well-formed, allowed Command");
    TEST_ASSERT_EQUAL_INT(DIAG_OP_CLEAR_DTC, s_last_diagnostic_request.operation);
    TEST_ASSERT_EQUAL_UINT32(7u, s_last_diagnostic_request.target);

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count,
        "an ACK must be published once the Command is accepted");
}

void test_ack_envelope_type_is_command_ack(void) {
    uint8_t         msg_type;
    const uint8_t*  payload;
    uint32_t         payload_len;

    go_connected();
    queue_incoming_diagnostic_command(2u, 7u, 0u);

    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);
    parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
    TEST_ASSERT_EQUAL_MESSAGE(TEST_WIRE_MSG_TYPE_RESPONSE, msg_type, "Command ACK must map to the Response wire type (0x11)");
}

void test_diagnostic_response_is_forwarded_once_ready(void) {
    go_connected();
    queue_incoming_diagnostic_command(2u, 7u, 0u);
    Server_Process(); /* accepts the Command, sends the ACK */
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    s_stub_diagnostic_response_ready         = true;
    s_stub_diagnostic_response.status        = DIAG_STATUS_SUCCESS;
    s_stub_diagnostic_response.data_length   = 3u;
    s_stub_diagnostic_response.data[0]       = 0xAA;
    s_stub_diagnostic_response.data[1]       = 0xBB;
    s_stub_diagnostic_response.data[2]       = 0xCC;

    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS); /* clear the ACK's own send throttle */
    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_data_send_call_count,
        "the Diagnostic result must be forwarded to Server once Diagnostic_GetResponse() is ready");

    {
        uint8_t             msg_type;
        const uint8_t*      payload;
        uint32_t             payload_len;
        DiagnosticResponse  roundtrip;

        parse_binary_frame(s_last_sent_payload, s_last_sent_length, &msg_type, &payload, &payload_len);
        TEST_ASSERT_EQUAL_MESSAGE(TEST_WIRE_MSG_TYPE_TELEMETRY, msg_type,
            "SERVER_REQUEST_SEND_DIAGNOSTIC_DATA -> SERVER_MSG_TELEMETRY"); /* raw payload carried opaquely, same as every other item */

        TEST_ASSERT_EQUAL_UINT32(sizeof(DiagnosticResponse), payload_len);
        memcpy(&roundtrip, payload, sizeof(roundtrip));
        TEST_ASSERT_EQUAL_INT(DIAG_STATUS_SUCCESS, roundtrip.status);
        TEST_ASSERT_EQUAL_UINT32(3u, roundtrip.data_length);
        TEST_ASSERT_EQUAL_UINT8(0xAA, roundtrip.data[0]);
        TEST_ASSERT_EQUAL_UINT8(0xBB, roundtrip.data[1]);
        TEST_ASSERT_EQUAL_UINT8(0xCC, roundtrip.data[2]);
    }
}

void test_message_with_wrong_type_is_ignored(void) {
    go_connected();
    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    /* A well-formed Binary frame, but Message Type = Telemetry (0x01), not Command (0x10). */
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_TELEMETRY,
        NULL, 0u, s_mqtt_received_payload);
    s_mqtt_received_pending = true;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(0, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a non-Command frame must never be ACKed");
    TEST_ASSERT_TRUE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_malformed_command_payload_pushes_fault_not_ack(void) {
    uint8_t short_payload[] = { 0xAA }; /* not a 16-byte CommandWirePayload */

    go_connected();
    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        short_payload, sizeof(short_payload), s_mqtt_received_payload);
    s_mqtt_received_pending = true;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(0, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a malformed Command must never be ACKed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_version_mismatch_frame_is_dropped_and_logged(void) {
    LogRecord log_record;
    uint32_t  log_len;

    go_connected();
    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    /* Version byte deliberately wrong (0x02 instead of SERVER_PROTOCOL_VERSION) -
     * binary-protocol-final.md section 2: "نسخه دیگر = Drop + Log خطا". */
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)(SERVER_PROTOCOL_VERSION + 1u), TEST_WIRE_MSG_TYPE_COMMAND,
        NULL, 0u, s_mqtt_received_payload);
    s_mqtt_received_pending = true;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(0, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a version-mismatched frame must never be ACKed");
    TEST_ASSERT_FALSE_MESSAGE(Queue_IsEmpty(Queues_GetLogQueue()), "a version mismatch must be logged, not just silently dropped");
    TEST_ASSERT_TRUE(Queue_Pop(Queues_GetLogQueue(), &log_record, sizeof(log_record), &log_len));
    TEST_ASSERT_EQUAL(LOG_SOURCE_SERVER, log_record.source);
}

void test_diagnostic_busy_pushes_fault_not_ack(void) {
    go_connected();
    queue_incoming_diagnostic_command(1u /* DIAG_OP_READ_DTC */, 1u, 0u);
    s_stub_diagnostic_submit_result = DIAG_SUBMIT_BUSY;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a BUSY Diagnostic must never be ACKed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

void test_unrouted_domain_pushes_fault_not_ack(void) {
    uint32_t wire[4];

    go_connected();

    wire[0] = 99u; /* not COMMAND_DOMAIN_DIAGNOSTIC - CommandSafetyPolicy_IsAllowed() fail-closes it */
    wire[1] = 0u;
    wire[2] = 0u;
    wire[3] = 0u;

    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)wire, sizeof(wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(0, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "policy-rejected domain must never be ACKed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Firmware Download (COMMAND_DOMAIN_FIRMWARE_DOWNLOAD): Download+Store
 * only, no Install - see server_management.c's file header "INBOUND path"
 * section and OPEN-ITEMS.md for the exact scope of this pass. --- */

void test_firmware_download_command_is_routed_and_acked(void) {
    go_connected();
    queue_incoming_firmware_download_command(262144u, "https://example.com/fw.bin");

    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_firmware_submit_call_count,
        "FirmwareUpdate_SubmitRequest() must be called exactly once for a well-formed, allowed Command");
    TEST_ASSERT_EQUAL_UINT32(262144u, s_last_firmware_expected_size);
    TEST_ASSERT_EQUAL_STRING("https://example.com/fw.bin", s_last_firmware_url);
    TEST_ASSERT_EQUAL_INT(0, s_diagnostic_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count, "an ACK must be published once the Command is accepted");
}

void test_firmware_download_busy_pushes_fault_not_ack(void) {
    go_connected();
    queue_incoming_firmware_download_command(1024u, "https://example.com/fw.bin");
    s_stub_firmware_submit_result = false; /* already downloading, expected_size was 0, or url was empty */

    Server_Process();

    TEST_ASSERT_EQUAL_INT(1, s_firmware_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a rejected Firmware Download must never be ACKed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* The old MQTT-chunk-forwarding mechanism (a Command frame arriving while
 * DOWNLOADING used to be treated as raw file bytes) is gone now that the
 * transfer itself uses EC200U's HTTP Client Capability - see
 * handle_incoming_message()'s comment on COMMAND_DOMAIN_FIRMWARE_DOWNLOAD.
 * This is now simply a malformed Command (wrong payload length for either
 * known wire shape), same as any other malformed frame. */
void test_firmware_download_wrong_payload_length_pushes_fault_not_ack(void) {
    uint32_t short_wire[2]; /* domain + one more field - neither CommandWirePayload(16B) nor FirmwareDownloadWirePayload's real length */
    short_wire[0] = (uint32_t)COMMAND_DOMAIN_FIRMWARE_DOWNLOAD;
    short_wire[1] = 1024u;

    go_connected();
    strncpy(s_mqtt_received_topic, "tcu/TEST/data", sizeof(s_mqtt_received_topic) - 1);
    s_mqtt_received_payload_length = build_binary_frame((uint8_t)SERVER_PROTOCOL_VERSION, TEST_WIRE_MSG_TYPE_COMMAND,
        (const uint8_t*)short_wire, sizeof(short_wire), s_mqtt_received_payload);
    s_mqtt_received_pending = true;

    Server_Process();

    TEST_ASSERT_EQUAL_INT(0, s_firmware_submit_call_count);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_data_send_call_count, "a malformed Firmware Download Command must never be ACKed");
    TEST_ASSERT_FALSE(Queue_IsEmpty(Queues_GetFaultQueue()));
}

/* --- Door Actuator Test via Server (COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST) -
 * explicit user decision, OPEN-ITEMS.md 🔴 critical item, Pass-Through by
 * request. Reuses the exact same generic Command decode path already
 * proven for Diagnostic/GPS/Firmware Download - nothing server-management-
 * specific needed beyond widening s_diagnostic_command_in_flight's
 * tracking to this domain too (see handle_incoming_message()). --- */

void test_door_command_is_routed_and_acked(void) {
    go_connected();
    queue_incoming_door_command(0x100u, 1u /* open/unlock */);

    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_diagnostic_submit_call_count,
        "a Door Actuator Test also lands on Diagnostic_SubmitRequest() via command_router.c");
    TEST_ASSERT_EQUAL_INT(DIAG_OP_ACTUATOR_TEST, s_last_diagnostic_request.operation);
    TEST_ASSERT_EQUAL_UINT32(0x100u, s_last_diagnostic_request.target);
    TEST_ASSERT_EQUAL_UINT32(1u, s_last_diagnostic_request.parameter);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count, "an ACK must be published once the Command is accepted");
}

void test_door_command_response_is_forwarded_once_ready(void) {
    go_connected();
    queue_incoming_door_command(0x100u, 0u /* close/lock */);
    Server_Process(); /* accepts the command, sends the ACK */
    TEST_ASSERT_EQUAL_INT(1, s_data_send_call_count);

    s_stub_diagnostic_response_ready = true;
    s_stub_diagnostic_response.status = DIAG_STATUS_SUCCESS;

    advance_ms(SERVER_NORMAL_MIN_SEND_INTERVAL_MS); /* clear the ACK's own send throttle */
    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(2, s_data_send_call_count,
        "the Diagnostic result for a Door command must be forwarded to Server too - poll_diagnostic_response() does not care which domain originated it");
}

/* --- Door via direct GPIO relay (COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO) -
 * second, independent mechanism alongside the Diagnostic Actuator Test
 * pair above - reuses the exact same generic Command decode path, no
 * server-management-specific code needed beyond CommandRouter already
 * knowing this domain. Unlike the Diagnostic-bound domains, this one is
 * NOT tracked by s_diagnostic_command_in_flight - Actuator Management has
 * no async Response to poll, the GPIO write already happened by the time
 * Actuator_SubmitRequest() returns. --- */

void test_door_gpio_command_is_routed_and_acked(void) {
    go_connected();
    queue_incoming_door_gpio_command(1u /* open/unlock */);

    Server_Process();

    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_actuator_submit_call_count,
        "a Door GPIO command must reach Actuator_SubmitRequest() via command_router.c");
    TEST_ASSERT_EQUAL_INT(ACTUATOR_DOOR_UNLOCK, s_last_actuator_request.actuator);
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, s_data_send_call_count, "an ACK must be published once the Command is accepted");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, s_diagnostic_submit_call_count,
        "this domain must never reach Diagnostic Management");
}
