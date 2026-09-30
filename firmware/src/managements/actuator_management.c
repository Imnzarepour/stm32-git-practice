#include "managements/actuator_management.h"
#include "hardware_proxy/hardware_proxy_digital_io.h"
#include "shared/queues.h"
#include "shared/event.h"
#include "shared/reset_request.h"
#include "infrastructure/time_base.h"
#include <stddef.h>

/* Contract source: docs/02-management-contracts/actuator-management.md
 * Model per §13 (no complex FSM needed): Request -> Validate -> Execute -> Update State,
 * all inside one Process() call - Actuators have no independent physical feedback (§9),
 * so "done" simply means "the output command was issued", not "physically confirmed". */

#define NUM_ACTUATORS 4u /* Must stay in sync with ActuatorId in the header */
#define NUM_COMMANDS  4u /* Must stay in sync with ActuatorCommand in the header */

/* Placeholder logical lines (explicit user decision - Door Lock/Unlock
 * now also drive real GPIO output, see OPEN-ITEMS.md's 🔴 critical item).
 * These do not collide with Hardware Input Management's lines 0-3 (ACC/
 * DOOR/SHOCK/reserve_input), but no matching entry exists yet in
 * hardware_proxy_digital_io_stm32f4.c's LINE_MAP - that file only has
 * pins for the 4 input lines CubeMX has generated so far. Real board
 * wiring for these output lines is a hardware bring-up TODO, not
 * something to invent here.
 *
 * OPEN-ITEMS.md 🟡#3 "3 or 4 physical outputs?" - this has flipped TWICE:
 * (1) originally 4 independent lines, (2) explicit user correction moved
 * it to 3 (Door Lock/Unlock sharing one SET/RESET pin), (3) explicit user
 * decision again on 2026-09-19, after docs/02-management-contracts/
 * binary-protocol-final.md's Hekar-proposed 4-bit Relay Mask (B0..B3)
 * surfaced the conflict, moved it BACK to 4 independent lines ("۴ رله
 * واقعی (طبق سند هکار)"). If this is ever questioned a third time, read
 * OPEN-ITEMS.md 🟡#3's full history first - do not guess. */
#define LINE_ENGINE_CUT_OFF ((HWProxy_DigitalLine)4)
#define LINE_ALARM          ((HWProxy_DigitalLine)5)
#define LINE_DOOR_LOCK      ((HWProxy_DigitalLine)6)
#define LINE_DOOR_UNLOCK    ((HWProxy_DigitalLine)7)

/* TODO(open-item): ساختار رسمی Fault هنوز در OPEN-ITEMS.md باز است (بند 🔴 #5).
 * این یک فرم حداقلی و محلی است، دقیقاً هم‌سبک با EventPolicyFaultRecord در
 * shared/event_policy.h - جایگزین Fault Contract نهایی نیست. */
#define ACTUATOR_FAULT_CODE_INVALID_REQUEST       3001u
#define ACTUATOR_FAULT_CODE_INVALID_ACTUATOR      3002u
#define ACTUATOR_FAULT_CODE_OUTPUT_DRIVER_FAILURE 3003u

typedef struct {
    uint32_t fault_code;
    uint32_t actuator_id; /* 0xFFFFFFFF اگر Request اصلا شناسه معتبر نداشت */
    uint32_t timestamp;
} ActuatorFaultRecord;

/* Local recovery policy (§11): a single failed HWProxy write is not
 * escalated immediately - only repeated, consecutive failures on the same
 * Actuator are treated as a Hardware Fault that needs a Reset Request. */
#define OUTPUT_FAILURE_RESET_THRESHOLD 3u

static ActuatorState s_state[NUM_ACTUATORS];
static uint32_t      s_consecutive_output_failures[NUM_ACTUATORS];

static bool is_valid_actuator(ActuatorId id) {
    return (uint32_t)id < NUM_ACTUATORS;
}

static bool is_valid_command(ActuatorCommand command) {
    return (uint32_t)command < NUM_COMMANDS;
}

static HWProxy_DigitalLine line_for(ActuatorId id) {
    switch (id) {
        case ACTUATOR_ENGINE_CUT_OFF: return LINE_ENGINE_CUT_OFF;
        case ACTUATOR_ALARM:          return LINE_ALARM;
        case ACTUATOR_DOOR_LOCK:      return LINE_DOOR_LOCK;
        case ACTUATOR_DOOR_UNLOCK:    return LINE_DOOR_UNLOCK;
        default:                       return LINE_ENGINE_CUT_OFF; /* unreachable - execute() is only ever called after is_valid_actuator() */
    }
}

