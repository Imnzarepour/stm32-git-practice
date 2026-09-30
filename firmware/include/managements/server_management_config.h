#ifndef MGMT_SERVER_CONFIG_H
#define MGMT_SERVER_CONFIG_H
#include "shared/command_router.h" /* FIRMWARE_DOWNLOAD_URL_MAX_LENGTH - needed by SERVER_BINARY_MAX_PAYLOAD_BYTES below */

/* Every tunable constant for Server Management lives in this one file,
 * same convention as hardware_input_management_config.h and
 * gps_management_config.h. None of these are part of the Contract
 * (docs/02-management-contracts/server-management.md) - the Contract
 * explicitly leaves cadence/retry/buffer-size details as this
 * Management's internal design. */

/* --- Explicit Server Request queue (Server_SubmitRequest) ---
 * A small private queue, separate from the Telemetry/Server-Event Queues
 * owned by Queue Infrastructure, because ServerRequest carries a
 * caller-owned payload pointer that is only valid for the duration of the
 * Server_SubmitRequest() call - the bytes are copied in immediately, the
 * pointer itself is never stored (see server_management.c). */
#define SERVER_REQUEST_QUEUE_CAPACITY_BYTES 256u

/* Largest single item (Event, Telemetry record, or explicit Request
 * payload) Server Management will forward in one EC200U_Data_Send() call.
 * TODO(open-item): the system-wide Telemetry payload format is still open
 * (OPEN-ITEMS.md, red, #3); this is a generous placeholder sized well
 * above today's actual records (Event ~12 bytes, GpsTelemetryRecord ~12
 * bytes), not a Contract limit. */
#define SERVER_MAX_ITEM_BYTES 128u

/* --- Fault escalation (server-management.md section 12/17) ---
 * After this many consecutive EC200U_Data_Send() failures, Server
 * Management gives up locally and submits a Reset Request for EC200U to
 * System Management, instead of retrying forever. */
#define SERVER_MAX_SEND_FAILURES_BEFORE_RESET 5u

/* --- Send spacing / Power Mode (server-management.md section 18) ---
 * All timing constants that pace outbound sends live together here, same
 * convention as the rest of this file:
 *
 *  - SERVER_NORMAL_MIN_SEND_INTERVAL_MS: minimum spacing between
 *    successive sends while in NORMAL. Without this, the real send rate
 *    would just be "as fast as the Super Loop spins", which is not a
 *    realistic pace for a cellular link. Chosen in line with the polling
 *    cadence already used elsewhere in NORMAL (CALL_IDLE_POLL_INTERVAL_MS
 *    in call_management_config.h is 1000ms) - not itself part of the
 *    Contract, just this Management's own internal pacing choice.
 *  - SERVER_LOW_POWER_MIN_SEND_INTERVAL_MS: wider spacing while in
 *    LOW_POWER ("Reduced Communication/Telemetry Activity", section 18) -
 *    same 5000ms value already used by SMS_LOW_POWER_MIN_SEND_INTERVAL_MS
 *    (sms_management_config.h) for the same reason.
 *  - SERVER_SEND_RETRY_DELAY_MS: after ANY failed EC200U_Data_Send()
 *    attempt (independent of Power Mode), at least this much time must
 *    pass before the next item is attempted - same idea and same value as
 *    SMS_SEND_RETRY_DELAY_MS (sms_management_config.h) and
 *    CALL_NO_CONNECTION_TIMEOUT_MS (call_management_config.h).
 *
 * Server_Process() combines these by taking the LARGER of "the Power-Mode
 * interval for the current mode" and "the retry delay, if the previous
 * attempt failed" - not the sum of the two - so a failed send in NORMAL
 * still only waits SERVER_SEND_RETRY_DELAY_MS (not
 * NORMAL_INTERVAL + RETRY_DELAY), and a failed send in LOW_POWER waits
 * whichever of the two windows is longer. */
