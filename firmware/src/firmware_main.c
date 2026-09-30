#include "firmware_main.h"
#include "infrastructure/time_base.h"
#include "infrastructure/configuration.h"
#include "infrastructure/file_transfer.h"
#include "infrastructure/log.h"
#include "infrastructure/ec200u_manager.h"
#include "managements/system_management.h"
#include "managements/power_management.h"
#include "managements/hardware_input_management.h"
#include "managements/diagnostic_management.h"
#include "managements/gps_management.h"
#include "managements/server_management.h"
#include "managements/sms_management.h"
#include "managements/call_management.h"
#include "managements/firmware_update_management.h"
#include "managements/actuator_management.h"
#include "shared/event_policy.h"
#include "shared/queues.h"

/*
 * The firmware's main Super Loop.
 * The order of the Process() calls is not a hard requirement (per
 * docs/03-infrastructure-contracts/management-infrastructure-dependency.md
 * section 7), but each one must:
 *   - be non-blocking
 *   - do a small, bounded amount of work
 *   - keep its own state between calls
 *   - return quickly when there is nothing to do
 *
 * This file is pure wiring; the real logic lives inside each Management.
 *
 * This function never returns and does not replace each target's real
 * main() - the full explanation of how it integrates with the CubeIDE-
 * generated main() of each board is in include/firmware_main.h and
 * docs/03-infrastructure-contracts/target-integration.md.
 */

void Firmware_Main(void) {
    Time_Init();
    Queues_Init(); /* Must run before any Management Init/Process - every
                     * queue instance (Event, SMS/Call/Actuator Request,
                     * Server Event, Telemetry, Fault) starts as a zeroed
                     * static Queue{buffer=NULL, capacity=0}; without this
                     * call every Queue_Push() below silently fails
                     * (entry_size > capacity is always true at 0). */
    Configuration_Init(); /* Loads Runtime Configuration before any Management that may read it (e.g. Call_IsNumberAllowed()) */
    FileTransfer_Init(); /* Infrastructure, generic over any StorageObject - see infrastructure/file_transfer.h */
    Log_Init(); /* Infrastructure - consumes the Log Queue, see infrastructure/log.h */
    EC200U_Init(); /* GPS/Server/SMS/Call depend on this; its Contract is still a draft (OPEN-ITEMS #1) */

    System_Init(); /* Owns Watchdog Init/Refresh (via Hardware Proxy) and LED status - see system_management.c */
    Power_Init();
    HardwareInput_Init();
    Diagnostic_Init();
    GPS_Init();
    Server_Init();
    SMS_Init();
    Call_Init();
    FirmwareUpdate_Init();
    Actuator_Init();
    EventPolicy_Init();

    for (;;) {
        HardwareInput_Process();
        Power_Process();
        Diagnostic_Process();
        GPS_Process();
        Server_Process();
        SMS_Process();
        Call_Process();
        FirmwareUpdate_Process();
        Actuator_Process();
        EC200U_Process();
        System_Process();
        FileTransfer_Process(); /* advances a pending Storage write and checks the transfer Timeout - see infrastructure/file_transfer.h */

        /* Drains the Event Queue and routes each event to SMS/Call/Server/
         * Storage per the policy table. Without this call, events pushed by
         * Hardware Input Management (and future producers) would just sit
         * in the queue until it fills and starts dropping the oldest ones. */
        EventPolicy_Process();

        /* Drains the Log Queue that EventPolicy_Process()/check_boot_error()
         * (and any future producer) fed above - see infrastructure/log.h. */
        Log_Process();
    }
}