static bool command_is_active(ActuatorCommand command) {
    return (command == ACTUATOR_CMD_ON) || (command == ACTUATOR_CMD_ACTIVATE);
}

static void push_event(EventType type, uint32_t timestamp) {
    Event event = { type, 0u, timestamp };
    (void)Queue_Push(Queues_GetEventQueue(), &event, sizeof(event));
}

static void push_fault(uint32_t fault_code, uint32_t actuator_id, uint32_t timestamp) {
    ActuatorFaultRecord fault = { fault_code, actuator_id, timestamp };
    (void)Queue_Push(Queues_GetFaultQueue(), &fault, sizeof(fault));
}

static void report_output_failure(ActuatorId id, uint32_t timestamp) {
    push_fault(ACTUATOR_FAULT_CODE_OUTPUT_DRIVER_FAILURE, (uint32_t)id, timestamp);
    push_event(EVENT_ACTUATOR_FAULT, timestamp);

    s_consecutive_output_failures[id]++;
    if (s_consecutive_output_failures[id] >= OUTPUT_FAILURE_RESET_THRESHOLD) {
        /* Reset فقط متمرکز است - Actuator Management خودش MCU را Reset
         * نمی‌کند، فقط درخواست می‌دهد (docs/02-management-contracts/reset-request-contract.md). */
        ResetRequest reset_request;
        reset_request.type      = RESET_TYPE_SYSTEM_RESET;
        reset_request.target    = "MCU";
        reset_request.source    = RESET_SOURCE_ACTUATOR;
        reset_request.reason    = RESET_REASON_RECOVERY_FAILED;
        reset_request.timestamp = timestamp;
        System_SubmitResetRequest(&reset_request);
        s_consecutive_output_failures[id] = 0u;
    }
}

static void execute(const ActuatorRequest* request) {
    uint32_t       now    = Time_GetTick();
    bool           active  = command_is_active(request->command);
    HWProxy_Result result  = HWProxy_WriteDigitalLine(line_for(request->actuator), active);

    if (result != HWPROXY_OK) {
        report_output_failure(request->actuator, now);
        return; /* Output was not actually changed - State must not claim otherwise */
    }
    s_consecutive_output_failures[request->actuator] = 0u;

    s_state[request->actuator].command   = request->command;
    s_state[request->actuator].active    = active;
    s_state[request->actuator].timestamp = now;
}

void Actuator_Init(void) {
    for (uint32_t i = 0; i < NUM_ACTUATORS; i++) {
        s_state[i].command   = ACTUATOR_CMD_OFF;
        s_state[i].active    = false;
        s_state[i].timestamp = 0u;
        s_consecutive_output_failures[i] = 0u;
    }
}

void Actuator_Process(void) {
    ActuatorRequest request;
    uint32_t        length;

    if (!Queue_Pop(Queues_GetActuatorRequestQueue(), &request, sizeof(request), &length)) {
        return; /* هیچ Request‌ای در صف نبود - Super Loop نباید منتظر بماند */
    }

    /* Defense-in-depth: Actuator_SubmitRequest() از قبل ساختاری اعتبارسنجی
     * کرده، اما هیچ‌وقت به یک Byte Blob پاپ‌شده از صف کورکورانه اعتماد نکن. */
    if (!is_valid_actuator(request.actuator) || !is_valid_command(request.command)) {
        push_fault(ACTUATOR_FAULT_CODE_INVALID_REQUEST, (uint32_t)request.actuator, Time_GetTick());
        return;
    }

    execute(&request);
}

bool Actuator_SubmitRequest(const ActuatorRequest* request) {
    uint32_t now = Time_GetTick();

    if (request == NULL || !is_valid_actuator(request->actuator) || !is_valid_command(request->command)) {
        uint32_t fault_code   = (request != NULL && !is_valid_actuator(request->actuator))
                                     ? ACTUATOR_FAULT_CODE_INVALID_ACTUATOR
                                     : ACTUATOR_FAULT_CODE_INVALID_REQUEST;
        uint32_t actuator_id  = (request != NULL) ? (uint32_t)request->actuator : 0xFFFFFFFFu;
        push_fault(fault_code, actuator_id, now);
        push_event(EVENT_ACTUATOR_COMMAND_REJECTED, now);
        return false;
    }

    (void)Queue_Push(Queues_GetActuatorRequestQueue(), request, sizeof(*request));
    push_event(EVENT_ACTUATOR_COMMAND_ACCEPTED, now);
    return true;
}

ActuatorState Actuator_GetState(ActuatorId id) {
    if (!is_valid_actuator(id)) {
        ActuatorState safe_default = { ACTUATOR_CMD_OFF, false, 0u };
        return safe_default;
    }
    return s_state[id];
}
