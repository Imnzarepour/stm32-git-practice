#ifndef SHARED_COMMAND_ROUTER_H
#define SHARED_COMMAND_ROUTER_H
#include "shared/command_safety_policy.h"
#include <stdint.h>

/* Mirrors shared/event_policy.c's role but for the opposite direction: a
 * light, FSM-less Component that decouples "something decoded a Command
 * off the wire" (today: Server Management, section 2/11 of
 * backend-contract-tcu-hekar.md puts "دریافت Command" in its scope) from
 * "which Management actually owns executing it" (out of that same
 * Contract's scope - "خارج از محدوده: ... منطق Actuator/Firmware Update/
 * Diagnostic"). The caller only needs to know the generic wire shape
 * below, never DiagnosticOperation/DiagnosticRequest or any other owning
 * Management's internal types - same boundary event_policy.c already
 * keeps between the Event Queue and SMS/Call/Server.
 *
 * Every routed Command passes through CommandSafetyPolicy_IsAllowed()
 * first (shared/command_safety_policy.h) - this Component enforces that
 * gate, callers must not bypass it. */

typedef struct {
    CommandDomain domain;
    /* Domain-specific meaning owned entirely by whichever Management
     * ultimately receives it (e.g. for COMMAND_DOMAIN_DIAGNOSTIC these
     * three map 1:1 onto DiagnosticRequest's operation/target/parameter,
     * docs/02-management-contracts/diagnostic-management.md) - this
     * router does not interpret them beyond that mapping. */
    uint32_t operation;
    uint32_t target;
    uint32_t parameter;
} Command;

typedef enum {
    COMMAND_ROUTER_ACCEPTED,           /* handed off to the owning Management, now in flight */
    COMMAND_ROUTER_REJECTED_POLICY,    /* CommandSafetyPolicy_IsAllowed() returned false for this domain */
    COMMAND_ROUTER_REJECTED_MALFORMED, /* unknown domain, or an operation the owning Management does not recognize */
    COMMAND_ROUTER_BUSY                /* owning Management could not accept right now (e.g. Diagnostic's Current+Pending both occupied) */
} CommandRouterResult;

CommandRouterResult CommandRouter_Submit(const Command* command);

/* Shared with server_management.c's own wire decode (FirmwareDownloadWirePayload)
 * and firmware_update_management.h's stored URL - single source of truth so
 * all three never drift apart. */
#define FIRMWARE_DOWNLOAD_URL_MAX_LENGTH 128u

/* COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's own, separate entry point - explicit
 * user decision (OPEN-ITEMS.md TCU<->Backend #12, after HTTP was confirmed
 * as the FOTA transport): the generic `Command` struct above (three plain
 * uint32_t fields) has no room for a URL, so this domain does not go
 * through CommandRouter_Submit()/Command at all - server_management.c
 * decodes its own dedicated, larger wire shape and calls this function
 * directly instead. Still enforces the same CommandSafetyPolicy_IsAllowed()
 * gate internally - callers must not bypass it by calling
 * FirmwareUpdate_SubmitRequest() themselves. */
typedef struct {
    uint32_t expected_size;
    char     url[FIRMWARE_DOWNLOAD_URL_MAX_LENGTH]; /* always NUL-terminated by the caller */
} FirmwareDownloadRequest;

CommandRouterResult CommandRouter_SubmitFirmwareDownload(const FirmwareDownloadRequest* request);

#endif
