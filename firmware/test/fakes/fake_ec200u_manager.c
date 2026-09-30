#include "infrastructure/ec200u_manager.h"
#include <string.h>

/* Fake EC200U Manager برای Unit Test - طبق اصل Fake/Stub در ec200u-manager.md §21.
 * هدف: امکان تست GPS/Server/SMS/Call Management بدون سخت‌افزار EC200U واقعی.
 * سناریوهای تست از طریق توابع FakeEC200U_* کنترل می‌شوند (نه از طریق AT Command). */

static EC200U_ModuleState  s_module_state = EC200U_MODULE_OFF;
static EC200U_NetworkState s_network_state = EC200U_NETWORK_UNKNOWN;
static EC200U_SimCardState s_sim_card_state = EC200U_SIMCARD_NOT_READY;
static EC200U_GnssData     s_gnss_data;
static bool                s_sms_pending;
static EC200U_SmsData      s_pending_sms;
static EC200U_CallState    s_call_state = EC200U_CALL_IDLE;
static bool                s_call_is_incoming; /* true only for a session started by FakeEC200U_InjectIncomingCall() */
static char                s_incoming_call_number[64];

/* MQTT Capability fake state - Server Management's own Ceedling tests will
 * mock this whole header via CMock instead in most cases, but this Fake is
 * still kept up to date per project convention (see top-of-file note and
 * CLAUDE.md: GPS/Server/SMS/Call need a working EC200U to develop/test
 * against independent of the outsourced contractor). Architecture-level
 * capability only - MQTT is NOT confirmed as the final Transport, see the
 * block comment above EC200U_MQTT_Connect() in ec200u_manager.h. */
static EC200U_MqttState s_mqtt_state = EC200U_MQTT_DISCONNECTED;
static bool             s_mqtt_received_pending;
static char             s_mqtt_received_topic[64];
static uint8_t          s_mqtt_received_payload[256];
static uint32_t         s_mqtt_received_payload_length;

EC200U_Result EC200U_Init(void) {
    s_module_state = EC200U_MODULE_READY;
    s_network_state = EC200U_NETWORK_REGISTERED;
    s_sim_card_state = EC200U_SIMCARD_READY;
    memset(&s_gnss_data, 0, sizeof(s_gnss_data));
    s_call_state = EC200U_CALL_IDLE;
    s_call_is_incoming = false;
    s_incoming_call_number[0] = '\0';
    s_mqtt_state = EC200U_MQTT_DISCONNECTED;
    s_mqtt_received_pending = false;
    s_mqtt_received_payload_length = 0;
    s_mqtt_received_topic[0] = '\0';
    return EC200U_OK;
}

void EC200U_Process(void) {}

EC200U_ModuleState EC200U_GetModuleState(void) { return s_module_state; }
EC200U_NetworkState EC200U_GetNetworkState(void) { return s_network_state; }
EC200U_SimCardState EC200U_GetSimCardState(void) { return s_sim_card_state; }

EC200U_Result EC200U_GNSS_Start(void) { return EC200U_OK; }
EC200U_Result EC200U_GNSS_Stop(void) { return EC200U_OK; }
EC200U_Result EC200U_GNSS_GetLatest(EC200U_GnssData* data) {
    if (!data) return EC200U_ERROR;
    *data = s_gnss_data;
    return EC200U_OK;
}

EC200U_Result EC200U_SMS_Send(const EC200U_SmsData* sms) { (void)sms; return EC200U_OK; }
EC200U_Result EC200U_SMS_GetReceived(EC200U_SmsData* sms) {
    if (!sms) return EC200U_ERROR;
    if (!s_sms_pending) return EC200U_NOT_READY;
    *sms = s_pending_sms;
    s_sms_pending = false;
    return EC200U_OK;
}

EC200U_Result EC200U_Call_Dial(const char* number) {
    (void)number;
    s_call_state       = EC200U_CALL_DIALING;
    s_call_is_incoming = false;
    return EC200U_OK;
}
EC200U_Result EC200U_Call_Hangup(void) { s_call_state = EC200U_CALL_ENDED; return EC200U_OK; }
EC200U_CallState EC200U_Call_GetState(void) { return s_call_state; }

/* Answer only makes sense while a call is currently RINGING - same
 * NOT_READY-otherwise convention as the board simulator
 * (src/infrastructure/ec200u_manager.c). */
EC200U_Result EC200U_Call_Answer(void) {
    if (s_call_state != EC200U_CALL_RINGING) {
        return EC200U_NOT_READY;
    }
    s_call_state = EC200U_CALL_ACTIVE;
    return EC200U_OK;
}

/* Caller ID is only meaningful for a call that actually originated as
 * incoming (FakeEC200U_InjectIncomingCall()) and is still RINGING - an
 * outgoing call this side dialed has no incoming number to report, same
 * rule the board simulator applies. */
EC200U_Result EC200U_Call_GetIncomingNumber(char* buffer, uint32_t buffer_size) {
    uint32_t i;

    if (!buffer || buffer_size == 0) return EC200U_ERROR;
    if (s_call_state != EC200U_CALL_RINGING || !s_call_is_incoming) return EC200U_NOT_READY;

    i = 0;
    while (i < (buffer_size - 1u) && s_incoming_call_number[i] != '\0') {
        buffer[i] = s_incoming_call_number[i];
        i++;
    }
    buffer[i] = '\0';
    return EC200U_OK;
}

EC200U_Result EC200U_Data_Send(const uint8_t* payload, uint32_t length) { (void)payload; (void)length; return EC200U_OK; }
EC200U_Result EC200U_Data_Receive(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length) {
    (void)buffer; (void)buffer_size;
    if (received_length) *received_length = 0;
    return EC200U_NOT_READY;
}

