#include "managements/server_management.h"
#include "managements/server_management_config.h"
#include "managements/power_management.h"
#include "managements/diagnostic_management.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/configuration.h"
#include "infrastructure/time_base.h"
#include "shared/queues.h"
#include "shared/reset_request.h"
#include "shared/command_router.h"
#include "shared/log_record.h"
#include <string.h>

/* Contract: docs/02-management-contracts/server-management.md
 * Wire-level Contract: docs/02-management-contracts/backend-contract-tcu-hekar.md
 * (Message Envelope / Encoder-Transport layering) + docs/02-management-contracts/
 * binary-protocol-final.md (Hekar's binary proposal - closed the Encoding
 * decision to Binary and the Header/CRC framing; see OPEN-ITEMS.md "ارتباط
 * TCU <-> Backend" items #1/#2/#5/#8 for exactly what is/isn't closed).
 *
 * OUTBOUND path: draining the Telemetry Queue and Server Event Queue
 * (already fed by production code: GPS Management's process_telemetry()
 * and shared/event_policy.c's dispatch()) plus the explicit
 * Server_SubmitRequest() API, wrapping each item in a Message Envelope,
 * encoding it (Binary: 8-byte Header + raw payload + CRC16-CCITT, per
 * binary-protocol-final.md sections 2/7) and handing it to a Transport
 * Adapter (MQTT, via EC200U Manager's MQTT Capability). Connection State
 * and a Send-failure -> Reset Request path round out the Contract's Fault
 * handling.
 *
 * INBOUND path (Command receive/decode/route): OPEN-ITEMS.md's critical red
 * item ("Command Safety Policy for sensitive commands") explicitly names
 * Server Command Handling as blocked until that gap closes - it still is,
 * for anything that would physically actuate the vehicle (Engine Cut-off,
 * Door Lock-Unlock: routed nowhere, shared/command_router.c has no case for
 * COMMAND_DOMAIN_ACTUATOR yet, and shared/command_safety_policy.c
 * fail-closes any domain it does not explicitly allow). By explicit user
 * decision, Diagnostic-only commands (Read Parameter/Read DTC/Clear DTC -
 * no physical actuation) are exempt and DO flow end-to-end today:
 * handle_incoming_message() below decodes an inbound MQTT Command Envelope,
 * hands it to shared/command_router.c (which re-checks
 * CommandSafetyPolicy_IsAllowed() itself - this file never bypasses that
 * gate), and on acceptance the eventual Diagnostic_GetResponse() result is
 * polled back out and forwarded to Server (poll_diagnostic_response()).
 * See OPEN-ITEMS.md for the exact scope of this decision and its
 * still-open edges (Command JSON Schema, Command ACK correlation by
 * message id, more domains).
 *
 * Event vs Telemetry priority (section 9: "a large volume of Telemetry
 * must not starve Event delivery") is honored by checking the explicit
 * Request queue, then the Server Event Queue, then the Telemetry Queue, in
 * that order, draining at most one item per Process() call - the same
 * one-item-per-call discipline shared/event_policy.c already uses.
 *
 * --- Layering (backend-contract-tcu-hekar.md section 3) ---
 *
 *   Server Management
 *         |
 *   Encoder Interface  --  Binary Encoder   (binary-protocol-final.md, closed)
 *         |
 *   Transport Interface -- MQTT Adapter     (current candidate)
 *         |
 *   EC200U Manager
 *
 * Both interfaces are kept as small function-pointer tables private to this
 * file (not a separate public header) - they are an implementation detail
 * of Server Management's own Contract, same as the request-priority drain
 * order above; nothing outside this file needs to know which Encoder or
 * Transport is active (backend-contract-tcu-hekar.md section 3, [قطعی]:
 * "انتخاب Transport یا Encoder نباید به کد Business Server Management
 * نشت کند"). Swapping the Transport later (a TCP/HTTP Adapter, once
 * Simulation with هکار settles OPEN-ITEMS.md item #1) only means writing a
 * new ServerTransport struct and pointing s_active_transport at it - none
 * of drain_one_item()/send_envelope()/Server_Process() change. */

/* --- Message Envelope ---
 * Wire shape itself (Header fields, no separate Device Id - IMEI travels in
 * the MQTT Topic per binary-protocol-final.md section 1, OPEN-ITEMS.md item
 * #5, closed) now follows binary-protocol-final.md section 2. Kept private
 * to this file for the same reason as the Encoder/Transport interfaces
 * above - an internal implementation detail, not a Contract anything else
 * in the firmware depends on. */
typedef enum {
    SERVER_MSG_EVENT,
    SERVER_MSG_TELEMETRY,
    SERVER_MSG_STATUS,
    SERVER_MSG_COMMAND,
    SERVER_MSG_COMMAND_ACK
} ServerMessageType;

typedef struct {
    uint32_t          version;
    ServerMessageType type;
    uint32_t          message_id;   /* truncated to uint16 Sequence ID on the wire - Trace/Duplicate Detection, binary-protocol-final.md section 2 */
    uint32_t          timestamp;
    const uint8_t*     payload;
    uint32_t           payload_length;
} ServerMessageEnvelope;

