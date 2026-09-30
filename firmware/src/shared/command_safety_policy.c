#include "shared/command_safety_policy.h"

/* See command_safety_policy.h for the full explanation. This body is
 * deliberately the only place in the firmware that answers "is it safe to
 * execute this command right now" - keep it that way; do not duplicate
 * this decision anywhere else. */
bool CommandSafetyPolicy_IsAllowed(CommandDomain domain) {
    switch (domain) {
        case COMMAND_DOMAIN_DIAGNOSTIC:
        case COMMAND_DOMAIN_GPS_POSITION_READ:
        case COMMAND_DOMAIN_FIRMWARE_DOWNLOAD:
        case COMMAND_DOMAIN_DIAGNOSTIC_ACTUATOR_TEST:
        case COMMAND_DOMAIN_ACTUATOR_DOOR_GPIO:
        case COMMAND_DOMAIN_FIRMWARE_INSTALL:
            return true; /* explicit user decision - see header comment on each case above */
        default:
            return false; /* fail-closed - no other domain has a defined policy yet */
    }
}
