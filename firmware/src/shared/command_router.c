#include "shared/command_router.h"
#include "managements/diagnostic_management.h"
#include "managements/firmware_update_management.h"
#include "managements/actuator_management.h"
#include "infrastructure/time_base.h"
#include <stddef.h>

/* See command_router.h for the role this plays. Every domain
 * CommandSafetyPolicy_IsAllowed() lets through has a matching case below;
 * anything it fail-closes never reaches one (the default case at the
 * bottom of CommandRouter_Submit() is unreachable for those, kept
 * explicit for when a domain's Policy opens up). Adding a new domain
 * means: give it a real case in CommandSafetyPolicy_IsAllowed()
 * (command_safety_policy.c) AND a new case below that maps Command's
 * generic operation/target/parameter onto that Management's own Request
 * type and calls its Submit function - same two-step shape every case
 * below already follows. */
static CommandRouterResult route_to_diagnostic(const Command* command) {
    if (command->operation > (uint32_t)DIAG_OP_CLEAR_DTC) {
        return COMMAND_ROUTER_REJECTED_MALFORMED; /* not a DiagnosticOperation this Contract knows */
    }

    DiagnosticRequest request;
    request.operation = (DiagnosticOperation)command->operation;
    request.target     = command->target;
    request.parameter  = command->parameter;

    switch (Diagnostic_SubmitRequest(&request)) {
        case DIAG_SUBMIT_ACCEPTED:
        case DIAG_SUBMIT_PENDING:
            return COMMAND_ROUTER_ACCEPTED; /* Diagnostic_GetResponse() will report it later, section 8/12's own "occupied means queued" shape */
        case DIAG_SUBMIT_BUSY:
        default:
            return COMMAND_ROUTER_BUSY;
    }
}

/* CommandRouter_SubmitFirmwareDownload() below is COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's
 * only entry point now (it carries a URL, which the generic Command struct
 * cannot hold - see command_router.h's comment on it) - this domain is
 * intentionally NOT one of the cases in CommandRouter_Submit()'s switch. */

/* Also lands on Diagnostic Management, same owner as route_to_diagnostic()
 * above, but ALWAYS as DIAG_OP_ACTUATOR_TEST - command->operation is
 * unused here (unlike route_to_diagnostic(), a caller in this domain
 * never picks the operation, only the Policy gate decided it may reach
 * an Actuator at all). command->target = target ECU id,
 * command->parameter = commanded state - both opaque to this router,
 * same "does not interpret domain-specific meaning" boundary
 * route_to_diagnostic() already keeps (see diagnostic_management.h's
 * comment on DiagnosticRequest.parameter). */
static CommandRouterResult route_to_diagnostic_actuator_test(const Command* command) {
    DiagnosticRequest request;
    request.operation = DIAG_OP_ACTUATOR_TEST;
    request.target     = command->target;
    request.parameter  = command->parameter;

    switch (Diagnostic_SubmitRequest(&request)) {
        case DIAG_SUBMIT_ACCEPTED:
        case DIAG_SUBMIT_PENDING:
            return COMMAND_ROUTER_ACCEPTED;
        case DIAG_SUBMIT_BUSY:
        default:
            return COMMAND_ROUTER_BUSY;
    }
}

/* Second, independent execution mechanism for the same physical action as
 * route_to_diagnostic_actuator_test() above - direct GPIO relay via
 * Actuator Management instead of Diagnostic/CAN, explicit user request
 * for two separate commands/domains (see command_safety_policy.h's
 * comment on COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO). command->target is
 * unused here - Actuator Management addresses its Actuators by
 * ActuatorId, not an ECU id, unlike the Diagnostic path.
 * command->parameter picks WHICH actuator to command (1 = unlock/open ->
 * ACTUATOR_DOOR_UNLOCK, 0 = lock/close -> ACTUATOR_DOOR_LOCK), always
 * issuing ACTUATOR_CMD_ACTIVATE on that one id - Door Lock and Door
 * Unlock are already two distinct ActuatorIds, one physical relay pulse
 * each (actuator_management.h), unlike a single actuator with an on/off
 * state. */