/* Connect() "succeeds" immediately (CONNECTED right away, no CONNECTING
 * staging) - same trivial-success convention EC200U_GNSS_Start()/
 * EC200U_Data_Send() already use in this Fake. A test that specifically
 * needs to observe Server Management's behavior while still CONNECTING (or
 * DISCONNECTED) drives that directly with FakeEC200U_SetMqttState() below,
 * instead of this Fake modeling any timing itself. */
EC200U_Result EC200U_MQTT_Connect(const char* client_id, const char* host, uint16_t port) {
    (void)port;
    if (!client_id || !host) return EC200U_ERROR;
    s_mqtt_state = EC200U_MQTT_CONNECTED;
    return EC200U_OK;
}

EC200U_Result EC200U_MQTT_Disconnect(void) {
    s_mqtt_state = EC200U_MQTT_DISCONNECTED;
    return EC200U_OK;
}

EC200U_MqttState EC200U_MQTT_GetState(void) { return s_mqtt_state; }

EC200U_Result EC200U_MQTT_Publish(const EC200U_MqttMessage* message) {
    if (!message || !message->topic || (!message->payload && message->payload_length != 0u)) {
        return EC200U_ERROR;
    }
    if (s_mqtt_state != EC200U_MQTT_CONNECTED) return EC200U_NOT_READY;
    return EC200U_OK;
}

EC200U_Result EC200U_MQTT_Subscribe(const char* topic) {
    if (!topic) return EC200U_ERROR;
    if (s_mqtt_state != EC200U_MQTT_CONNECTED) return EC200U_NOT_READY;
    return EC200U_OK;
}

/* Pop, not Peek - same convention as EC200U_SMS_GetReceived() above. */
EC200U_Result EC200U_MQTT_GetReceived(EC200U_MqttMessage* message) {
    if (!message) return EC200U_ERROR;
    if (!s_mqtt_received_pending) return EC200U_NOT_READY;
    message->topic          = s_mqtt_received_topic;
    message->payload        = s_mqtt_received_payload;
    message->payload_length = s_mqtt_received_payload_length;
    s_mqtt_received_pending  = false;
    return EC200U_OK;
}

/* --- کنترل سناریو برای تست (Test-Only API) --- */
void FakeEC200U_SetGnssFix(int32_t lat, int32_t lon, uint32_t timestamp) {
    s_gnss_data.latitude = lat;
    s_gnss_data.longitude = lon;
    s_gnss_data.timestamp = timestamp;
    s_gnss_data.fix_valid = true;
}

void FakeEC200U_ClearGnssFix(void) { s_gnss_data.fix_valid = false; }

void FakeEC200U_InjectIncomingSms(const char* destination, const char* payload, uint32_t payload_length) {
    s_pending_sms.destination = destination;
    s_pending_sms.payload = payload;
    s_pending_sms.payload_length = payload_length;
    s_sms_pending = true;
}

/* Moves the fake call state to RINGING with the given caller number
 * available via EC200U_Call_GetIncomingNumber() - without EC200U_Call_Dial()
 * ever being called, exactly like a real unsolicited incoming call.
 * number is copied immediately; the caller's own pointer/buffer does not
 * need to stay valid afterwards. */
void FakeEC200U_InjectIncomingCall(const char* number) {
    uint32_t i = 0;

    s_call_state       = EC200U_CALL_RINGING;
    s_call_is_incoming = true;

    if (number) {
        while (i < (sizeof(s_incoming_call_number) - 1u) && number[i] != '\0') {
            s_incoming_call_number[i] = number[i];
            i++;
        }
    }
    s_incoming_call_number[i] = '\0';
}

void FakeEC200U_SetModuleState(EC200U_ModuleState state) { s_module_state = state; }
void FakeEC200U_SetNetworkState(EC200U_NetworkState state) { s_network_state = state; }
void FakeEC200U_SetSimCardState(EC200U_SimCardState state) { s_sim_card_state = state; }

/* Directly drives the fake MQTT Session state - e.g. to exercise Server
 * Management's behavior while DISCONNECTED or CONNECTING, since
 * EC200U_MQTT_Connect() above always jumps straight to CONNECTED. */
void FakeEC200U_SetMqttState(EC200U_MqttState state) { s_mqtt_state = state; }

/* Simulates a Command message arriving from Backend over MQTT, available
 * via EC200U_MQTT_GetReceived() above, without a real Subscribe/Publish
 * round trip - same purpose as FakeEC200U_InjectIncomingSms()/
 * FakeEC200U_InjectIncomingCall() above. topic/payload are copied
 * immediately (bounded, silently truncated to this Fake's fixed storage);
 * the caller's own pointers/buffers do not need to stay valid afterward. */
void FakeEC200U_InjectMqttReceived(const char* topic, const uint8_t* payload, uint32_t payload_length) {
    uint32_t i = 0;

    if (topic) {
        while (i < (sizeof(s_mqtt_received_topic) - 1u) && topic[i] != '\0') {
            s_mqtt_received_topic[i] = topic[i];
            i++;
        }
    }
    s_mqtt_received_topic[i] = '\0';

    s_mqtt_received_payload_length = 0;
    if (payload && payload_length > 0) {
        uint32_t n = payload_length;
        if (n > sizeof(s_mqtt_received_payload)) n = (uint32_t)sizeof(s_mqtt_received_payload);
        memcpy(s_mqtt_received_payload, payload, n);
        s_mqtt_received_payload_length = n;
    }

    s_mqtt_received_pending = true;
}
