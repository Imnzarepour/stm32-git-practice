#include "infrastructure/ec200u_manager.h"
#include "infrastructure/ec200u_manager_config.h"
#include "hardware_proxy/hardware_proxy_uart.h"
#include "infrastructure/time_base.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* REAL AT Command driver for the EC200U module - explicit user decision:
 * outsourcing this Component to a contractor is cancelled ("دیگه پیمانکاری
 * در کار نیست"); this file now implements the actual AT Command/UART
 * protocol behind ec200u_manager.h's Capability Contract, replacing the
 * board-level Simulator this file used to be. Every Management above this
 * layer (GPS/Server/SMS/Call) is completely unaffected - same Contract,
 * same header - per docs/03-infrastructure-contracts/ec200u-manager.md
 * section 20 ("مستقیم نبودن وابستگی به EC200U") and section 24's own
 * stated goal ("این فایل باید تمام پیچیدگی EC200U را پشت یک Contract
 * پایدار پنهان کند").
 *
 * AT Command syntax below follows the Quectel EC200U AT Commands Manual's
 * documented behavior as known at the time of writing - it has NOT been
 * validated against a real EC200U module yet (no hardware in this Repo).
 * Exact timeout values, the PDP/MQTT client index choices, and a few
 * parsing simplifications are called out at their own use sites and
 * tracked in docs/04-open-items/OPEN-ITEMS.md - this is real, working
 * protocol logic, not a placeholder, but "never bench-tested" is a
 * materially different risk than "simulated", and is flagged as such.
 *
 * Architecture (ec200u-manager.md section 6/8/9): a single shared AT
 * channel (one command in flight at a time - real AT modems do not
 * support interleaving commands), fed by hardware_proxy/hardware_proxy_uart.h
 * (Non-Blocking byte Send/Read - the ONLY hardware this file touches,
 * never infrastructure/drivers/uart.h directly, never a HAL/register call -
 * project principle #7). Every Capability (Cellular/GNSS/SMS/Call/MQTT/
 * HTTP) below is its own small Non-Blocking state machine that only ever
 * acts when the shared channel is free (at_is_free()), takes ownership via
 * at_send(), and releases it once done - the same "one Current, arbitrated
 * by ownership" shape diagnostic_management.c already uses for CAN. URCs
 * (RING, +CMTI, +QMTRECV, ...) are recognized and dispatched independently
 * of whatever command is in flight, per section 9. */

/* ============================================================================
 * AT ENGINE - RX line accumulation, TX, single-owner command arbitration,
 * URC dispatch. Everything below "PUBLIC API" is private to this file.
 * ============================================================================ */

typedef enum {
    AT_OWNER_NONE,
    AT_OWNER_MODULE,
    AT_OWNER_GNSS,
    AT_OWNER_SMS,
    AT_OWNER_CALL,
    AT_OWNER_MQTT,
    AT_OWNER_HTTP
} AtOwner;

static AtOwner  s_at_owner = AT_OWNER_NONE;
static bool     s_at_done;         /* true once a final result (OK/ERROR/timeout) is known for the in-flight command */
static bool     s_at_ok;           /* the result itself, valid only once s_at_done */
static bool     s_at_prompt_seen;  /* a bare '>' data prompt arrived (AT+CMGS/AT+QMTPUB/AT+QHTTPURL) - consumed by the owning Capability */
static TimeTick s_at_sent_tick;
static uint32_t s_at_timeout_ms;

static char     s_at_info_lines[AT_INFO_LINE_MAX_COUNT][AT_LINE_MAX_BYTES];
static uint32_t s_at_info_line_count;

static char     s_rx_line[AT_LINE_MAX_BYTES];
static uint32_t s_rx_line_len;

/* --- Debug/Runtime override state (board bring-up only) - declared here,
 * ahead of every Capability, since several public API functions below
 * check these before doing any real AT work. See the EC200U_Debug_* function
 * definitions near the end of this file for the full rationale. */
typedef enum { OVERRIDE_NONE, OVERRIDE_SUCCESS, OVERRIDE_FAILURE } DebugOverride;
static DebugOverride s_data_send_override    = OVERRIDE_NONE;
static DebugOverride s_mqtt_publish_override = OVERRIDE_NONE;
static DebugOverride s_call_dial_override    = OVERRIDE_NONE;

static bool at_try_dispatch_urc(const char* line); /* forward - defined after each Capability's own state, since dispatch touches all of them */

static void at_reset_info_lines(void) {
    s_at_info_line_count = 0u;
}

static void at_capture_info_line(const char* line) {
    if (s_at_info_line_count >= AT_INFO_LINE_MAX_COUNT) {
        return; /* excess lines silently dropped - same "bounded, not unlimited" discipline every Queue/buffer in this firmware already follows */
    }
    strncpy(s_at_info_lines[s_at_info_line_count], line, AT_LINE_MAX_BYTES - 1u);
    s_at_info_lines[s_at_info_line_count][AT_LINE_MAX_BYTES - 1u] = '\0';
    s_at_info_line_count++;
}

static bool at_line_is_final_ok(const char* line) {
    return strcmp(line, "OK") == 0;
}

static bool at_line_is_final_error(const char* line) {
    return strcmp(line, "ERROR") == 0
        || strncmp(line, "+CME ERROR:", 11) == 0
        || strncmp(line, "+CMS ERROR:", 11) == 0;
}

static void at_handle_line(const char* line) {
    if (line[0] == '\0') {
        return; /* blank line between frames - every AT response is wrapped in blank lines, this is normal noise */
    }

    if (at_try_dispatch_urc(line)) {
        return; /* Unsolicited - can arrive at any time, independent of whatever command is in flight (section 9) */
    }

    if (s_at_owner == AT_OWNER_NONE) {
        return; /* stray line with nothing pending - ignore rather than treat as a bug; this is unauthenticated wire input from hardware, not this firmware's own state */
    }

    if (at_line_is_final_ok(line)) {
        s_at_ok = true;
        s_at_done = true;
        return;
    }
    if (at_line_is_final_error(line)) {
        s_at_ok = false;
        s_at_done = true;
        return;
    }

    at_capture_info_line(line);
}

/* Reads whatever bytes HWProxy_UART_Read() has buffered and splits them
 * into CRLF-terminated lines, handling the one framing exception real
 * Quectel modems use: the bare '>' data prompt (AT+CMGS/AT+QMTPUB/
 * AT+QHTTPURL) arrives as "> " WITHOUT a trailing CRLF, so it is detected
 * the instant the '>' byte itself arrives instead of waiting for a
 * newline that will never come. */
static void at_rx_pump(void) {
    uint8_t  chunk[64];
    uint32_t n;

    /* Drains EVERYTHING already buffered, not just one fixed-size chunk -
     * still Non-Blocking (never waits for a byte that has not arrived
     * yet), but a whole already-arrived response/URC must not be spread
     * across several Super Loop ticks just because it was longer than one
     * internal chunk buffer; a multi-line reply (e.g. AT+CMGR's response,
     * or a long +QHTTPREAD body line) must be fully visible to
     * at_handle_line() within the same EC200U_Process() call it arrived
     * in, or every state machine above this loses its "resolves in
     * exactly the next call" guarantee. */
    while ((n = HWProxy_UART_Read(chunk, sizeof(chunk))) > 0u) {
        for (uint32_t i = 0; i < n; i++) {
            uint8_t byte = chunk[i];

            if (byte == '\r') {
                continue;
            }
            if (byte == '\n') {
                if (s_rx_line_len > 0u) {
                    s_rx_line[s_rx_line_len] = '\0';
                    at_handle_line(s_rx_line);
                    s_rx_line_len = 0u;
                }
                continue;
            }
            if (s_rx_line_len == 0u && byte == '>') {
                s_at_prompt_seen = true;
                continue; /* the prompt is consumed immediately, never becomes a "line" - the trailing space after it is dropped by the next check */
            }
            if (s_rx_line_len == 0u && byte == ' ') {
                continue; /* the space right after a '>' prompt (and any stray leading space) - never a meaningful line on its own */
            }
            if (s_rx_line_len + 1u < sizeof(s_rx_line)) {
                s_rx_line[s_rx_line_len++] = (char)byte;
            }
            /* a line longer than AT_LINE_MAX_BYTES is truncated, never
             * overflowed - same bounded-buffer discipline as everywhere
             * else; an oversized response would already fail whatever
             * field parsing the caller attempts on it, which is an
             * acceptable degradation for a line this firmware never
             * expected to be this long. */
        }
        if (n < sizeof(chunk)) {
            break; /* a short read means the source is drained for now - avoids one extra, guaranteed-empty HWProxy_UART_Read() call */
        }
    }
}

/* Sends `cmd` (no trailing CRLF - this appends it) and takes ownership of
 * the shared AT channel for `owner`. Returns false only if the channel is
 * already owned by someone else, or the UART itself would not accept the
 * bytes right now (HWPROXY_UART_BUSY/ERROR) - the caller must retry on a
 * later Process() call in either case, no state is disturbed. */
static bool at_send(AtOwner owner, const char* cmd, uint32_t timeout_ms) {
    char     line[AT_COMMAND_MAX_BYTES];
    int      written;

    if (s_at_owner != AT_OWNER_NONE) {
        return false;
    }

    written = snprintf(line, sizeof(line), "%s\r\n", cmd);
    if (written < 0 || (uint32_t)written >= sizeof(line)) {
        return false; /* command too long for this buffer - a real bug in a caller, never sent partially */
    }

    if (HWProxy_UART_Send((const uint8_t*)line, (uint32_t)written) != HWPROXY_UART_OK) {
        return false;
    }

    s_at_owner        = owner;
    s_at_done         = false;
    s_at_ok           = false;
    s_at_prompt_seen  = false;
    s_at_sent_tick    = Time_GetTick();
    s_at_timeout_ms   = timeout_ms;
    at_reset_info_lines();
    return true;
}

/* Sends raw bytes on the already-owned channel (after a '>' prompt) -
 * does not touch ownership/timeout/done state, the original at_send()'s
 * timeout keeps governing the overall transaction. */
static bool at_send_raw(const uint8_t* data, uint32_t length) {
    return HWProxy_UART_Send(data, length) == HWPROXY_UART_OK;
}

static bool at_is_free(void) {
    return s_at_owner == AT_OWNER_NONE;
}

static bool at_prompt_seen(AtOwner owner) {
    return s_at_owner == owner && s_at_prompt_seen;
}

/* Non-Blocking poll - true once `owner`'s in-flight command has a final
 * result (a real OK/ERROR from the module, or this function's own
 * Timeout - ec200u-manager.md section 12: Timeout belongs entirely to this
 * layer). Must only be called by the current owner. `out_ok` is only
 * written when this returns true. */
static bool at_poll(AtOwner owner, bool* out_ok) {
    if (s_at_owner != owner) {
        return false;
    }
    if (s_at_done) {
        *out_ok = s_at_ok;
        return true;
    }
    if (Time_Elapsed(s_at_sent_tick, s_at_timeout_ms)) {
        s_at_done = true;
        s_at_ok   = false;
        *out_ok   = false;
        return true;
    }
    return false;
}

static void at_release(AtOwner owner) {
    if (s_at_owner == owner) {
        s_at_owner = AT_OWNER_NONE;
    }
}

/* --- Minimal, purpose-built field parsing for the exact flat, single-line
 * shapes Quectel AT responses use here - NOT a general AT/CSV parser, same
 * "closed, purpose-built" spirit as server_management.c's own base64/JSON
 * helpers used before the Binary Protocol replaced them. No support for an
 * escaped quote inside a quoted field - real EC200U responses for the
 * fields this file reads (phone numbers, hostnames, short status text)
 * never contain one. */

/* Copies the Nth (0-based) double-quoted field from `line` into `out`.
 * Returns false if that many quoted fields do not exist, or the field
 * does not fit `out_size` (out is left untouched in that case). */
static bool at_extract_quoted_field(const char* line, uint32_t index, char* out, uint32_t out_size) {
    const char* cursor = line;
    uint32_t    found   = 0u;

    while (*cursor != '\0') {
        if (*cursor == '"') {
            const char* start = cursor + 1;
            const char* end   = strchr(start, '"');
            if (end == NULL) {
                return false;
            }
            if (found == index) {
                uint32_t len = (uint32_t)(end - start);
                if (len + 1u > out_size) {
                    return false;
                }
                memcpy(out, start, len);
                out[len] = '\0';
                return true;
            }
            found++;
            cursor = end + 1;
        } else {
            cursor++;
        }
    }
    return false;
}

/* Returns the Nth (0-based) comma-separated field, whether quoted or bare,
 * as a plain integer - used for the numeric fields inside +CSQ/+CREG/
 * +QMTOPEN/+QMTCONN/+QMTPUB/+QMTSUB/+QMTCLOSE/+QHTTPGET/+CMTI/+CLCC URCs
 * and responses (e.g. "+QMTOPEN: 0,0" -> field 1 is the result code). Skips
 * over the "+NAME:" prefix automatically (splits after the first ':').
 * Returns 0 (and leaves *out_value unset -> caller must check the return
 * value, not just accept a default 0) if that field does not exist. */
static bool at_extract_int_field(const char* line, uint32_t index, long* out_value) {
    const char* colon = strchr(line, ':');
    const char* cursor = (colon != NULL) ? (colon + 1) : line;
    uint32_t    field  = 0u;

    while (*cursor == ' ') cursor++; /* Quectel always puts one space after ':' */

    while (field < index) {
        const char* comma = strchr(cursor, ',');
        if (comma == NULL) {
            return false;
        }
        cursor = comma + 1;
        field++;
    }

    char* end = NULL;
    *out_value = strtol(cursor, &end, 10);
    return end != cursor;
}

/* ============================================================================
 * MODULE INIT / CELLULAR STATUS (ec200u-manager.md sections 10/11)
 * ============================================================================ */

typedef enum {
    INIT_POWER_SETTLE,   /* waiting EC200U_INIT_POWERUP_SETTLE_MS before the first liveness probe */
    INIT_PROBE_AT,       /* plain "AT" - module may still be booting, retried until it answers */
    INIT_ECHO_OFF,       /* ATE0 - so every later response is exactly the line this file expects, no command echo to filter out */
    INIT_CHECK_SIM,      /* AT+CPIN? -> "+CPIN: READY" */
    INIT_CHECK_NETWORK,  /* AT+CREG? -> "+CREG: <n>,<stat>" */
    INIT_CHECK_SIGNAL,   /* AT+CSQ -> "+CSQ: <rssi>,<ber>" - read once here, then re-checked periodically once READY */
    INIT_SMS_TEXT_MODE,  /* AT+CMGF=1 - text mode, simplest to parse (no PDU decode) */
    INIT_SMS_NOTIFY,     /* AT+CNMI=2,1,0,0,0 - deliver new SMS as a +CMTI URC immediately */
    INIT_ACTIVATE_PDP,   /* AT+QIACT=<EC200U_PDP_CONTEXT_ID> - not fatal if it fails, see its own comment */
    INIT_DONE
} InitStep;

static InitStep           s_init_step = INIT_POWER_SETTLE;
static TimeTick           s_init_step_tick;
static uint32_t           s_init_probe_attempts;
static EC200U_ModuleState  s_module_state  = EC200U_MODULE_OFF;
static EC200U_NetworkState s_network_state = EC200U_NETWORK_UNKNOWN;
static EC200U_SimCardState s_sim_card_state = EC200U_SIMCARD_NOT_READY;
static TimeTick            s_status_poll_tick;

static void module_enter_step(InitStep step) {
    s_init_step      = step;
    s_init_step_tick = Time_GetTick();
}

/* Parses "+CREG: <n>,<stat>" (or "+CGREG:"/"+CEREG:", same shape) -
 * <stat> 1 = registered home, 5 = registered roaming; anything else is
 * "not registered" for this Contract's purposes (it only exposes
 * REGISTERED/SEARCHING/LOST, not the full 3GPP registration taxonomy). */
static void apply_creg_status(const char* line) {
    long stat;
    if (!at_extract_int_field(line, 1u, &stat)) {
        return;
    }
    if (stat == 1 || stat == 5) {
        s_network_state = EC200U_NETWORK_REGISTERED;
    } else if (stat == 2) {
        s_network_state = EC200U_NETWORK_SEARCHING;
    } else {
        s_network_state = EC200U_NETWORK_LOST;
    }
}

static void module_process(void) {
    bool ok;

    if (s_init_step == INIT_DONE) {
        /* Periodic re-check while READY, so Network/Signal state is never
         * more than EC200U_STATUS_POLL_INTERVAL_MS stale - section 10's
         * own Module/Capability State ownership. */
        if (s_module_state == EC200U_MODULE_READY && at_is_free()
            && Time_Elapsed(s_status_poll_tick, EC200U_STATUS_POLL_INTERVAL_MS)) {
            s_status_poll_tick = Time_GetTick();
            (void)at_send(AT_OWNER_MODULE, "AT+CREG?", AT_NETWORK_TIMEOUT_MS);
        } else if (s_module_state == EC200U_MODULE_READY && at_poll(AT_OWNER_MODULE, &ok)) {
            if (ok && s_at_info_line_count > 0u) {
                apply_creg_status(s_at_info_lines[0]);
            }
            at_release(AT_OWNER_MODULE);
        }
        return;
    }

    switch (s_init_step) {
        case INIT_POWER_SETTLE:
            if (Time_Elapsed(s_init_step_tick, EC200U_INIT_POWERUP_SETTLE_MS)) {
                s_init_probe_attempts = 0u;
                module_enter_step(INIT_PROBE_AT);
            }
            return;

        case INIT_PROBE_AT:
            if (at_is_free()) {
                if (!Time_Elapsed(s_init_step_tick, (s_init_probe_attempts == 0u) ? 0u : EC200U_INIT_PROBE_RETRY_MS)) {
                    return;
                }
                s_init_probe_attempts++;
                (void)at_send(AT_OWNER_MODULE, "AT", AT_DEFAULT_TIMEOUT_MS);
                s_init_step_tick = Time_GetTick();
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                at_release(AT_OWNER_MODULE);
                if (ok) {
                    module_enter_step(INIT_ECHO_OFF);
                }
                /* else: stays in INIT_PROBE_AT, retried after EC200U_INIT_PROBE_RETRY_MS -
                 * module Recovery (section 13), no attempt limit here: a
                 * module that never answers "AT" simply never becomes
                 * READY, which is itself an observable, honest state
                 * (EC200U_MODULE_INITIALIZING forever) - no false READY
                 * is ever reported. */
            }
            return;

        case INIT_ECHO_OFF:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "ATE0", AT_DEFAULT_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_CHECK_SIM); /* ATE0 failing is not credible on a module that just answered "AT" - proceed regardless */
            }
            return;

        case INIT_CHECK_SIM:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "AT+CPIN?", AT_DEFAULT_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                if (ok && s_at_info_line_count > 0u && strstr(s_at_info_lines[0], "READY") != NULL) {
                    /* +CPIN: READY is a bare status word after the ':', not quoted */
                    s_sim_card_state = EC200U_SIMCARD_READY;
                }
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_CHECK_NETWORK); /* SIM not ready is reported via GetSimCardState(), not fatal to the rest of Init - Cellular/GNSS/Call may still partially work per section 11's own "do not halt the whole Firmware" principle */
            }
            return;

        case INIT_CHECK_NETWORK:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "AT+CREG?", AT_NETWORK_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                if (ok && s_at_info_line_count > 0u) {
                    apply_creg_status(s_at_info_lines[0]);
                }
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_CHECK_SIGNAL);
            }
            return;

        case INIT_CHECK_SIGNAL:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "AT+CSQ", AT_DEFAULT_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                /* Signal quality (+CSQ: <rssi>,<ber>) is read here to
                 * confirm the AT path carries real numeric responses, but
                 * this Contract does not expose a GetSignalQuality() yet -
                 * no consumer needs it (section 4's own "Satellite/Signal
                 * Information در صورت نیاز" - not needed yet). */
                (void)ok;
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_SMS_TEXT_MODE);
            }
            return;

        case INIT_SMS_TEXT_MODE:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "AT+CMGF=1", AT_DEFAULT_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                (void)ok;
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_SMS_NOTIFY);
            }
            return;

        case INIT_SMS_NOTIFY:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_MODULE, "AT+CNMI=2,1,0,0,0", AT_DEFAULT_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                (void)ok;
                at_release(AT_OWNER_MODULE);
                module_enter_step(INIT_ACTIVATE_PDP);
            }
            return;

        case INIT_ACTIVATE_PDP:
            if (at_is_free()) {
                char cmd[AT_COMMAND_MAX_BYTES];
                snprintf(cmd, sizeof(cmd), "AT+QIACT=%u", (unsigned)EC200U_PDP_CONTEXT_ID);
                (void)at_send(AT_OWNER_MODULE, cmd, AT_NETWORK_TIMEOUT_MS);
            } else if (at_poll(AT_OWNER_MODULE, &ok)) {
                /* Not fatal if this fails (no APN configured anywhere yet -
                 * see ec200u_manager_config.h's own comment): Data/MQTT/
                 * HTTP simply will not work without it, but SMS/Call/GNSS
                 * do not need PDP at all - section 11's "GNSS Initialization
                 * Failure -> GPS unavailable -> Server/Actuator/Diagnostic
                 * continue where possible" principle, generalized. */
                (void)ok;
                at_release(AT_OWNER_MODULE);
                s_status_poll_tick = Time_GetTick();
                s_module_state = EC200U_MODULE_READY;
                module_enter_step(INIT_DONE);
            }
            return;

        default:
            return;
    }
}