/* --- Encoder Interface --- */
typedef struct {
    /* Encodes envelope into out_buffer. Returns the number of bytes
     * written (NOT counting any NUL terminator the Encoder may also write
     * for its own convenience), or 0 if out_buffer was too small to hold
     * the full encoded Envelope - callers must treat 0 as an encode
     * failure, never attempt to send a truncated/partial result. */
    uint32_t (*encode)(const ServerMessageEnvelope* envelope, uint8_t* out_buffer, uint32_t out_buffer_size);
} ServerEncoder;

/* --- Transport Interface --- */
typedef struct {
    /* Fire-and-forget: starts (or re-confirms) a Session. Does not itself
     * report success/failure - get_state() is polled afterward, same
     * asynchronous shape as EC200U_MQTT_Connect() itself. */
    void             (*connect)(void);
    bool             (*is_connected)(void);
    /* Sends one already-encoded item. Returns the underlying EC200U_Result
     * unchanged so the existing Fault/Retry handling in
     * handle_send_result() keeps working unmodified. */
    EC200U_Result    (*publish)(const uint8_t* payload, uint32_t length);
} ServerTransport;

/* TODO(open-item): the system-wide Fault struct is still open
 * (OPEN-ITEMS.md red #5). This is a local, provisional shape for Server
 * Communication faults only, same pattern already used by SmsFaultRecord
 * (sms_management.c) and CallFaultRecord (call_management.c) for their
 * own still-open Contracts. */
typedef struct {
    uint32_t fault_code;
    uint32_t timestamp;
} ServerFaultRecord;

#define SERVER_FAULT_CODE_SEND_FAILED                1u
#define SERVER_FAULT_CODE_COMMAND_REJECTED_POLICY    2u
#define SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED 3u
#define SERVER_FAULT_CODE_COMMAND_BUSY               4u
/* code 5 (SERVER_FAULT_CODE_FIRMWARE_CHUNK_REJECTED) retired - Firmware
 * Download chunks no longer arrive over this MQTT channel at all now that
 * the transfer itself uses EC200U Manager's HTTP Client Capability (see
 * handle_incoming_message()'s comment on COMMAND_DOMAIN_FIRMWARE_DOWNLOAD);
 * left as a documented gap, not reused for something else, so any old log
 * evidence of code 5 is never misread as a different fault. */

/* LogRecord.type meaning for LOG_SOURCE_SERVER (shared/log_record.h -
 * "meaning is entirely up to source") - binary-protocol-final.md section 2:
 * an inbound frame whose Version byte does not match SERVER_PROTOCOL_VERSION
 * must be dropped AND logged as an error, not just silently ignored. */
#define SERVER_LOG_TYPE_PROTOCOL_VERSION_MISMATCH 1u

/* Generic wire shape for an inbound Command's payload, decoded inside
 * handle_incoming_message() below - same [برداشت/پیشنهاد] status as the
 * outbound Envelope (OPEN-ITEMS.md TCU<->Backend #8, Schema still [باز]).
 * Maps 1:1 onto shared/command_router.h's Command - kept as a separate
 * type because this one specifically describes the wire byte layout
 * (fixed uint32_t fields, no padding assumptions left implicit), while
 * Command is the router's own logical shape. */
typedef struct {
    uint32_t domain;
    uint32_t operation;
    uint32_t target;
    uint32_t parameter;
} CommandWirePayload;

/* Dedicated wire shape for COMMAND_DOMAIN_FIRMWARE_DOWNLOAD only - see
 * shared/command_router.h's own comment on FirmwareDownloadRequest for why
 * (a URL does not fit in CommandWirePayload's fixed 4x uint32_t fields
 * above). `domain` sits at the same offset/size as CommandWirePayload's own
 * first field, so handle_incoming_message() can always peek at just that
 * leading uint32_t first to know which of the two shapes a given inbound
 * payload actually is, before deciding how to decode the rest of it. */
typedef struct {
    uint32_t domain;        /* always COMMAND_DOMAIN_FIRMWARE_DOWNLOAD - checked, never trusted blindly */
    uint32_t expected_size;
    char     url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH];
} FirmwareDownloadWirePayload;

static ServerConnectionState s_connection_state;

/* Inbound Command handling state - see the file header's "INBOUND path"
 * section. s_subscribed is a one-shot latch reset on every reconnect
 * (update_connection_state() below) so Subscribe is re-issued after the
 * Session drops, same idempotent-latch shape mqtt_transport_connect()
 * already uses via is_connected(). s_diagnostic_command_in_flight tracks
 * at most one Server-originated Diagnostic-Management-bound Request at a
 * time - both COMMAND_DOMAIN_DIAGNOSTIC (Read/Clear DTC) and
 * COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST (Door Lock/Unlock) land on the
 * same owner (command_router.c), and Diagnostic Management itself already
 * enforces "one Current + one Pending" (diagnostic-management.md section
 * 8), so this Management never needs to track more than a single
 * in-flight id either, regardless of which of the two domains it was. */