static CommandRouterResult route_to_actuator_door_gpio(const Command* command) {
    ActuatorRequest request;
    request.actuator  = (command->parameter != 0u) ? ACTUATOR_DOOR_UNLOCK : ACTUATOR_DOOR_LOCK;
    request.command    = ACTUATOR_CMD_ACTIVATE;
    request.timestamp  = Time_GetTick();

    if (!Actuator_SubmitRequest(&request)) {
        return COMMAND_ROUTER_REJECTED_MALFORMED; /* structural validation failure only - request.actuator/command are both compile-time-valid constants above, so this is effectively unreachable, kept explicit per Actuator_SubmitRequest()'s own bool contract */
    }
    return COMMAND_ROUTER_ACCEPTED;
}

/* Triggers Installation of whatever Firmware Download is already parked in
 * FIRMWARE_UPDATE_READY - command->operation/target/parameter are all
 * unused, there is exactly one thing this domain can mean ("install the
 * already-downloaded image"), same one-argument-less shape reasoning
 * route_to_firmware_download() above documents for its own single
 * argument. Explicit user decision (OPEN-ITEMS.md, given by direct analogy
 * to route_to_actuator_door_gpio()/route_to_diagnostic_actuator_test()
 * above - "مثل درب باز کن"): accepting this Command ultimately leads to a
 * real Boot Action=PROGRAM write + SYSTEM_RESET, see
 * managements/firmware_update_management.c's FirmwareUpdate_SubmitInstallRequest(). */
static CommandRouterResult route_to_firmware_install(const Command* command) {
    (void)command;
    if (!FirmwareUpdate_SubmitInstallRequest()) {
        return COMMAND_ROUTER_BUSY; /* not currently READY - see FirmwareUpdate_SubmitInstallRequest()'s own comment */
    }
    return COMMAND_ROUTER_ACCEPTED;
}

CommandRouterResult CommandRouter_Submit(const Command* command) {
    if (command == NULL) {
        return COMMAND_ROUTER_REJECTED_MALFORMED;
    }

    if (!CommandSafetyPolicy_IsAllowed(command->domain)) {
        return COMMAND_ROUTER_REJECTED_POLICY;
    }

    switch (command->domain) {
        case COMMAND_DOMAIN_DIAGNOSTIC:
            return route_to_diagnostic(command);
        case COMMAND_DOMAIN_FIRMWARE_DOWNLOAD:
            return COMMAND_ROUTER_REJECTED_MALFORMED; /* must arrive via CommandRouter_SubmitFirmwareDownload() instead - see that function's own comment */
        case COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST:
            return route_to_diagnostic_actuator_test(command);
        case COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO:
            return route_to_actuator_door_gpio(command);
        case COMMAND_DOMAIN_FIRMWARE_INSTALL:
            return route_to_firmware_install(command);
        default:
            return COMMAND_ROUTER_REJECTED_MALFORMED; /* unreachable while CommandSafetyPolicy_IsAllowed() fail-closes every domain not given a case here, kept explicit for when that changes (e.g. COMMAND_DOMAIN_GPS_POSITION_READ is intentionally never routed through here - see sms_management.c, which calls GPS_GetPosition() directly as a Data-path read, not an Action) */
    }
}

/* COMMAND_DOMAIN_FIRMWARE_DOWNLOAD's dedicated entry point (command_router.h's
 * own comment on FirmwareDownloadRequest explains why it bypasses Command/
 * CommandRouter_Submit() entirely) - still goes through the exact same
 * Policy gate every other domain does, just inline here instead of via the
 * generic switch above. */
CommandRouterResult CommandRouter_SubmitFirmwareDownload(const FirmwareDownloadRequest* request) {
    if (request == NULL) {
        return COMMAND_ROUTER_REJECTED_MALFORMED;
    }

    if (!CommandSafetyPolicy_IsAllowed(COMMAND_DOMAIN_FIRMWARE_DOWNLOAD)) {
        return COMMAND_ROUTER_REJECTED_POLICY;
    }

    if (!FirmwareUpdate_SubmitRequest(request->expected_size, request->url)) {
        return COMMAND_ROUTER_BUSY; /* already downloading, expected_size was 0, or url was empty - see FirmwareUpdate_SubmitRequest()'s own comment */
    }
    return COMMAND_ROUTER_ACCEPTED;
}