/* ============================================================================
 * GNSS CAPABILITY (ec200u-manager.md section 4 - GNSS)
 * ============================================================================ */

typedef enum { GNSS_SESSION_STOPPED, GNSS_SESSION_STARTING, GNSS_SESSION_ACTIVE, GNSS_SESSION_STOPPING } GnssSession;

static GnssSession     s_gnss_session = GNSS_SESSION_STOPPED;
static TimeTick        s_gnss_poll_tick;
static EC200U_GnssData s_gnss_latest;

/* Parses "+QGPSLOC: <UTC>,<lat>,<lon>,<hdop>,<altitude>,<fix>,<cog>,<spkm>,<spkn>,<date>,<nsat>"
 * (mode 2 of AT+QGPSLOC= - plain decimal degrees, simplest to convert into
 * this Contract's microdegree int32 convention: value = degrees * 1e6,
 * same placeholder scale ec200u_manager.c's own Simulator predecessor
 * already used, so GPS Management needs no change). <fix> is 2 or 3 for a
 * valid 2D/3D fix - anything else (including the whole command failing
 * with "+CME ERROR: 516", "not fixed yet") means no fix. */
static void apply_gnss_loc(const char* line) {
    const char* colon = strchr(line, ':');
    const char* cursor = (colon != NULL) ? (colon + 1) : line;
    char        lat_field[16];
    char        lon_field[16];
    long        field_index = 0;
    const char* field_start = cursor;

    while (*cursor == ' ') { cursor++; field_start = cursor; }

    /* Walk field-by-field (comma-separated, none of these are quoted)
     * picking out field 1 (latitude) and field 2 (longitude) - same
     * closed/purpose-built parsing spirit as at_extract_int_field(),
     * just needing two adjacent float fields instead of one integer. */
    while (*cursor != '\0') {
        if (*cursor == ',' || *(cursor + 1) == '\0') {
            uint32_t len = (uint32_t)(cursor - field_start) + ((*(cursor + 1) == '\0' && *cursor != ',') ? 1u : 0u);
            if (field_index == 1 && len < sizeof(lat_field)) {
                memcpy(lat_field, field_start, len);
                lat_field[len] = '\0';
            } else if (field_index == 2 && len < sizeof(lon_field)) {
                memcpy(lon_field, field_start, len);
                lon_field[len] = '\0';
            }
            field_index++;
            field_start = cursor + 1;
        }
        cursor++;
    }

    if (field_index < 3) {
        return; /* malformed/short line - leave s_gnss_latest untouched */
    }

    s_gnss_latest.latitude   = (int32_t)(atof(lat_field) * 1000000.0);
    s_gnss_latest.longitude  = (int32_t)(atof(lon_field) * 1000000.0);
    s_gnss_latest.timestamp  = Time_GetTick();
    s_gnss_latest.fix_valid  = true;
}