static bool s_subscribed;
static bool s_diagnostic_command_in_flight;

/* Server_SubmitRequest's own private queue - separate from the Telemetry/
 * Server-Event Queues owned by Queue Infrastructure, because ServerRequest
 * carries a caller-owned payload pointer that is only valid for the
 * duration of the call; the bytes are copied into this queue immediately
 * (see Server_SubmitRequest below), the pointer itself is never stored. */
static uint8_t s_request_queue_buffer[SERVER_REQUEST_QUEUE_CAPACITY_BYTES];
static Queue   s_request_queue;

static uint32_t s_consecutive_send_failures;
static bool     s_reset_request_sent; /* latch: submit a Reset Request at most once per failure episode */

static bool     s_send_throttled;
static TimeTick s_last_send_tick;

static uint32_t s_next_message_id; /* pre-incremented in send_envelope() - first message id is 1 */

/* ============================= Binary Encoder ============================= */
/* docs/02-management-contracts/binary-protocol-final.md sections 2/7 -
 * OPEN-ITEMS.md "ارتباط TCU <-> Backend" items #2/#8, closed: Encoding is
 * Binary (no JSON phase), Header/CRC framing is fully specified. What is
 * NOT done here (deliberately, per the same OPEN-ITEMS entries and #11/#14
 * history): the exact per-type Telemetry(32B)/Event(14B)/Response(14B)
 * FIELD layout (Vehicle Speed source is still an open 🔴 item; Relay Mask
 * bit-to-Actuator mapping is not specified anywhere) - this Management
 * still only wraps whatever opaque payload bytes its caller already built,
 * same boundary the old JSON Encoder had (server-management.md section
 * 2/11: "Server Management مسئول معنی Payload نیست"). */

/* CRC16-CCITT (poly 0x1021, init 0xFFFF, no reflection - binary-protocol-
 * final.md section 7 "CRC16-CCITT"), computed over every byte of the frame
 * except the trailing CRC field itself, written Big-Endian by the caller. */