#define SERVER_NORMAL_MIN_SEND_INTERVAL_MS    1000u
#define SERVER_LOW_POWER_MIN_SEND_INTERVAL_MS 5000u
#define SERVER_SEND_RETRY_DELAY_MS            5000u

/* --- Message Envelope (docs/02-management-contracts/backend-contract-tcu-hekar.md
 * section 5, "Message Envelope" - explicitly [برداشت/پیشنهاد], not [قطعی]) ---
 * Version is the Envelope/Protocol version field, independent from the
 * Transport (MQTT) or Encoding (JSON) choice - both of those are still
 * [باز] per that same document's section 1. */
#define SERVER_PROTOCOL_VERSION 1u

/* TODO(open-item): the basis for Device Identity (IMEI / Serial Number /
 * dedicated Device Id) is still [باز] - OPEN-ITEMS.md, "ارتباط TCU <-> Backend
 * (هکار)" section, item 5. A fixed compile-time placeholder so the
 * Envelope/MQTT-Client-Id path is real and testable today, NOT a claim
 * this is the final Identity source - same status the SMS/Call alert
 * destination constants used to have before they migrated to
 * Configuration Infrastructure (see sms_management_config.h's and
 * call_management_config.h's own comments on that migration). */
#define SERVER_DEVICE_ID_PLACEHOLDER "TCU-000000"

/* MQTT Client Id passed to EC200U_MQTT_Connect(). backend-contract-tcu-hekar.md
 * never separates "Device Identity" from "MQTT Client Id" - both name the
 * same device to the Backend/Broker - so this reuses the placeholder above
 * instead of inventing a second, unrelated identifier. Also still gated by
 * the same open item (#5) and by Authentication (item #4, also [باز]: a
 * real deployment will likely need Client Id tied to whatever
 * Authentication scheme Simulation settles on). */
#define SERVER_MQTT_CLIENT_ID_PLACEHOLDER SERVER_DEVICE_ID_PLACEHOLDER

/* backend-contract-tcu-hekar.md section 6 ("Topic / آدرس‌دهی"): one fixed
 * Topic per device, with the message Type carried inside the Envelope
 * rather than via separate Sub-Topics - but section 6 is explicit that this
 * only applies "در صورت انتخاب نهایی MQTT", and OPEN-ITEMS.md item 10
 * ("Topic Structure نهایی") is still [باز] pending agreement with هکار.
 * Placeholder only, not a Contract. */
#define SERVER_MQTT_TOPIC_PLACEHOLDER "tcu/" SERVER_DEVICE_ID_PLACEHOLDER "/data"

/* --- هکار (Backend) environment IPs/Ports (backend-contract-tcu-hekar.md
 * section 6, table of known environments) ---
 * This is هکار's CURRENT infrastructure, explicitly NOT a final Firmware
 * Contract (section 6: "این اطلاعات زیرساخت فعلی هکار است، نه Contract نهایی
 * Firmware"). Reference/documentation only, kept here so whoever
 * provisions a device's EEPROM (Configuration_SetServerAddressSandbox() +
 * Configuration_SetServerPortSandbox(), infrastructure/configuration.h)
 * knows what real values هکار's Sandbox/Deployment/Operation environments
 * currently answer to - nothing in this file feeds these into Config
 * automatically (Configuration Infrastructure's own factory defaults are
 * deliberately generic placeholders, not this real infra data - see
 * configuration.c's LoadFactoryDefaults()).
 *
 * WIRED (no longer a TODO): EC200U_MQTT_Connect() now takes host/port
 * (firmware/include/infrastructure/ec200u_manager.h) and
 * mqtt_transport_connect()'s select_active_server_address() helper
 * (server_management.c) picks which of the three Address/Port pairs to
 * read based on Configuration_Get()->communication.active_environment
 * (infrastructure/configuration.h's ConfigServerEnvironment,
 * Configuration_SetActiveServerEnvironment()) - Sandbox is only the
 * factory-default RUNTIME value now (configuration.c's
 * LoadFactoryDefaults(), same "safest choice" reasoning as before), not a
 * compile-time constant; a device can be provisioned to Deployment/
 * Operation without a firmware rebuild. Reaching the specific IP/Port over
 * AT-Commands/Socket still stays entirely behind the outsourced EC200U
 * Component boundary (see the comment above the "MQTT Capability" block in
 * ec200u_manager.h) - this file/Server Management only ever hand the two
 * values over, never interpret them further. */