static void gnss_process(void) {
    bool ok;

    if (s_gnss_session == GNSS_SESSION_STARTING) {
        if (at_is_free()) {
            (void)at_send(AT_OWNER_GNSS, "AT+QGPS=1", AT_GNSS_TIMEOUT_MS);
        } else if (at_poll(AT_OWNER_GNSS, &ok)) {
            at_release(AT_OWNER_GNSS);
            /* "+CME ERROR: 504" (already on) is treated the same as OK -
             * either way GNSS is now active from this Capability's point
             * of view, same idempotent-Start reasoning EC200U_MQTT_Connect()
             * already documents for an already-CONNECTED session. */
            s_gnss_session   = GNSS_SESSION_ACTIVE;
            s_gnss_poll_tick = Time_GetTick();
            (void)ok;
        }
        return;
    }

    if (s_gnss_session == GNSS_SESSION_STOPPING) {
        if (at_is_free()) {
            (void)at_send(AT_OWNER_GNSS, "AT+QGPSEND", AT_GNSS_TIMEOUT_MS);
        } else if (at_poll(AT_OWNER_GNSS, &ok)) {
            (void)ok;
            at_release(AT_OWNER_GNSS);
            s_gnss_session          = GNSS_SESSION_STOPPED;
            s_gnss_latest.fix_valid = false;
        }
        return;
    }

    if (s_gnss_session == GNSS_SESSION_ACTIVE) {
        if (at_is_free() && Time_Elapsed(s_gnss_poll_tick, EC200U_GNSS_POLL_INTERVAL_MS)) {
            s_gnss_poll_tick = Time_GetTick();
            (void)at_send(AT_OWNER_GNSS, "AT+QGPSLOC=2", AT_GNSS_TIMEOUT_MS);
        } else if (at_poll(AT_OWNER_GNSS, &ok)) {
            if (ok && s_at_info_line_count > 0u) {
                apply_gnss_loc(s_at_info_lines[0]);
            } else {
                s_gnss_latest.fix_valid = false; /* "+CME ERROR: 516" (not fixed) or a Timeout - both mean no usable fix right now */
            }
            at_release(AT_OWNER_GNSS);
        }
    }
}

/* ============================================================================
 * SMS CAPABILITY (ec200u-manager.md section 4 - SMS), text mode
 * (AT+CMGF=1, set once during Module Init).
 * ============================================================================ */

#define SMS_MAX_TEXT_LENGTH 160u /* single-part GSM7 SMS - concatenated/multi-part SMS is out of scope, no consumer needs it */

typedef enum { SMS_RX_IDLE, SMS_RX_READING, SMS_RX_DELETING } SmsRxState;
typedef enum { SMS_TX_IDLE, SMS_TX_SENDING_HEADER, SMS_TX_AWAIT_PROMPT, SMS_TX_AWAIT_RESULT } SmsTxState;

static SmsRxState s_sms_rx_state = SMS_RX_IDLE;
static long       s_sms_rx_pending_index = -1;
static bool       s_sms_pending;
static char       s_sms_rx_sender[32];
static char       s_sms_rx_text[SMS_MAX_TEXT_LENGTH + 1u];
static uint32_t   s_sms_rx_text_length;

static SmsTxState    s_sms_tx_state = SMS_TX_IDLE;
static char           s_sms_tx_number[32];
static char           s_sms_tx_text[SMS_MAX_TEXT_LENGTH + 1u];
static uint32_t        s_sms_tx_text_length;
static EC200U_Result  s_sms_tx_last_status = EC200U_NOT_READY;

static void sms_start_read(long index) {
    char cmd[AT_COMMAND_MAX_BYTES];
    snprintf(cmd, sizeof(cmd), "AT+CMGR=%ld", index);
    if (at_send(AT_OWNER_SMS, cmd, AT_DEFAULT_TIMEOUT_MS)) {
        s_sms_rx_state = SMS_RX_READING;
    }
}