static uint16_t crc16_ccitt(const uint8_t* data, uint32_t len) {
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

/* Wire Message Type byte (binary-protocol-final.md section 2: 0x01/0x02/
 * 0x10/0x11 - only 4 values exist on the wire, unlike the 5-value internal
 * ServerMessageType). SERVER_MSG_COMMAND is Backend->TCU only (never
 * actually encoded outbound by send_envelope() today) but mapped anyway for
 * completeness/symmetry with the decode side below. SERVER_MSG_STATUS has
 * no wire code of its own any more - the document removed the separate
 * Status message type and folded it into Telemetry's own Status Flags
 * bitfield (OPEN-ITEMS.md item #7, NOT fully closed: only the wire Message
 * Type mapping is decided here, the real bitfield content is not built by
 * this Management) - mapped to the same 0x01 as Telemetry rather than
 * inventing a 5th wire code the Contract does not have. */
#define SERVER_WIRE_MSG_TYPE_TELEMETRY 0x01u
#define SERVER_WIRE_MSG_TYPE_EVENT     0x02u
#define SERVER_WIRE_MSG_TYPE_COMMAND   0x10u
#define SERVER_WIRE_MSG_TYPE_RESPONSE  0x11u

static uint8_t message_type_to_wire_byte(ServerMessageType type) {
    switch (type) {
        case SERVER_MSG_TELEMETRY:   return SERVER_WIRE_MSG_TYPE_TELEMETRY;
        case SERVER_MSG_EVENT:       return SERVER_WIRE_MSG_TYPE_EVENT;
        case SERVER_MSG_COMMAND:     return SERVER_WIRE_MSG_TYPE_COMMAND;
        case SERVER_MSG_COMMAND_ACK: return SERVER_WIRE_MSG_TYPE_RESPONSE; /* "Frame پاسخ" (section 6) - closest fit for an ACK, same reasoning the old JSON "command_ack" string had */
        case SERVER_MSG_STATUS:      return SERVER_WIRE_MSG_TYPE_TELEMETRY;
        default:                      return SERVER_WIRE_MSG_TYPE_TELEMETRY;
    }
}

/* Encodes: [0]=Version [1]=MsgType [2:3]=SeqId(BE) [4:7]=Timestamp(BE) +
 * raw payload + [last 2]=CRC16-CCITT(BE) over everything before it -
 * binary-protocol-final.md section 2/7. Returns 0 (never a partial frame)
 * if out_buffer cannot hold Header+payload+CRC. */
static uint32_t binary_encoder_encode(const ServerMessageEnvelope* envelope, uint8_t* out_buffer, uint32_t out_buffer_size) {
    uint32_t header_and_payload_len = SERVER_BINARY_HEADER_BYTES + envelope->payload_length;
    uint32_t total_len              = header_and_payload_len + SERVER_BINARY_CRC_BYTES;
    uint16_t seq_id                 = (uint16_t)(envelope->message_id & 0xFFFFu);
    uint16_t crc;

    if (total_len > out_buffer_size) {
        return 0;
    }

    out_buffer[0] = (uint8_t)(envelope->version & 0xFFu);
    out_buffer[1] = message_type_to_wire_byte(envelope->type);
    out_buffer[2] = (uint8_t)((seq_id >> 8) & 0xFFu);
    out_buffer[3] = (uint8_t)(seq_id & 0xFFu);
    out_buffer[4] = (uint8_t)((envelope->timestamp >> 24) & 0xFFu);
    out_buffer[5] = (uint8_t)((envelope->timestamp >> 16) & 0xFFu);
    out_buffer[6] = (uint8_t)((envelope->timestamp >> 8) & 0xFFu);
    out_buffer[7] = (uint8_t)(envelope->timestamp & 0xFFu);

    if (envelope->payload_length > 0u) {
        memcpy(out_buffer + SERVER_BINARY_HEADER_BYTES, envelope->payload, envelope->payload_length);
    }

    crc = crc16_ccitt(out_buffer, header_and_payload_len);
    out_buffer[header_and_payload_len]      = (uint8_t)((crc >> 8) & 0xFFu);
    out_buffer[header_and_payload_len + 1u] = (uint8_t)(crc & 0xFFu);

    return total_len;
}

static const ServerEncoder s_binary_encoder = { binary_encoder_encode };
static const ServerEncoder* s_active_encoder = &s_binary_encoder;

/* ============================= MQTT Transport ============================= */

/* Host/Port now come from Configuration Infrastructure (EEPROM via
 * STORAGE_CONFIG) instead of being unreachable compile-time constants -
 * see the updated comment in server_management_config.h. Sandbox stays
 * the compile-time active environment (same "safest choice at this stage
 * of development" reasoning that comment already documented); switching
 * environments, or making the choice itself runtime-configurable, is a
 * separate still-open product decision, not something this pass invents. */
/* Picks the Host/Port pair for whichever environment Configuration
 * Infrastructure says is active (DeviceConfig.communication.active_environment -
 * infrastructure/configuration.h) - previously this always read *_sandbox,
 * with no runtime mechanism to ever point at Deployment/Operation at all. */
static void select_active_server_address(const DeviceConfig* config, const char** out_host, uint16_t* out_port) {
    switch (config->communication.active_environment) {
        case CONFIG_SERVER_ENV_DEPLOYMENT:
            *out_host = config->communication.server_address_deployment;
            *out_port = config->communication.server_port_deployment;
            break;
        case CONFIG_SERVER_ENV_OPERATION:
            *out_host = config->communication.server_address_operation;
            *out_port = config->communication.server_port_operation;
            break;
        case CONFIG_SERVER_ENV_SANDBOX:
        default: /* an out-of-range value can never be persisted (Configuration_SetActiveServerEnvironment() rejects it) - Sandbox is still the safe fallback if it somehow occurs */
            *out_host = config->communication.server_address_sandbox;
            *out_port = config->communication.server_port_sandbox;
            break;
    }
}

static void mqtt_transport_connect(void) {
    const DeviceConfig* config = Configuration_Get();
    const char*           host;
    uint16_t               port;

    select_active_server_address(config, &host, &port);

    /* Fire-and-forget, same as EC200U_GNSS_Start()/EC200U_Call_Dial()
     * elsewhere in this codebase - Server_Process() polls is_connected()
     * on later calls rather than waiting here. Return value intentionally
     * ignored: EC200U_ERROR only happens for a NULL client_id/host
     * (impossible, SERVER_MQTT_CLIENT_ID_PLACEHOLDER is a compile-time
     * literal and every server_address_* field is always a NUL-terminated
     * buffer inside DeviceConfig) and EC200U_NOT_READY/EC200U_OK both just
     * mean "try again on a later Process() call", which polling
     * is_connected() already achieves. */
    (void)EC200U_MQTT_Connect(SERVER_MQTT_CLIENT_ID_PLACEHOLDER, host, port);
}

static bool mqtt_transport_is_connected(void) {
    return EC200U_MQTT_GetState() == EC200U_MQTT_CONNECTED;
}

static EC200U_Result mqtt_transport_publish(const uint8_t* payload, uint32_t length) {
    EC200U_MqttMessage message;
    message.topic          = SERVER_MQTT_TOPIC_PLACEHOLDER;
    message.payload        = payload;
    message.payload_length = length;
    return EC200U_MQTT_Publish(&message);
}

static const ServerTransport s_mqtt_transport = {
    mqtt_transport_connect,
    mqtt_transport_is_connected,
    mqtt_transport_publish
};
static const ServerTransport* s_active_transport = &s_mqtt_transport;

/* ============================= Core logic ============================= */

static void push_fault(uint32_t fault_code) {
    ServerFaultRecord fault;
    fault.fault_code = fault_code;
    fault.timestamp  = Time_GetTick();
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

static void update_connection_state(void) {
    EC200U_ModuleState  module  = EC200U_GetModuleState();
    EC200U_NetworkState network = EC200U_GetNetworkState();
    bool                cellular_ready = (module == EC200U_MODULE_READY && network == EC200U_NETWORK_REGISTERED);

    if (!cellular_ready) {
        s_connection_state = (module == EC200U_MODULE_OFF) ? SERVER_DISCONNECTED : SERVER_CONNECTING;
        s_subscribed        = false; /* Session will need a fresh Subscribe once reconnected */
        return;
    }

    /* Cellular network alone is no longer enough (see the file header
     * layering note): the real data path is the MQTT Session on top of it
     * (backend-contract-tcu-hekar.md section 3). Kick off/keep the Session
     * once the network underneath it is usable. */
    if (!s_active_transport->is_connected()) {
        s_active_transport->connect();
        s_subscribed = false;
    }

    s_connection_state = s_active_transport->is_connected() ? SERVER_CONNECTED : SERVER_CONNECTING;

    if (s_connection_state == SERVER_CONNECTED && !s_subscribed) {
        /* Fire-and-forget, same shape as connect() above -
         * handle_incoming_message() below simply finds nothing pending
         * until this actually takes effect; no separate state is needed
         * to know "did Subscribe succeed". */
        (void)EC200U_MQTT_Subscribe(SERVER_MQTT_TOPIC_PLACEHOLDER);
        s_subscribed = true;
    }
}

/* Decodes and routes at most one inbound MQTT message per Process() call -
 * same one-item-per-call discipline as drain_one_item(). See the file
 * header's "INBOUND path" section for the full picture. Only a well-formed
 * Binary frame (binary-protocol-final.md section 2) whose Message Type
 * byte is Command (0x10) is acted on; anything else is silently ignored
 * (this Session is Subscribed only to this device's own topic, so nothing
 * else is expected on it, but a stray/malformed message must never be
 * treated as a programming error the way an unrecognized internal
 * EventType is in shared/event_policy.c - this is unauthenticated wire
 * input, not this firmware's own state). */
static void handle_incoming_message(void) {
    EC200U_MqttMessage message;
    uint8_t             frame[SERVER_BINARY_FRAME_MAX_BYTES];
    uint32_t             header_and_payload_len;
    uint16_t             computed_crc;
    uint16_t             received_crc;
    const uint8_t*        payload;
    uint32_t              payload_len;

    if (EC200U_MQTT_GetReceived(&message) != EC200U_OK) {
        return;
    }
    if (message.payload == NULL
        || message.payload_length < (SERVER_BINARY_HEADER_BYTES + SERVER_BINARY_CRC_BYTES)
        || message.payload_length > sizeof(frame)) {
        return; /* too short to even hold Header+CRC, or larger than this Management buffers - ignored, no Fault (never "received" a well-formed frame) */
    }

    memcpy(frame, message.payload, message.payload_length);

    if (frame[0] != (uint8_t)SERVER_PROTOCOL_VERSION) {
        /* binary-protocol-final.md section 2: "نسخه دیگر = Drop + Log خطا" */
        LogRecord log_record;
        log_record.source         = LOG_SOURCE_SERVER;
        log_record.type           = SERVER_LOG_TYPE_PROTOCOL_VERSION_MISMATCH;
        log_record.timestamp      = Time_GetTick();
        log_record.payload_length = 1u;
        log_record.payload[0]     = frame[0];
        (void)Queue_Push(Queues_GetLogQueue(), &log_record, sizeof(log_record));
        return;
    }

    if (frame[1] != SERVER_WIRE_MSG_TYPE_COMMAND) {
        return; /* not a Command frame (or malformed) - ignored, same as before */
    }

    header_and_payload_len = message.payload_length - SERVER_BINARY_CRC_BYTES;
    payload                = frame + SERVER_BINARY_HEADER_BYTES;
    payload_len             = header_and_payload_len - SERVER_BINARY_HEADER_BYTES;

    computed_crc = crc16_ccitt(frame, header_and_payload_len);
    received_crc = (uint16_t)(((uint16_t)frame[header_and_payload_len] << 8) | frame[header_and_payload_len + 1u]);
    if (computed_crc != received_crc) {
        push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
        return;
    }

    /* COMMAND_DOMAIN_FIRMWARE_DOWNLOAD carries a URL the generic
     * CommandWirePayload below cannot hold - peek at the leading `domain`
     * field (same offset/size in both wire shapes) before deciding which
     * one this payload actually is. Explicit user decision (OPEN-ITEMS.md
     * TCU<->Backend #12, after HTTP was confirmed as the FOTA transport):
     * this domain no longer arrives via CommandRouter_Submit()/Command at
     * all - see shared/command_router.h's comment on FirmwareDownloadRequest.
     * The transfer itself now happens over HTTP (firmware_update_management.c
     * calls EC200U_HTTP_StartGet()), so this MQTT channel no longer carries
     * file chunks either - the old "Download already in progress -> treat
     * every subsequent Command frame as a raw chunk" disambiguation this
     * comment block used to describe is gone. */
    if (payload_len < (uint32_t)sizeof(uint32_t)) {
        push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
        return;
    }

    {
        uint32_t domain_value;
        memcpy(&domain_value, payload, sizeof(domain_value));

        if ((CommandDomain)domain_value == COMMAND_DOMAIN_FIRMWARE_DOWNLOAD) {
            if (payload_len != (uint32_t)sizeof(FirmwareDownloadWirePayload)) {
                push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
                return;
            }

            FirmwareDownloadWirePayload wire;
            FirmwareDownloadRequest      request;
            memcpy(&wire, payload, sizeof(wire));
            wire.url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH - 1u] = '\0'; /* defense in depth - never trust the sender to have NUL-terminated it */

            request.expected_size = wire.expected_size;
            memcpy(request.url, wire.url, sizeof(request.url));

            switch (CommandRouter_SubmitFirmwareDownload(&request)) {
                case COMMAND_ROUTER_ACCEPTED:
                    /* Fire-and-forget ACK only, same "received, not executed"
                     * meaning as the generic path below - no result-reporting
                     * for this domain, explicit user scope (OPEN-ITEMS.md). */
                    {
                        ServerRequest ack;
                        ack.type           = SERVER_REQUEST_SEND_COMMAND_ACK;
                        ack.payload         = NULL;
                        ack.payload_length  = 0u;
                        (void)Server_SubmitRequest(&ack);
                    }
                    break;
                case COMMAND_ROUTER_REJECTED_POLICY:
                    push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_POLICY);
                    break;
                case COMMAND_ROUTER_REJECTED_MALFORMED:
                    push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
                    break;
                case COMMAND_ROUTER_BUSY:
                    push_fault(SERVER_FAULT_CODE_COMMAND_BUSY);
                    break;
            }
            return;
        }
    }

    if (payload_len != (uint32_t)sizeof(CommandWirePayload)) {
        push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
        return;
    }

    {
        CommandWirePayload wire;
        Command             command;
        memcpy(&wire, payload, sizeof(wire));

        command.domain    = (CommandDomain)wire.domain;
        command.operation = wire.operation;
        command.target     = wire.target;
        command.parameter  = wire.parameter;

        switch (CommandRouter_Submit(&command)) {
            case COMMAND_ROUTER_ACCEPTED: {
                /* Command ACK means "received/accepted", NOT "executed"
                 * (backend-contract-tcu-hekar.md line 77 - a Diagnostic
                 * result follows later via poll_diagnostic_response()
                 * below, on its own Event/Telemetry path, never through
                 * the ACK itself). */
                ServerRequest ack;
                ack.type           = SERVER_REQUEST_SEND_COMMAND_ACK;
                ack.payload         = NULL;
                ack.payload_length  = 0u;
                (void)Server_SubmitRequest(&ack);
                if (command.domain == COMMAND_DOMAIN_DIAGNOSTIC || command.domain == COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST) {
                    /* Both land on Diagnostic Management (command_router.c) -
                     * poll_diagnostic_response() below owns reporting either
                     * one back, it does not need to know which. */
                    s_diagnostic_command_in_flight = true;
                }
                break;
            }
            case COMMAND_ROUTER_REJECTED_POLICY:
                push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_POLICY);
                break;
            case COMMAND_ROUTER_REJECTED_MALFORMED:
                push_fault(SERVER_FAULT_CODE_COMMAND_REJECTED_MALFORMED);
                break;
            case COMMAND_ROUTER_BUSY:
                push_fault(SERVER_FAULT_CODE_COMMAND_BUSY);
                break;
        }
    }
}

