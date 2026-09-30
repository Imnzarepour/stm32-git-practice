#ifndef MGMT_SERVER_H
#define MGMT_SERVER_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/02-management-contracts/server-management.md
 * See also: docs/02-management-contracts/backend-contract-tcu-hekar.md
 * (TCU <-> Backend "هکار" wire-level Contract - Message Envelope, Encoder/
 * Transport layering) and docs/02-management-contracts/binary-protocol-final.md
 * (Hekar's binary proposal - closed Encoding to Binary, closed the Header/
 * CRC framing, closed Transport to MQTT+4 Topics - see OPEN-ITEMS.md
 * "ارتباط TCU <-> Backend" for exactly what is/isn't closed).
 * TODO(open-item): the exact per-message-type FIELD Schema (Telemetry/Event/
 * Response body layout - Vehicle Speed source, Relay Mask bit mapping) is
 * still [باز] - server_management.c's Binary Encoder only implements the
 * wire ENVELOPE (Header+CRC), it still carries whatever opaque payload
 * bytes its caller already built, same boundary the old JSON Encoder had.
 * The Transport Adapter (MQTT, via EC200U Manager) is built so it stays
 * swappable (backend-contract-tcu-hekar.md section 3), not a claim it is
 * final. */

typedef enum {
    SERVER_REQUEST_SEND_TELEMETRY,
    SERVER_REQUEST_SEND_EVENT,
    SERVER_REQUEST_SEND_DIAGNOSTIC_DATA,
    SERVER_REQUEST_SEND_GPS_DATA,
    SERVER_REQUEST_SEND_FAULT_DATA,
    /* Server Management's own reaction to an inbound Command it just
     * decoded (server_management.c's handle_incoming_message()) - never
     * submitted by another Management, only used internally, but kept as
     * a ServerRequestType so it reuses the existing request queue/
     * priority/throttle path instead of a bespoke one. */
    SERVER_REQUEST_SEND_COMMAND_ACK
} ServerRequestType;

typedef struct {
    ServerRequestType type;
    const uint8_t*     payload;
    uint32_t            payload_length;
} ServerRequest;

typedef enum {
    SERVER_DISCONNECTED,
    SERVER_CONNECTING,
    SERVER_CONNECTED
} ServerConnectionState;

void                  Server_Init(void);
void                  Server_Process(void);
bool                  Server_SubmitRequest(const ServerRequest* request);
ServerConnectionState Server_GetConnectionState(void);

#endif