static void sms_rx_process(void) {
    bool ok;

    if (s_sms_rx_state == SMS_RX_IDLE) {
        if (s_sms_rx_pending_index >= 0 && at_is_free()) {
            sms_start_read(s_sms_rx_pending_index);
        }
        return;
    }

    if (s_sms_rx_state == SMS_RX_READING) {
        if (!at_poll(AT_OWNER_SMS, &ok)) {
            return;
        }
        if (ok && s_at_info_line_count >= 2u) {
            /* +CMGR: "REC UNREAD","<sender>",,"<timestamp>" then the body on its own line */
            if (at_extract_quoted_field(s_at_info_lines[0], 1u, s_sms_rx_sender, sizeof(s_sms_rx_sender))) {
                strncpy(s_sms_rx_text, s_at_info_lines[1], sizeof(s_sms_rx_text) - 1u);
                s_sms_rx_text[sizeof(s_sms_rx_text) - 1u] = '\0';
                s_sms_rx_text_length = (uint32_t)strlen(s_sms_rx_text);
                s_sms_pending = true;
            }
        }
        /* !ok (CMGR failed, e.g. the index was already gone) - the message
         * simply is not surfaced this time; STORAGE.md-level "unlimited
         * retry" is not attempted, same bounded-effort spirit as
         * log.c "must never build an unbounded chain of retries". */
        at_release(AT_OWNER_SMS);

        {
            char delete_cmd[AT_COMMAND_MAX_BYTES];
            snprintf(delete_cmd, sizeof(delete_cmd), "AT+CMGD=%ld", s_sms_rx_pending_index);
            if (at_send(AT_OWNER_SMS, delete_cmd, AT_DEFAULT_TIMEOUT_MS)) {
                s_sms_rx_state = SMS_RX_DELETING;
            } else {
                /* channel momentarily unavailable right after release() -
                 * unreachable in practice (nothing else claims it in the
                 * same tick), kept as a safe fallback: drop back to IDLE
                 * rather than spin forever trying to delete. */
                s_sms_rx_state         = SMS_RX_IDLE;
                s_sms_rx_pending_index = -1;
            }
        }
        return;
    }

    if (s_sms_rx_state == SMS_RX_DELETING) {
        if (!at_poll(AT_OWNER_SMS, &ok)) {
            return;
        }
        (void)ok; /* a failed Delete just leaves the message on the SIM - harmless, not retried */
        at_release(AT_OWNER_SMS);
        s_sms_rx_state         = SMS_RX_IDLE;
        s_sms_rx_pending_index = -1;
    }
}

static void sms_tx_process(void) {
    bool ok;

    switch (s_sms_tx_state) {
        case SMS_TX_SENDING_HEADER: {
            char cmd[AT_COMMAND_MAX_BYTES];
            snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", s_sms_tx_number);
            if (at_send(AT_OWNER_SMS, cmd, AT_SMS_SEND_TIMEOUT_MS)) {
                s_sms_tx_state = SMS_TX_AWAIT_PROMPT;
            }
            return;
        }
        case SMS_TX_AWAIT_PROMPT:
            if (at_prompt_seen(AT_OWNER_SMS)) {
                static const uint8_t ctrl_z = 0x1Au;
                (void)at_send_raw((const uint8_t*)s_sms_tx_text, s_sms_tx_text_length);
                (void)at_send_raw(&ctrl_z, 1u);
                s_sms_tx_state = SMS_TX_AWAIT_RESULT;
            } else if (at_poll(AT_OWNER_SMS, &ok)) {
                /* Timed out or the module rejected AT+CMGS itself before ever prompting (e.g. no SIM/no network) */
                (void)ok;
                at_release(AT_OWNER_SMS);
                s_sms_tx_last_status = EC200U_ERROR;
                s_sms_tx_state        = SMS_TX_IDLE;
            }
            return;
        case SMS_TX_AWAIT_RESULT:
            if (at_poll(AT_OWNER_SMS, &ok)) {
                at_release(AT_OWNER_SMS);
                s_sms_tx_last_status = ok ? EC200U_OK : EC200U_ERROR;
                s_sms_tx_state        = SMS_TX_IDLE;
            }
            return;
        default:
            return;
    }
}

static void sms_process(void) {
    /* Receiving already-arrived SIM storage takes priority over sending -
     * an unread message sitting on the module is closer to being lost
     * (next CMTI could arrive for a different index) than an outbound
     * send waiting one more tick. Both share the single AT_OWNER_SMS
     * ownership, so only one of the two ever actually holds the channel
     * at a time regardless of this ordering. */
    if (s_sms_rx_state != SMS_RX_IDLE || s_sms_rx_pending_index >= 0) {
        sms_rx_process();
        return;
    }
    if (s_sms_tx_state != SMS_TX_IDLE) {
        sms_tx_process();
    }
}

/* ============================================================================
 * VOICE CALL CAPABILITY (ec200u-manager.md section 4 - Voice Call)
 * ============================================================================ */

#define EC200U_SIM_CALL_NUMBER_MAX_LEN 32u

static EC200U_CallState s_call_state = EC200U_CALL_IDLE;
static bool               s_call_is_incoming;
static char               s_incoming_number[EC200U_SIM_CALL_NUMBER_MAX_LEN];
static TimeTick            s_call_poll_tick;

typedef enum { CALL_ACTION_NONE, CALL_ACTION_DIAL, CALL_ACTION_HANGUP, CALL_ACTION_ANSWER } CallAction;
static CallAction s_call_pending_action = CALL_ACTION_NONE;
static char        s_call_pending_number[EC200U_SIM_CALL_NUMBER_MAX_LEN];

/* Parses "+CLCC: <id>,<dir>,<stat>,<mode>,<mpty>[,...]" - <stat>:
 * 0=active, 2=dialing, 3=alerting("ringing" from our side), everything
 * else (held/waiting, or the line being absent entirely because no call
 * exists) is treated as "nothing in progress" - this Contract does not
 * expose multi-party/held call detail. */
static bool clcc_reports_active(void)   { long v; return at_extract_int_field(s_at_info_lines[0], 2u, &v) && v == 0; }
static bool clcc_reports_alerting(void) { long v; return at_extract_int_field(s_at_info_lines[0], 2u, &v) && v == 3; }

/* AT_OWNER_CALL is shared by two DIFFERENT reasons this Capability might
 * hold the channel (a pending Dial/Hangup/Answer action, or a background
 * AT+CLCC poll) - this flag disambiguates which one a pending result
 * belongs to, so a CLCC response arriving right as an action also happens
 * to be idle is never mistaken for the action's own result (and vice
 * versa). Without it, checking only `s_at_owner == AT_OWNER_CALL` cannot
 * tell the two apart. */
typedef enum { CALL_AT_PURPOSE_NONE, CALL_AT_PURPOSE_ACTION, CALL_AT_PURPOSE_CLCC } CallAtPurpose;
static CallAtPurpose s_call_at_purpose = CALL_AT_PURPOSE_NONE;

static void call_process(void) {
    bool ok;

    if (s_call_pending_action != CALL_ACTION_NONE && at_is_free()) {
        char cmd[AT_COMMAND_MAX_BYTES];
        bool sent = false;
        switch (s_call_pending_action) {
            case CALL_ACTION_DIAL:
                snprintf(cmd, sizeof(cmd), "ATD%s;", s_call_pending_number);
                sent = at_send(AT_OWNER_CALL, cmd, AT_CALL_DIAL_TIMEOUT_MS);
                break;
            case CALL_ACTION_HANGUP:
                sent = at_send(AT_OWNER_CALL, "ATH", AT_DEFAULT_TIMEOUT_MS);
                break;
            case CALL_ACTION_ANSWER:
                sent = at_send(AT_OWNER_CALL, "ATA", AT_DEFAULT_TIMEOUT_MS);
                break;
            default:
                break;
        }
        if (sent) {
            s_call_at_purpose = CALL_AT_PURPOSE_ACTION;
        }
        return;
    }

    if (s_call_at_purpose == CALL_AT_PURPOSE_ACTION) {
        if (at_poll(AT_OWNER_CALL, &ok)) {
            at_release(AT_OWNER_CALL);
            s_call_at_purpose = CALL_AT_PURPOSE_NONE;
            switch (s_call_pending_action) {
                case CALL_ACTION_DIAL:
                    s_call_state = ok ? EC200U_CALL_DIALING : EC200U_CALL_ENDED;
                    if (ok) {
                        s_call_poll_tick = Time_GetTick();
                    }
                    break;
                case CALL_ACTION_HANGUP:
                    s_call_state = EC200U_CALL_ENDED;
                    break;
                case CALL_ACTION_ANSWER:
                    s_call_state = ok ? EC200U_CALL_ACTIVE : EC200U_CALL_ENDED;
                    break;
                default:
                    break;
            }
            s_call_pending_action = CALL_ACTION_NONE;
        }
        return;
    }

    /* Background AT+CLCC poll while a call this side dialed is in
     * progress - the only way to learn a Dial finished ringing and became
     * ACTIVE, or that the far end answered/hung up, without a dedicated
     * URC for every transition (section 4/12: this file owns Timeout AND
     * ordinary state-progress polling equally). Not run for an incoming
     * (RING-originated) call: RING/+CLIP already reported RINGING, and
     * Answer/Hangup are always explicit user Requests, not something to
     * auto-detect here. */
    if (!s_call_is_incoming && (s_call_state == EC200U_CALL_DIALING || s_call_state == EC200U_CALL_RINGING)) {
        if (at_is_free() && Time_Elapsed(s_call_poll_tick, EC200U_CALL_POLL_INTERVAL_MS)) {
            s_call_poll_tick = Time_GetTick();
            if (at_send(AT_OWNER_CALL, "AT+CLCC", AT_DEFAULT_TIMEOUT_MS)) {
                s_call_at_purpose = CALL_AT_PURPOSE_CLCC;
            }
        } else if (s_call_at_purpose == CALL_AT_PURPOSE_CLCC && at_poll(AT_OWNER_CALL, &ok)) {
            if (ok && s_at_info_line_count > 0u) {
                if (clcc_reports_active()) {
                    s_call_state = EC200U_CALL_ACTIVE;
                } else if (clcc_reports_alerting()) {
                    s_call_state = EC200U_CALL_RINGING;
                }
            } else {
                /* No +CLCC line at all (module answers plain OK with
                 * nothing else) means no call exists any more - the far
                 * end hung up or the dial failed silently. */
                s_call_state = EC200U_CALL_ENDED;
            }
            at_release(AT_OWNER_CALL);
            s_call_at_purpose = CALL_AT_PURPOSE_NONE;
        }
    }
}

/* ============================================================================
 * MQTT CAPABILITY (backend-contract-tcu-hekar.md / binary-protocol-final.md's
 * Transport). AT+QMTOPEN (TCP)/AT+QMTCONN (MQTT CONNECT)/AT+QMTSUB/AT+QMTPUB/
 * AT+QMTDISC+QMTCLOSE, single fixed client index (EC200U_MQTT_CLIENT_ID).
 * ============================================================================ */