/* Polls at most once per Process() call for the result of a Command this
 * Management itself routed via CommandRouter_Submit() above -
 * Diagnostic_GetResponse() must be polled by whoever submitted the
 * Request (diagnostic-management.md section 10), and this is Server
 * Management's own share of that duty for Server-originated Requests
 * only. The raw DiagnosticResponse bytes are forwarded opaquely, same
 * "Server Management does not interpret Payload meaning" boundary as
 * every other outbound item (binary_encoder_encode()'s own comment). */
static void poll_diagnostic_response(void) {
    DiagnosticResponse response;
    ServerRequest       result;

    if (!s_diagnostic_command_in_flight) {
        return;
    }
    if (!Diagnostic_GetResponse(&response)) {
        return; /* still RUNNING - try again on a later Process() call */
    }

    s_diagnostic_command_in_flight = false;

    result.type           = SERVER_REQUEST_SEND_DIAGNOSTIC_DATA;
    result.payload         = (const uint8_t*)&response;
    result.payload_length  = (uint32_t)sizeof(response);
    (void)Server_SubmitRequest(&result);
}

static void handle_send_result(EC200U_Result result) {
    ResetRequest req;

    if (result == EC200U_OK) {
        s_consecutive_send_failures = 0;
        s_reset_request_sent        = false;
        return;
    }

    /* Section 17: "the Management creates a visible Fault if needed" -
     * every single failed send is reported here, not only once the Reset
     * threshold below is reached, so System Management's central Fault
     * view (System_GetFaultCount()) is not blind to Server communication
     * trouble until things get as bad as a Reset. */
    push_fault(SERVER_FAULT_CODE_SEND_FAILED);

    s_consecutive_send_failures++;
    if (s_consecutive_send_failures >= SERVER_MAX_SEND_FAILURES_BEFORE_RESET && !s_reset_request_sent) {
        req.type      = RESET_TYPE_MODULE_RESET;
        req.target    = "EC200U";
        req.source    = RESET_SOURCE_SERVER;
        req.reason    = RESET_REASON_COMMUNICATION_STUCK;
        req.timestamp = Time_GetTick();
        System_SubmitResetRequest(&req);
        s_reset_request_sent = true; /* do not resubmit every cycle while still failing */
    }
}