#define SERVER_HEKAR_SANDBOX_IP           "185.172.3.70"
#define SERVER_HEKAR_SANDBOX_MQTT_PORT    9095u

#define SERVER_HEKAR_DEPLOYMENT_IP        "95.38.182.115"
#define SERVER_HEKAR_DEPLOYMENT_MQTT_PORT 5016u

#define SERVER_HEKAR_OPERATION_IP         "5.160.52.66"
#define SERVER_HEKAR_OPERATION_MQTT_PORT  9096u

/* --- Binary Encoder framing (server_management.c) ---
 * docs/02-management-contracts/binary-protocol-final.md section 2/7 closed
 * the previously-[باز] Encoding decision to Binary (no JSON phase) - this
 * replaces the old SERVER_BASE64_MAX_BYTES/SERVER_JSON_ENCODE_BUFFER_BYTES.
 * SERVER_BINARY_HEADER_BYTES: fixed 8-byte common header (section 2 -
 * Version/MsgType/SeqId/Timestamp), identical for every message type.
 * SERVER_BINARY_CRC_BYTES: trailing CRC16-CCITT (section 7), 2 bytes.
 * SERVER_BINARY_FRAME_MAX_BYTES: header + the largest raw payload this
 * Management ever forwards (SERVER_MAX_ITEM_BYTES) + CRC - sized so a
 * full-size item's encoded frame never gets silently truncated. NOTE: the
 * document's own fixed-size Telemetry(32B)/Event(14B)/Response(14B) frame
 * LAYOUTS (exact field-by-field content, e.g. GPS lat/lon into the
 * Telemetry body) are NOT built by this Management - Server Management
 * still only carries whatever opaque payload bytes its caller already
 * built (server-management.md section 2/11: "مسئول معنی Payload نیست"),
 * same boundary the old JSON Encoder had. Only the wire ENVELOPE (Header +
 * payload + CRC) changed from JSON+Base64 to real Binary. */
#define SERVER_BINARY_HEADER_BYTES     8u
#define SERVER_BINARY_CRC_BYTES        2u

/* Largest single wire payload this Management must be able to buffer - the
 * generic SERVER_MAX_ITEM_BYTES above was never sized for
 * COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's own dedicated wire shape (domain +
 * expected_size + a URL - see server_management.c's FirmwareDownloadWirePayload
 * and shared/command_router.h's FIRMWARE_DOWNLOAD_URL_MAX_LENGTH, added once
 * FOTA-over-HTTP was confirmed, OPEN-ITEMS.md TCU<->Backend #12), so this
 * takes whichever of the two is actually larger instead of silently
 * truncating/overreading one of them. */
#define SERVER_FIRMWARE_DOWNLOAD_WIRE_PAYLOAD_BYTES (4u + 4u + FIRMWARE_DOWNLOAD_URL_MAX_LENGTH)
#define SERVER_BINARY_MAX_PAYLOAD_BYTES ((SERVER_FIRMWARE_DOWNLOAD_WIRE_PAYLOAD_BYTES > SERVER_MAX_ITEM_BYTES) \
    ? SERVER_FIRMWARE_DOWNLOAD_WIRE_PAYLOAD_BYTES : SERVER_MAX_ITEM_BYTES)
#define SERVER_BINARY_FRAME_MAX_BYTES (SERVER_BINARY_HEADER_BYTES + SERVER_BINARY_MAX_PAYLOAD_BYTES + SERVER_BINARY_CRC_BYTES)

#endif /* MGMT_SERVER_CONFIG_H */