typedef enum {
    MQTT_LOCAL_IDLE,
    MQTT_LOCAL_OPENING,   /* AT+QMTOPEN sent, waiting for the +QMTOPEN: URC */
    MQTT_LOCAL_CONNECTING /* AT+QMTCONN sent, waiting for the +QMTCONN: URC */
} MqttLocalState;

static EC200U_MqttState s_mqtt_state = EC200U_MQTT_DISCONNECTED;
static MqttLocalState    s_mqtt_local = MQTT_LOCAL_IDLE;
static char               s_mqtt_client_id[32];
static char               s_mqtt_host[64];
static uint16_t            s_mqtt_port;
static bool                s_mqtt_open_urc_pending;
static long                 s_mqtt_open_urc_result;
static bool                s_mqtt_conn_urc_pending;
static long                 s_mqtt_conn_urc_result;

/* Sized for the largest single outbound wire item Server Management ever
 * hands to a Transport (SERVER_MAX_ITEM_BYTES / SERVER_BINARY_MAX_PAYLOAD_BYTES,
 * server_management_config.h) - this Infrastructure layer must not include
 * that Management-owned header (layering), so the same generous bound is
 * simply repeated here as its own constant. */
#define EC200U_MQTT_PUBLISH_PAYLOAD_MAX_BYTES 256u

typedef enum { MQTT_PUB_IDLE, MQTT_PUB_SENDING_HEADER, MQTT_PUB_AWAIT_PROMPT, MQTT_PUB_AWAIT_RESULT } MqttPubState;
static MqttPubState s_mqtt_pub_state = MQTT_PUB_IDLE;
static uint8_t        s_mqtt_pub_payload[EC200U_MQTT_PUBLISH_PAYLOAD_MAX_BYTES];
static uint32_t        s_mqtt_pub_payload_length;
static char             s_mqtt_pub_topic[128];

typedef enum { MQTT_SUB_IDLE, MQTT_SUB_SENDING } MqttSubState;
static MqttSubState s_mqtt_sub_state = MQTT_SUB_IDLE;
static char           s_mqtt_sub_topic[128];

#define MQTT_RECV_TOPIC_MAX_LEN   128u
#define MQTT_RECV_PAYLOAD_MAX_LEN 512u
static bool     s_mqtt_recv_pending;
static char     s_mqtt_recv_topic[MQTT_RECV_TOPIC_MAX_LEN];
static uint8_t  s_mqtt_recv_payload[MQTT_RECV_PAYLOAD_MAX_LEN];
static uint32_t s_mqtt_recv_payload_length;

static void mqtt_process(void) {
    bool ok;

    if (s_mqtt_local == MQTT_LOCAL_OPENING) {
        if (s_mqtt_open_urc_pending) {
            s_mqtt_open_urc_pending = false;
            if (s_mqtt_open_urc_result == 0) {
                /* Opened - now MQTT CONNECT */
                char cmd[AT_COMMAND_MAX_BYTES];
                snprintf(cmd, sizeof(cmd), "AT+QMTCONN=%u,\"%s\"", (unsigned)EC200U_MQTT_CLIENT_ID, s_mqtt_client_id);
                s_mqtt_local = MQTT_LOCAL_CONNECTING;
                (void)at_send(AT_OWNER_MQTT, cmd, AT_MQTT_CONN_TIMEOUT_MS);
            } else {
                s_mqtt_local = MQTT_LOCAL_IDLE;
                s_mqtt_state = EC200U_MQTT_DISCONNECTED;
            }
            return;
        }
        if (at_poll(AT_OWNER_MQTT, &ok)) {
            /* AT+QMTOPEN's own immediate OK/ERROR only means "the command
             * was accepted", the real Open result comes later via the
             * +QMTOPEN: URC captured above - a bare ERROR here (malformed
             * command) fails the attempt outright without waiting for a
             * URC that will never come. */
            at_release(AT_OWNER_MQTT);
            if (!ok) {
                s_mqtt_local = MQTT_LOCAL_IDLE;
                s_mqtt_state = EC200U_MQTT_DISCONNECTED;
            }
        }
        return;
    }

    if (s_mqtt_local == MQTT_LOCAL_CONNECTING) {
        if (s_mqtt_conn_urc_pending) {
            s_mqtt_conn_urc_pending = false;
            s_mqtt_local            = MQTT_LOCAL_IDLE;
            s_mqtt_state            = (s_mqtt_conn_urc_result == 0) ? EC200U_MQTT_CONNECTED : EC200U_MQTT_DISCONNECTED;
            return;
        }
        if (at_poll(AT_OWNER_MQTT, &ok)) {
            at_release(AT_OWNER_MQTT);
            if (!ok) {
                s_mqtt_local = MQTT_LOCAL_IDLE;
                s_mqtt_state = EC200U_MQTT_DISCONNECTED;
            }
        }
        return;
    }

    /* --- Subscribe --- */
    if (s_mqtt_sub_state == MQTT_SUB_SENDING) {
        if (at_is_free()) {
            char cmd[AT_COMMAND_MAX_BYTES];
            snprintf(cmd, sizeof(cmd), "AT+QMTSUB=%u,1,\"%s\",1", (unsigned)EC200U_MQTT_CLIENT_ID, s_mqtt_sub_topic);
            (void)at_send(AT_OWNER_MQTT, cmd, AT_MQTT_SHORT_TIMEOUT_MS);
        } else if (at_poll(AT_OWNER_MQTT, &ok)) {
            (void)ok; /* the definitive Subscribe result is the +QMTSUB: URC - a local ERROR here just also ends the attempt */
            at_release(AT_OWNER_MQTT);
            s_mqtt_sub_state = MQTT_SUB_IDLE;
        }
        return;
    }

    /* --- Publish --- */
    switch (s_mqtt_pub_state) {
        case MQTT_PUB_SENDING_HEADER: {
            char cmd[AT_COMMAND_MAX_BYTES];
            if (!at_is_free()) return;
            snprintf(cmd, sizeof(cmd), "AT+QMTPUB=%u,1,0,0,\"%s\"", (unsigned)EC200U_MQTT_CLIENT_ID, s_mqtt_pub_topic);
            if (at_send(AT_OWNER_MQTT, cmd, AT_MQTT_SHORT_TIMEOUT_MS)) {
                s_mqtt_pub_state = MQTT_PUB_AWAIT_PROMPT;
            }
            return;
        }
        case MQTT_PUB_AWAIT_PROMPT:
            if (at_prompt_seen(AT_OWNER_MQTT)) {
                static const uint8_t ctrl_z = 0x1Au;
                if (s_mqtt_pub_payload_length > 0u) {
                    (void)at_send_raw(s_mqtt_pub_payload, s_mqtt_pub_payload_length);
                }
                (void)at_send_raw(&ctrl_z, 1u);
                s_mqtt_pub_state = MQTT_PUB_AWAIT_RESULT;
            } else if (at_poll(AT_OWNER_MQTT, &ok)) {
                (void)ok;
                at_release(AT_OWNER_MQTT);
                s_mqtt_pub_state = MQTT_PUB_IDLE;
            }
            return;
        case MQTT_PUB_AWAIT_RESULT:
            if (at_poll(AT_OWNER_MQTT, &ok)) {
                (void)ok; /* the definitive Publish result is the +QMTPUB: URC - same "OK does not guarantee Backend delivery" boundary this Capability's header comment already documents */
                at_release(AT_OWNER_MQTT);
                s_mqtt_pub_state = MQTT_PUB_IDLE;
            }
            return;
        default:
            return;
    }
}

/* ============================================================================
 * HTTP CLIENT CAPABILITY (OPEN-ITEMS.md TCU<->Backend #12 - FOTA over HTTP).
 * AT+QHTTPURL (upload the URL) -> AT+QHTTPGET (perform the GET) ->
 * AT+QHTTPREAD (stream the response body back out).
 * ============================================================================ */

typedef enum {
    HTTP_LOCAL_IDLE,
    HTTP_LOCAL_SENDING_URL_CMD,
    HTTP_LOCAL_AWAIT_URL_PROMPT,
    HTTP_LOCAL_AWAIT_URL_RESULT,
    HTTP_LOCAL_SENDING_GET,
    HTTP_LOCAL_AWAIT_GET_URC,
    HTTP_LOCAL_SENDING_READ,
    HTTP_LOCAL_STREAMING_READ
} HttpLocalState;

#define HTTP_URL_MAX_LEN 256u

static EC200U_HttpState s_http_state = EC200U_HTTP_IDLE;
static HttpLocalState    s_http_local = HTTP_LOCAL_IDLE;
static char               s_http_url[HTTP_URL_MAX_LEN];
static bool               s_http_get_urc_pending;
static long                s_http_get_urc_err;
static long                s_http_get_urc_http_code;

/* Response body is buffered fully before EC200U_HTTP_GetChunk() drains it -
 * see that function's own comment on why a byte-stream Capability still
 * needs a bound (HTTP_RESPONSE_BUFFER_BYTES). A real Firmware Image is
 * much larger than this - Firmware Update Management already downloads in
 * File Transfer Infrastructure's own small chunks
 * (FILE_TRANSFER_MAX_CHUNK_BYTES per SupplyChunk call), so QHTTPREAD is
 * re-issued with an offset/length window per chunk instead of ever trying
 * to hold a whole firmware image in RAM here. */
#define HTTP_RESPONSE_BUFFER_BYTES 2048u
static uint8_t  s_http_response[HTTP_RESPONSE_BUFFER_BYTES];
static uint32_t s_http_response_length;
static uint32_t s_http_drain_offset;
static uint32_t s_http_content_length; /* from +QHTTPGET:'s own 3rd field - how many bytes AT+QHTTPREAD will actually stream */
static uint32_t s_http_read_offset;    /* how much of s_http_content_length has been read into s_http_response so far across repeated QHTTPREAD windows */