/* Maps the caller-visible ServerRequestType (server_management.h) onto the
 * wire-level ServerMessageType (backend-contract-tcu-hekar.md section 4).
 * Only two of the five ServerRequestType values have an obvious 1:1
 * meaning; the rest are mapped to the CLOSEST fit per the task's own
 * instruction, reasoned as follows:
 *   - SEND_TELEMETRY       -> TELEMETRY   (exact match)
 *   - SEND_EVENT           -> EVENT       (exact match)
 *   - SEND_GPS_DATA        -> TELEMETRY   ("چه مقداری اندازه‌گیری شد؟" -
 *                              a position reading is a measured value, same
 *                              shape as any other Telemetry record)
 *   - SEND_DIAGNOSTIC_DATA -> TELEMETRY   (same reasoning - a DTC/parameter
 *                              reading is a measured value, not something
 *                              that "happened" or a periodic device-status
 *                              snapshot)
 *   - SEND_FAULT_DATA      -> EVENT       ("چه اتفاقی افتاد؟" fits a Fault
 *                              report - something went wrong - better than
 *                              TELEMETRY's "what was measured" or STATUS's
 *                              periodic/on-change device snapshot)
 *   - SEND_COMMAND_ACK     -> COMMAND_ACK (exact match - see the file
 *                              header's "INBOUND path" section) */
