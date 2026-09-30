#ifndef INFRA_EC200U_MANAGER_CONFIG_H
#define INFRA_EC200U_MANAGER_CONFIG_H

/* Every tunable constant for EC200U Manager's AT Command engine and each
 * Capability lives in this one file, same convention as every other
 * Management's own *_config.h (e.g. hardware_input_management_config.h).
 * None of these are part of any formal Contract -
 * docs/03-infrastructure-contracts/ec200u-manager.md deliberately leaves
 * exact Timeout/Retry values as this layer's own internal design (section
 * 12: "Timeoutهای AT ... متعلق به EC200U Manager هستند"). Every value
 * below is a reasonable default taken from the Quectel EC200U AT Commands
 * Manual's own suggested ranges - NOT yet validated against real hardware
 * (see docs/04-open-items/OPEN-ITEMS.md). */

/* --- AT engine: per-command-family response timeouts --- */
#define AT_DEFAULT_TIMEOUT_MS    300u    /* plain local commands: AT, ATE0, AT+CPIN?, AT+CSQ, AT+CMGF, AT+CNMI, AT+CMGD, ATH, ATA */
#define AT_NETWORK_TIMEOUT_MS    5000u   /* AT+CREG?/AT+CGATT?/AT+QIACT= - may involve real network round trips */
#define AT_SMS_SEND_TIMEOUT_MS   15000u  /* AT+CMGS prompt phase + final result - the network SMSC round trip, not just the local module */
#define AT_CALL_DIAL_TIMEOUT_MS  3000u   /* ATD<number>; - local accept only, call progress itself is tracked via AT+CLCC polling/URCs afterward, not this timeout */
#define AT_GNSS_TIMEOUT_MS       2000u   /* AT+QGPS=.../AT+QGPSEND/AT+QGPSLOC= */
#define AT_MQTT_OPEN_TIMEOUT_MS  45000u  /* AT+QMTOPEN - TCP handshake over the cellular network, URC-driven (+QMTOPEN:) */
#define AT_MQTT_CONN_TIMEOUT_MS  15000u  /* AT+QMTCONN - MQTT CONNACK round trip, URC-driven (+QMTCONN:) */
#define AT_MQTT_SHORT_TIMEOUT_MS 8000u   /* AT+QMTSUB/QMTPUB/QMTDISC/QMTCLOSE - URC-driven (+QMTSUB:/+QMTPUB:/+QMTCLOSE:) */
#define AT_HTTP_URL_TIMEOUT_MS   5000u   /* AT+QHTTPURL - local prompt + URL upload only */
#define AT_HTTP_GET_TIMEOUT_MS   60000u  /* AT+QHTTPGET - the actual HTTP round trip, URC-driven (+QHTTPGET:) */
#define AT_HTTP_READ_TIMEOUT_MS  30000u  /* AT+QHTTPREAD - streams the already-fetched response body out of the module */

/* Local Recovery (ec200u-manager.md section 13): how many times a single
 * AT command is retried (fresh send, not just re-polling) before that
 * Capability's operation gives up. */
#define AT_MAX_RETRIES 3u

/* --- Module Init sequence pacing --- */
#define EC200U_INIT_POWERUP_SETTLE_MS 3000u /* time to let the module boot before the first "AT" liveness probe - EC200U datasheet typical ready time after power-on */
#define EC200U_INIT_PROBE_RETRY_MS    1000u /* how often to re-probe "AT" while the module has not answered yet - module Recovery (section 13), not a Fault until AT_MAX_RETRIES probes all fail */

/* --- Periodic status refresh while READY (Cellular) --- */
#define EC200U_STATUS_POLL_INTERVAL_MS 15000u /* AT+CREG?/AT+CSQ periodic re-check, so Network/Signal state is never more than this stale */

/* --- Voice Call state poll (AT+CLCC) while a call is DIALING/RINGING/ACTIVE - EC200U Manager has no other way to learn a Dial finished ringing and became ACTIVE, or that the far end hung up, without polling --- */
#define EC200U_CALL_POLL_INTERVAL_MS 1000u

/* --- GNSS position poll (AT+QGPSLOC) while a GNSS session is active --- */
#define EC200U_GNSS_POLL_INTERVAL_MS 2000u

/* --- Buffer sizes --- */
#define AT_LINE_MAX_BYTES      256u /* one AT response/URC line, CRLF stripped */
#define AT_COMMAND_MAX_BYTES   192u /* one formatted outbound AT command, CRLF included */
#define AT_INFO_LINE_MAX_COUNT 4u   /* non-final response lines captured per in-flight command (e.g. the single "+CSQ: 20,99" line before the closing OK) */

/* PDP Context / MQTT client index - single fixed values, this Capability
 * Contract only ever needs one Data connection and one MQTT client at a
 * time (server-management.md/backend-contract-tcu-hekar.md never describe
 * more than one Backend connection). No APN is configured anywhere yet
 * (Configuration Infrastructure has no field for it) - AT+QIACT is issued
 * without AT+QICSGP first, relying on the SIM/network's own default APN;
 * if a real SIM needs an explicit APN this must be added later, not
 * guessed here. */
#define EC200U_PDP_CONTEXT_ID 1u
#define EC200U_MQTT_CLIENT_ID 0u

#endif /* INFRA_EC200U_MANAGER_CONFIG_H */