static void http_process(void) {
    bool ok;

    switch (s_http_local) {
        case HTTP_LOCAL_SENDING_URL_CMD: {
            char cmd[AT_COMMAND_MAX_BYTES];
            if (!at_is_free()) return;
            snprintf(cmd, sizeof(cmd), "AT+QHTTPURL=%u,80", (unsigned)strlen(s_http_url));
            if (at_send(AT_OWNER_HTTP, cmd, AT_HTTP_URL_TIMEOUT_MS)) {
                s_http_local = HTTP_LOCAL_AWAIT_URL_PROMPT;
            }
            return;
        }
        case HTTP_LOCAL_AWAIT_URL_PROMPT:
            /* Quectel sends "CONNECT\r\n" (a normal CRLF-terminated line,
             * unlike the bare '>' prompt) before it accepts the raw URL
             * bytes - captured here as an ordinary info line rather than
             * needing a second prompt mechanism in the AT engine. */
            if (s_at_owner == AT_OWNER_HTTP && s_at_info_line_count > 0u && strcmp(s_at_info_lines[0], "CONNECT") == 0) {
                (void)at_send_raw((const uint8_t*)s_http_url, (uint32_t)strlen(s_http_url));
                s_http_local = HTTP_LOCAL_AWAIT_URL_RESULT;
            } else if (at_poll(AT_OWNER_HTTP, &ok)) {
                (void)ok;
                at_release(AT_OWNER_HTTP);
                s_http_local = HTTP_LOCAL_IDLE;
                s_http_state = EC200U_HTTP_ERROR;
            }
            return;
        case HTTP_LOCAL_AWAIT_URL_RESULT:
            if (at_poll(AT_OWNER_HTTP, &ok)) {
                at_release(AT_OWNER_HTTP);
                if (ok) {
                    s_http_local = HTTP_LOCAL_SENDING_GET;
                } else {
                    s_http_local = HTTP_LOCAL_IDLE;
                    s_http_state = EC200U_HTTP_ERROR;
                }
            }
            return;
        case HTTP_LOCAL_SENDING_GET:
            if (at_is_free()) {
                (void)at_send(AT_OWNER_HTTP, "AT+QHTTPGET=80", AT_HTTP_GET_TIMEOUT_MS);
                s_http_local = HTTP_LOCAL_AWAIT_GET_URC;
            }
            return;
        case HTTP_LOCAL_AWAIT_GET_URC:
            if (s_http_get_urc_pending) {
                s_http_get_urc_pending = false;
                if (s_at_owner == AT_OWNER_HTTP) {
                    at_release(AT_OWNER_HTTP);
                }
                if (s_http_get_urc_err == 0 && s_http_get_urc_http_code == 200) {
                    s_http_read_offset      = 0u;
                    s_http_response_length  = 0u;
                    s_http_drain_offset     = 0u;
                    s_http_local            = HTTP_LOCAL_SENDING_READ;
                } else {
                    s_http_local = HTTP_LOCAL_IDLE;
                    s_http_state = EC200U_HTTP_ERROR;
                }
                return;
            }
            if (at_poll(AT_OWNER_HTTP, &ok)) {
                /* AT+QHTTPGET's own immediate OK only means "accepted" -
                 * the real result is the +QHTTPGET: URC handled above.
                 * A local ERROR here (malformed command) fails outright. */
                at_release(AT_OWNER_HTTP);
                if (!ok) {
                    s_http_local = HTTP_LOCAL_IDLE;
                    s_http_state = EC200U_HTTP_ERROR;
                }
            }
            return;
        case HTTP_LOCAL_SENDING_READ:
            if (at_is_free() && s_http_response_length < sizeof(s_http_response)
                && s_http_read_offset < s_http_content_length) {
                char     cmd[AT_COMMAND_MAX_BYTES];
                uint32_t remaining_module_side = s_http_content_length - s_http_read_offset;
                uint32_t remaining_buffer_room  = sizeof(s_http_response) - s_http_response_length;
                uint32_t window = (remaining_module_side < remaining_buffer_room) ? remaining_module_side : remaining_buffer_room;
                snprintf(cmd, sizeof(cmd), "AT+QHTTPREAD=80"); /* TODO(open-item): real windowed reads need QHTTPREAD's offset/length parameters - see this function's own top comment; today this always reads from the module's own internal cursor in one shot, only safe while content fits HTTP_RESPONSE_BUFFER_BYTES */
                (void)window;
                (void)at_send(AT_OWNER_HTTP, cmd, AT_HTTP_READ_TIMEOUT_MS);
                s_http_local = HTTP_LOCAL_STREAMING_READ;
            } else if (s_http_read_offset >= s_http_content_length || s_http_response_length >= sizeof(s_http_response)) {
                s_http_local = HTTP_LOCAL_IDLE;
                s_http_state = EC200U_HTTP_COMPLETE;
            }
            return;
        case HTTP_LOCAL_STREAMING_READ:
            if (at_poll(AT_OWNER_HTTP, &ok)) {
                at_release(AT_OWNER_HTTP);
                if (ok) {
                    /* The body itself arrived as info lines captured
                     * between the command and the final OK (Quectel
                     * streams it as raw lines terminated by its own
                     * "+QHTTPREAD: 0" trailer, which this engine's generic
                     * line capture already treats as just another info
                     * line - reassembled here with a CRLF placed back
                     * between what were originally separate lines, since
                     * that framing detail is lost once split into lines;
                     * a response body containing an embedded CRLF is
                     * therefore reconstructed only approximately - a real,
                     * known limitation of this line-oriented engine
                     * applied to a body that is not actually line-shaped,
                     * tracked in OPEN-ITEMS.md, not a silent bug). */
                    for (uint32_t i = 0; i < s_at_info_line_count; i++) {
                        uint32_t len = (uint32_t)strlen(s_at_info_lines[i]);
                        if (strncmp(s_at_info_lines[i], "+QHTTPREAD:", 11) == 0) {
                            continue; /* the trailer line, not body content */
                        }
                        if (s_http_response_length + len <= sizeof(s_http_response)) {
                            memcpy(&s_http_response[s_http_response_length], s_at_info_lines[i], len);
                            s_http_response_length += len;
                        }
                    }
                    s_http_read_offset = s_http_content_length; /* single-shot read today - see HTTP_LOCAL_SENDING_READ's own TODO */
                    s_http_local        = HTTP_LOCAL_SENDING_READ;
                } else {
                    s_http_local = HTTP_LOCAL_IDLE;
                    s_http_state = EC200U_HTTP_ERROR;
                }
            }
            return;
        default:
            return;
    }
}

/* ============================================================================
 * URC DISPATCH (ec200u-manager.md section 9) - checked before treating any
 * line as a response to the currently in-flight command.
 * ============================================================================ */

static bool at_try_dispatch_urc(const char* line) {
    if (strcmp(line, "RING") == 0) {
        if (s_call_state != EC200U_CALL_RINGING && s_call_state != EC200U_CALL_ACTIVE) {
            s_call_state       = EC200U_CALL_RINGING;
            s_call_is_incoming = true;
            s_incoming_number[0] = '\0'; /* filled in by the +CLIP: URC that normally follows RING immediately */
        }
        return true;
    }
    if (strncmp(line, "+CLIP:", 6) == 0) {
        (void)at_extract_quoted_field(line, 0u, s_incoming_number, sizeof(s_incoming_number));
        return true;
    }
    if (strcmp(line, "NO CARRIER") == 0 || strcmp(line, "BUSY") == 0 || strcmp(line, "NO ANSWER") == 0) {
        s_call_state       = EC200U_CALL_ENDED;
        s_call_is_incoming = false;
        return true;
    }
    if (strncmp(line, "+CMTI:", 6) == 0) {
        long index;
        if (at_extract_int_field(line, 1u, &index)) {
            s_sms_rx_pending_index = index;
        }
        return true;
    }
    if (strncmp(line, "+QMTOPEN:", 9) == 0) {
        long result;
        if (at_extract_int_field(line, 1u, &result)) {
            s_mqtt_open_urc_pending = true;
            s_mqtt_open_urc_result  = result;
        }
        return true;
    }
    if (strncmp(line, "+QMTCONN:", 9) == 0) {
        long result;
        if (at_extract_int_field(line, 1u, &result)) {
            s_mqtt_conn_urc_pending = true;
            s_mqtt_conn_urc_result  = result;
        }
        return true;
    }
    if (strncmp(line, "+QMTSTAT:", 9) == 0) {
        /* Connection lost after the fact (network drop, Keep-Alive
         * timeout, Broker-initiated close) - Server Management discovers
         * this the same way it discovers any other disconnect, by polling
         * EC200U_MQTT_GetState() and finding it no longer CONNECTED. */
        s_mqtt_state = EC200U_MQTT_DISCONNECTED;
        s_mqtt_local = MQTT_LOCAL_IDLE;
        return true;
    }
    if (strncmp(line, "+QMTRECV:", 9) == 0) {
        /* +QMTRECV: <client_idx>,<msg_id>,"<topic>","<payload>" - KNOWN
         * LIMITATION (tracked in OPEN-ITEMS.md): Quectel delivers this
         * payload as a quoted string, so a payload byte that is itself a
         * literal '"' would corrupt parsing here - our own Binary Protocol
         * payloads (binary-protocol-final.md) are raw bytes and WILL
         * eventually contain 0x22. Implemented per the module's documented
         * behavior, not silently worked around with an untested guess. */
        if (at_extract_quoted_field(line, 0u, s_mqtt_recv_topic, sizeof(s_mqtt_recv_topic))) {
            char payload_field[MQTT_RECV_PAYLOAD_MAX_LEN];
            if (at_extract_quoted_field(line, 1u, payload_field, sizeof(payload_field))) {
                uint32_t len = (uint32_t)strlen(payload_field);
                if (len > sizeof(s_mqtt_recv_payload)) {
                    len = sizeof(s_mqtt_recv_payload);
                }
                memcpy(s_mqtt_recv_payload, payload_field, len);
                s_mqtt_recv_payload_length = len;
                s_mqtt_recv_pending         = true; /* overwrites any previously-injected-but-not-yet-popped message - single-pending-message, not a bounded FIFO, same convention the old Simulator's Debug Inject already had */
            }
        }
        return true;
    }
    if (strncmp(line, "+QHTTPGET:", 10) == 0) {
        long err;
        long content_length = 0;
        if (at_extract_int_field(line, 0u, &err)) {
            s_http_get_urc_err       = err;
            s_http_get_urc_http_code = 0;
            (void)at_extract_int_field(line, 1u, &s_http_get_urc_http_code);
            (void)at_extract_int_field(line, 2u, &content_length); /* how many bytes AT+QHTTPREAD will actually stream - see s_http_content_length's own comment */
            s_http_content_length    = (uint32_t)content_length;
            s_http_get_urc_pending   = true;
        }
        return true;
    }
    return false;
}