static ServerMessageType map_request_type_to_message_type(ServerRequestType type) {
    switch (type) {
        case SERVER_REQUEST_SEND_TELEMETRY:       return SERVER_MSG_TELEMETRY;
        case SERVER_REQUEST_SEND_EVENT:           return SERVER_MSG_EVENT;
        case SERVER_REQUEST_SEND_GPS_DATA:        return SERVER_MSG_TELEMETRY;
        case SERVER_REQUEST_SEND_DIAGNOSTIC_DATA: return SERVER_MSG_TELEMETRY;
        case SERVER_REQUEST_SEND_FAULT_DATA:      return SERVER_MSG_EVENT;
        case SERVER_REQUEST_SEND_COMMAND_ACK:     return SERVER_MSG_COMMAND_ACK;
        default:                                   return SERVER_MSG_TELEMETRY;
    }
}

/* Builds the Envelope for one item, encodes it with the active Encoder and
 * hands the result to the active Transport. Always returns true (an item
 * was popped and "handled", one way or another - see drain_one_item()'s
 * own comment: nothing is re-queued on failure, matching the existing
 * Offline Behavior note). An encode failure is folded into the same
 * handle_send_result() Fault/Retry path as a Transport failure - from this
 * Management's Contract point of view both are simply "could not deliver
 * this item", not two separate failure taxonomies. */
static bool send_envelope(ServerMessageType type, const uint8_t* payload, uint32_t payload_len) {
    ServerMessageEnvelope envelope;
    uint8_t                encoded[SERVER_BINARY_FRAME_MAX_BYTES];
    uint32_t                encoded_len;

    envelope.version        = SERVER_PROTOCOL_VERSION;
    envelope.type            = type;
    envelope.message_id      = ++s_next_message_id;
    envelope.timestamp       = Time_GetTick();
    envelope.payload         = payload;
    envelope.payload_length  = payload_len;

    encoded_len = s_active_encoder->encode(&envelope, encoded, sizeof(encoded));
    if (encoded_len == 0u) {
        handle_send_result(EC200U_ERROR);
        return true;
    }

    handle_send_result(s_active_transport->publish(encoded, encoded_len));
    return true;
}

/* Drains and forwards exactly one item, highest to lowest priority.
 * Returns true if something was actually popped and sent (used to know
 * whether to (re)start the Power-Mode send-spacing timer). A failed Send
 * still counts as "something was sent" for throttling purposes - the item
 * is already popped and gone either way (see the file-level Offline
 * Behavior note: nothing is re-queued on failure). */