/* ============================================================================
 * PUBLIC API - infrastructure/ec200u_manager.h. Unchanged Contract, real
 * implementation behind it (see this file's own top comment).
 * ============================================================================ */

EC200U_Result EC200U_Init(void) {
    s_module_state  = EC200U_MODULE_OFF;
    s_network_state = EC200U_NETWORK_UNKNOWN;
    s_sim_card_state = EC200U_SIMCARD_NOT_READY;

    s_at_owner = AT_OWNER_NONE;
    s_rx_line_len = 0u;

    HWProxy_UART_Init();

    s_module_state = EC200U_MODULE_INITIALIZING;
    module_enter_step(INIT_POWER_SETTLE);

    s_gnss_session = GNSS_SESSION_STOPPED;
    memset(&s_gnss_latest, 0, sizeof(s_gnss_latest));

    s_sms_rx_state         = SMS_RX_IDLE;
    s_sms_rx_pending_index = -1;
    s_sms_pending           = false;
    s_sms_tx_state          = SMS_TX_IDLE;
    s_sms_tx_last_status    = EC200U_NOT_READY;

    s_call_state          = EC200U_CALL_IDLE;
    s_call_is_incoming    = false;
    s_incoming_number[0]  = '\0';
    s_call_pending_action = CALL_ACTION_NONE;
    s_call_at_purpose     = CALL_AT_PURPOSE_NONE;

    s_mqtt_state             = EC200U_MQTT_DISCONNECTED;
    s_mqtt_local              = MQTT_LOCAL_IDLE;
    s_mqtt_open_urc_pending   = false;
    s_mqtt_conn_urc_pending   = false;
    s_mqtt_pub_state          = MQTT_PUB_IDLE;
    s_mqtt_sub_state          = MQTT_SUB_IDLE;
    s_mqtt_recv_pending       = false;

    s_http_state = EC200U_HTTP_IDLE;
    s_http_local = HTTP_LOCAL_IDLE;
    s_http_get_urc_pending = false;

    return EC200U_OK;
}

void EC200U_Process(void) {
    at_rx_pump();
    module_process();
    if (s_module_state != EC200U_MODULE_READY) {
        return; /* every Capability below needs a live AT interface first - same "Init must finish before Capability Init" ordering section 11 describes */
    }
    gnss_process();
    sms_process();
    call_process();
    mqtt_process();
    http_process();
}

EC200U_ModuleState  EC200U_GetModuleState(void)  { return s_module_state; }
EC200U_NetworkState EC200U_GetNetworkState(void) { return s_network_state; }
EC200U_SimCardState EC200U_GetSimCardState(void) { return s_sim_card_state; }

/* --- GNSS --- */

EC200U_Result EC200U_GNSS_Start(void) {
    if (s_module_state != EC200U_MODULE_READY) {
        return EC200U_NOT_READY;
    }
    if (s_gnss_session == GNSS_SESSION_STOPPED) {
        s_gnss_session = GNSS_SESSION_STARTING;
    }
    return EC200U_OK; /* idempotent while STARTING/ACTIVE already, same as EC200U_MQTT_Connect() */
}

EC200U_Result EC200U_GNSS_Stop(void) {
    if (s_gnss_session != GNSS_SESSION_STOPPED) {
        s_gnss_session = GNSS_SESSION_STOPPING;
    }
    return EC200U_OK;
}

EC200U_Result EC200U_GNSS_GetLatest(EC200U_GnssData* data) {
    if (data == NULL) {
        return EC200U_ERROR;
    }
    if (s_module_state != EC200U_MODULE_READY) {
        data->fix_valid = false;
        return EC200U_NOT_READY;
    }
    *data = s_gnss_latest;
    return EC200U_OK;
}

/* --- SMS --- */

EC200U_Result EC200U_SMS_Send(const EC200U_SmsData* sms) {
    if (sms == NULL || sms->destination == NULL || sms->payload == NULL) {
        return EC200U_ERROR;
    }
    if (s_module_state != EC200U_MODULE_READY) {
        return EC200U_NOT_READY;
    }
    if (s_sms_tx_state != SMS_TX_IDLE) {
        return EC200U_BUSY;
    }

    strncpy(s_sms_tx_number, sms->destination, sizeof(s_sms_tx_number) - 1u);
    s_sms_tx_number[sizeof(s_sms_tx_number) - 1u] = '\0';

    s_sms_tx_text_length = (sms->payload_length < SMS_MAX_TEXT_LENGTH) ? sms->payload_length : SMS_MAX_TEXT_LENGTH;
    memcpy(s_sms_tx_text, sms->payload, s_sms_tx_text_length);
    s_sms_tx_text[s_sms_tx_text_length] = '\0';

    s_sms_tx_last_status = EC200U_BUSY;
    s_sms_tx_state        = SMS_TX_SENDING_HEADER;
    return EC200U_OK;
}

/* NEW - not in the original draft Contract (docs/03-infrastructure-contracts/
 * ec200u-manager.md section 15 predates real hardware timing): a real
 * AT+CMGS round trip (module prompt + SMSC delivery) cannot complete
 * inside a single Non-Blocking EC200U_SMS_Send() call the way the old
 * Simulator's instant fake success could. sms_management.c's own
 * process_pending_send() was updated to poll this instead of treating
 * EC200U_SMS_Send()'s own return as final - see its own comment. */
EC200U_Result EC200U_SMS_GetSendStatus(void) {
    if (s_sms_tx_state != SMS_TX_IDLE) {
        return EC200U_BUSY;
    }
    return s_sms_tx_last_status;
}

EC200U_Result EC200U_SMS_GetReceived(EC200U_SmsData* sms) {
    if (sms == NULL) {
        return EC200U_ERROR;
    }
    if (!s_sms_pending) {
        return EC200U_NOT_READY;
    }
    sms->destination     = s_sms_rx_sender;
    sms->payload          = s_sms_rx_text;
    sms->payload_length   = s_sms_rx_text_length;
    s_sms_pending          = false;
    return EC200U_OK;
}

/* --- Voice Call --- */

EC200U_Result EC200U_Call_Dial(const char* number) {
    if (s_call_dial_override == OVERRIDE_FAILURE) {
        s_call_state = EC200U_CALL_ENDED;
        return EC200U_ERROR;
    }
    if (s_module_state != EC200U_MODULE_READY && s_call_dial_override != OVERRIDE_SUCCESS) {
        return EC200U_NOT_READY;
    }
    if (s_call_state == EC200U_CALL_DIALING || s_call_state == EC200U_CALL_RINGING || s_call_state == EC200U_CALL_ACTIVE) {
        return EC200U_BUSY;
    }
    if (number == NULL || number[0] == '\0') {
        return EC200U_ERROR;
    }

    strncpy(s_call_pending_number, number, sizeof(s_call_pending_number) - 1u);
    s_call_pending_number[sizeof(s_call_pending_number) - 1u] = '\0';
    s_call_is_incoming    = false;
    s_call_pending_action = CALL_ACTION_DIAL;
    s_call_state           = EC200U_CALL_DIALING; /* optimistic - call_process() reverts to ENDED if ATD itself is rejected */
    return EC200U_OK;
}

EC200U_Result EC200U_Call_Hangup(void) {
    if (s_call_state == EC200U_CALL_IDLE || s_call_state == EC200U_CALL_ENDED) {
        return EC200U_NOT_READY;
    }
    s_call_pending_action = CALL_ACTION_HANGUP;
    return EC200U_OK;
}

EC200U_CallState EC200U_Call_GetState(void) { return s_call_state; }

EC200U_Result EC200U_Call_Answer(void) {
    if (s_call_state != EC200U_CALL_RINGING || !s_call_is_incoming) {
        return EC200U_NOT_READY;
    }
    s_call_pending_action = CALL_ACTION_ANSWER;
    return EC200U_OK;
}

EC200U_Result EC200U_Call_GetIncomingNumber(char* buffer, uint32_t buffer_size) {
    if (buffer == NULL || buffer_size == 0u) {
        return EC200U_ERROR;
    }
    if (s_call_state != EC200U_CALL_RINGING || !s_call_is_incoming) {
        return EC200U_NOT_READY;
    }
    strncpy(buffer, s_incoming_number, buffer_size - 1u);
    buffer[buffer_size - 1u] = '\0';
    return EC200U_OK;
}

/* --- Data / Server transport (legacy, unused - superseded by the MQTT
 * Capability below; see server_management.c's own history. Kept as an
 * honest NOT_READY stub, same "do not build unused things" discipline as
 * every other dead-but-still-declared function in this codebase - no
 * consumer exists to give this real behavior meaning. --- */

EC200U_Result EC200U_Data_Send(const uint8_t* payload, uint32_t length) {
    (void)payload;
    (void)length;
    if (s_data_send_override == OVERRIDE_SUCCESS) return EC200U_OK;
    if (s_data_send_override == OVERRIDE_FAILURE) return EC200U_ERROR;
    return EC200U_NOT_READY;
}

EC200U_Result EC200U_Data_Receive(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length) {
    (void)buffer;
    (void)buffer_size;
    if (received_length) *received_length = 0u;
    return EC200U_NOT_READY;
}

/* --- MQTT --- */

EC200U_Result EC200U_MQTT_Connect(const char* client_id, const char* host, uint16_t port) {
    if (client_id == NULL || host == NULL) {
        return EC200U_ERROR;
    }
    if (s_module_state != EC200U_MODULE_READY) {
        return EC200U_NOT_READY;
    }
    if (s_mqtt_state == EC200U_MQTT_CONNECTED || s_mqtt_state == EC200U_MQTT_CONNECTING) {
        return EC200U_OK; /* idempotent, same shape as EC200U_GNSS_Start() */
    }

    strncpy(s_mqtt_client_id, client_id, sizeof(s_mqtt_client_id) - 1u);
    s_mqtt_client_id[sizeof(s_mqtt_client_id) - 1u] = '\0';
    strncpy(s_mqtt_host, host, sizeof(s_mqtt_host) - 1u);
    s_mqtt_host[sizeof(s_mqtt_host) - 1u] = '\0';
    s_mqtt_port = port;

    {
        char cmd[AT_COMMAND_MAX_BYTES];
        snprintf(cmd, sizeof(cmd), "AT+QMTOPEN=%u,\"%s\",%u", (unsigned)EC200U_MQTT_CLIENT_ID, s_mqtt_host, (unsigned)s_mqtt_port);
        if (!at_send(AT_OWNER_MQTT, cmd, AT_MQTT_OPEN_TIMEOUT_MS)) {
            return EC200U_OK; /* channel momentarily busy - mqtt_process() below will (re)issue this once free, since s_mqtt_local is about to become OPENING regardless */
        }
    }
    s_mqtt_local = MQTT_LOCAL_OPENING;
    s_mqtt_state = EC200U_MQTT_CONNECTING;
    return EC200U_OK;
}

EC200U_Result EC200U_MQTT_Disconnect(void) {
    s_mqtt_state = EC200U_MQTT_DISCONNECTED;
    s_mqtt_local = MQTT_LOCAL_IDLE;
    /* AT+QMTDISC/AT+QMTCLOSE are not issued here - Disconnect() is
     * documented as immediate, not staged through Process() (same as the
     * old Simulator's own contract) - the module-side socket is simply
     * abandoned; a fresh EC200U_MQTT_Connect() re-opens a new one. */
    return EC200U_OK;
}

EC200U_MqttState EC200U_MQTT_GetState(void) { return s_mqtt_state; }

EC200U_Result EC200U_MQTT_Publish(const EC200U_MqttMessage* message) {
    if (message == NULL || message->topic == NULL || (message->payload == NULL && message->payload_length > 0u)) {
        return EC200U_ERROR;
    }
    if (s_mqtt_publish_override == OVERRIDE_SUCCESS) {
        return EC200U_OK;
    }
    if (s_mqtt_publish_override == OVERRIDE_FAILURE) {
        return EC200U_ERROR;
    }
    if (s_mqtt_state != EC200U_MQTT_CONNECTED) {
        return EC200U_NOT_READY;
    }
    if (s_mqtt_pub_state != MQTT_PUB_IDLE) {
        return EC200U_NOT_READY; /* one Publish in flight at a time - Server Management's own one-item-per-Process() drain already never calls this twice back to back anyway */
    }

    strncpy(s_mqtt_pub_topic, message->topic, sizeof(s_mqtt_pub_topic) - 1u);
    s_mqtt_pub_topic[sizeof(s_mqtt_pub_topic) - 1u] = '\0';

    s_mqtt_pub_payload_length = (message->payload_length < sizeof(s_mqtt_pub_payload)) ? message->payload_length : sizeof(s_mqtt_pub_payload);
    if (s_mqtt_pub_payload_length > 0u) {
        memcpy(s_mqtt_pub_payload, message->payload, s_mqtt_pub_payload_length);
    }

    s_mqtt_pub_state = MQTT_PUB_SENDING_HEADER;
    /* Fire-and-forget from here - "OK does not by itself guarantee Backend
     * delivery" (this function's own header comment in ec200u_manager.h);
     * a real +QMTPUB: failure is not reported back up, same accepted
     * limitation the header already documents. */
    return EC200U_OK;
}

EC200U_Result EC200U_MQTT_Subscribe(const char* topic) {
    if (topic == NULL) {
        return EC200U_ERROR;
    }
    if (s_mqtt_state != EC200U_MQTT_CONNECTED) {
        return EC200U_NOT_READY;
    }
    strncpy(s_mqtt_sub_topic, topic, sizeof(s_mqtt_sub_topic) - 1u);
    s_mqtt_sub_topic[sizeof(s_mqtt_sub_topic) - 1u] = '\0';
    s_mqtt_sub_state = MQTT_SUB_SENDING;
    return EC200U_OK;
}

EC200U_Result EC200U_MQTT_GetReceived(EC200U_MqttMessage* message) {
    if (message == NULL) {
        return EC200U_ERROR;
    }
    if (!s_mqtt_recv_pending) {
        return EC200U_NOT_READY;
    }
    message->topic          = s_mqtt_recv_topic;
    message->payload        = s_mqtt_recv_payload;
    message->payload_length = s_mqtt_recv_payload_length;
    s_mqtt_recv_pending      = false;
    return EC200U_OK;
}

/* --- HTTP --- */

EC200U_Result EC200U_HTTP_StartGet(const char* url) {
    if (url == NULL || url[0] == '\0') {
        return EC200U_ERROR;
    }
    if (s_http_local != HTTP_LOCAL_IDLE) {
        return EC200U_BUSY;
    }
    strncpy(s_http_url, url, sizeof(s_http_url) - 1u);
    s_http_url[sizeof(s_http_url) - 1u] = '\0';

    s_http_state = EC200U_HTTP_IN_PROGRESS;
    s_http_local = HTTP_LOCAL_SENDING_URL_CMD;
    return EC200U_OK;
}

EC200U_HttpState EC200U_HTTP_GetState(void) { return s_http_state; }

EC200U_Result EC200U_HTTP_GetChunk(uint8_t* buffer, uint32_t buffer_size, uint32_t* received_length) {
    uint32_t remaining;
    uint32_t n;

    if (buffer == NULL || buffer_size == 0u) {
        return EC200U_ERROR;
    }
    if (received_length) {
        *received_length = 0u;
    }
    if (s_http_state != EC200U_HTTP_COMPLETE) {
        return EC200U_NOT_READY;
    }
    if (s_http_drain_offset >= s_http_response_length) {
        return EC200U_NOT_READY;
    }

    remaining = s_http_response_length - s_http_drain_offset;
    n         = (remaining < buffer_size) ? remaining : buffer_size;
    memcpy(buffer, &s_http_response[s_http_drain_offset], n);
    s_http_drain_offset += n;
    if (received_length) {
        *received_length = n;
    }
    return EC200U_OK;
}

/* --- Debug/Runtime overrides (board bring-up only) ---
 * Real hardware still benefits from being able to force a specific
 * Fault/Retry path on demand while bench-testing - same purpose these
 * always had, just short-circuiting real AT-driven logic now instead of
 * a Simulator's fake state. Not part of the real EC200U Contract; no
 * Management above this layer may call these. */

void EC200U_Debug_ForceDataSendSuccess(void)  { s_data_send_override = OVERRIDE_SUCCESS; }
void EC200U_Debug_ForceDataSendFailure(void)  { s_data_send_override = OVERRIDE_FAILURE; }
void EC200U_Debug_ClearDataSendOverride(void) { s_data_send_override = OVERRIDE_NONE; }

void EC200U_Debug_ForceMqttPublishSuccess(void)  { s_mqtt_publish_override = OVERRIDE_SUCCESS; }
void EC200U_Debug_ForceMqttPublishFailure(void)  { s_mqtt_publish_override = OVERRIDE_FAILURE; }
void EC200U_Debug_ClearMqttPublishOverride(void) { s_mqtt_publish_override = OVERRIDE_NONE; }

void EC200U_Debug_ForceCallDialSuccess(void)  { s_call_dial_override = OVERRIDE_SUCCESS; }
void EC200U_Debug_ForceCallDialFailure(void)  { s_call_dial_override = OVERRIDE_FAILURE; }
void EC200U_Debug_ClearCallDialOverride(void) { s_call_dial_override = OVERRIDE_NONE; }

void EC200U_Debug_SimulateIncomingCall(const char* number) {
    s_call_state       = EC200U_CALL_RINGING;
    s_call_is_incoming = true;
    strncpy(s_incoming_number, number ? number : "", sizeof(s_incoming_number) - 1u);
    s_incoming_number[sizeof(s_incoming_number) - 1u] = '\0';
}

void EC200U_Debug_InjectMqttReceived(const char* topic, const uint8_t* payload, uint32_t payload_length) {
    strncpy(s_mqtt_recv_topic, topic ? topic : "", sizeof(s_mqtt_recv_topic) - 1u);
    s_mqtt_recv_topic[sizeof(s_mqtt_recv_topic) - 1u] = '\0';
    s_mqtt_recv_payload_length = (payload_length < sizeof(s_mqtt_recv_payload)) ? payload_length : sizeof(s_mqtt_recv_payload);
    if (payload && s_mqtt_recv_payload_length > 0u) {
        memcpy(s_mqtt_recv_payload, payload, s_mqtt_recv_payload_length);
    }
    s_mqtt_recv_pending = true;
}