static bool drain_one_item(void) {
    /* Sized for the LARGEST possible item across all three source queues -
     * s_request_queue's own items carry a ServerRequestType header on top
     * of up to SERVER_MAX_ITEM_BYTES of payload (see Server_SubmitRequest()'s
     * own item_buf sizing below), which can exceed SERVER_MAX_ITEM_BYTES
     * alone. A buffer sized to SERVER_MAX_ITEM_BYTES only would make
     * Queue_Pop() safely refuse (never overflow) a max-sized Request item -
     * but "safely refuse" here means it is silently never popped at all,
     * permanently blocking the head of s_request_queue instead of being
     * delivered. The Server Event / Telemetry Queues below never carry more
     * than SERVER_MAX_ITEM_BYTES per item, so the larger buffer is simply
     * unused headroom for those two, not a behavior change. */
    uint8_t  buf[sizeof(ServerRequestType) + SERVER_MAX_ITEM_BYTES];
    uint32_t len;

    if (Queue_Pop(&s_request_queue, buf, sizeof(buf), &len)) {
        /* First bytes are the ServerRequestType header written by
         * Server_SubmitRequest below - that header is this Management's
         * own internal routing detail, not part of the wire payload. It is
         * read back here only to pick the right ServerMessageType for the
         * Envelope. */
        ServerRequestType header_type;
        uint32_t           header_size  = (uint32_t)sizeof(ServerRequestType);
        const uint8_t*     payload      = (len > header_size) ? (buf + header_size) : buf;
        uint32_t            payload_len  = (len > header_size) ? (len - header_size) : 0u;

        memcpy(&header_type, buf, (len >= header_size) ? header_size : len);
        return send_envelope(map_request_type_to_message_type(header_type), payload, payload_len);
    }

    if (Queue_Pop(Queues_GetServerEventQueue(), buf, sizeof(buf), &len)) {
        return send_envelope(SERVER_MSG_EVENT, buf, len);
    }

    if (Queue_Pop(Queues_GetTelemetryQueue(), buf, sizeof(buf), &len)) {
        return send_envelope(SERVER_MSG_TELEMETRY, buf, len);
    }

    return false;
}

void Server_Init(void) {
    s_connection_state = SERVER_DISCONNECTED;
    Queue_Init(&s_request_queue, s_request_queue_buffer, sizeof(s_request_queue_buffer));

    s_consecutive_send_failures = 0;
    s_reset_request_sent        = false;

    s_send_throttled = false;
    s_last_send_tick = 0;

    s_next_message_id = 0; /* first envelope sent gets message_id 1 */

    s_subscribed                   = false;
    s_diagnostic_command_in_flight = false;
}

void Server_Process(void) {
    update_connection_state();

    if (s_connection_state != SERVER_CONNECTED) {
        return; /* Offline Behavior (section 13): items simply stay queued - no loss, no busy retry here */
    }

    /* Inbound handling runs every cycle regardless of the outbound
     * throttle below - a Command Envelope arriving, or a Diagnostic
     * Response becoming available, is a distinct concern from outbound
     * send pacing (see the file header's "INBOUND path" section). */
    handle_incoming_message();
    poll_diagnostic_response();

    if (s_send_throttled) {
        /* Power-Mode spacing applies unconditionally (both NORMAL and
         * LOW_POWER, section 18 - NORMAL is no longer "no throttle at
         * all"). If the previous attempt failed, the retry backoff
         * (section 12/17) is combined in by taking the LARGER of the two
         * windows, not their sum - see server_management_config.h. */
        uint32_t interval = (Power_GetMode() == POWER_LOW_POWER)
                                 ? SERVER_LOW_POWER_MIN_SEND_INTERVAL_MS
                                 : SERVER_NORMAL_MIN_SEND_INTERVAL_MS;
        if (s_consecutive_send_failures > 0 && SERVER_SEND_RETRY_DELAY_MS > interval) {
            interval = SERVER_SEND_RETRY_DELAY_MS;
        }
        if (!Time_Elapsed(s_last_send_tick, interval)) {
            return;
        }
    }

    if (drain_one_item()) {
        s_last_send_tick = Time_GetTick();
        s_send_throttled = true;
    }
}

bool Server_SubmitRequest(const ServerRequest* request) {
    uint8_t  item_buf[sizeof(ServerRequestType) + SERVER_MAX_ITEM_BYTES];
    uint32_t item_len;

    if (!request) return false;
    if (request->payload_length > SERVER_MAX_ITEM_BYTES) return false; /* rejected - see SERVER_MAX_ITEM_BYTES */
    if (request->payload_length > 0 && !request->payload) return false;

    memcpy(item_buf, &request->type, sizeof(request->type));
    if (request->payload_length > 0) {
        memcpy(item_buf + sizeof(request->type), request->payload, request->payload_length);
    }
    item_len = (uint32_t)sizeof(request->type) + request->payload_length;

    /* Queue_Push copies these bytes into its own buffer right here - the
     * caller's payload pointer is never stored past this call (section 6/8:
     * "Request Accepted" is not "Request Processed", but the bytes
     * themselves must already be safe to use whenever Process() later
     * drains this queue). */
    return Queue_Push(&s_request_queue, item_buf, item_len);
}

ServerConnectionState Server_GetConnectionState(void) {
    return s_connection_state;
}
