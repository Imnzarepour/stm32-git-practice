#include "shared/queues.h"

static uint8_t s_event_buf[EVENT_QUEUE_CAPACITY_BYTES];
static uint8_t s_sms_buf[SMS_REQUEST_QUEUE_CAPACITY_BYTES];
static uint8_t s_call_buf[CALL_REQUEST_QUEUE_CAPACITY_BYTES];
static uint8_t s_server_event_buf[SERVER_EVENT_QUEUE_CAPACITY_BYTES];
static uint8_t s_telemetry_buf[TELEMETRY_QUEUE_CAPACITY_BYTES];
static uint8_t s_fault_buf[FAULT_QUEUE_CAPACITY_BYTES];
static uint8_t s_actuator_request_buf[ACTUATOR_REQUEST_QUEUE_CAPACITY_BYTES];
static uint8_t s_log_buf[LOG_QUEUE_CAPACITY_BYTES];

static Queue s_event_queue;
static Queue s_sms_request_queue;
static Queue s_call_request_queue;
static Queue s_server_event_queue;
static Queue s_telemetry_queue;
static Queue s_fault_queue;
static Queue s_actuator_request_queue;
static Queue s_log_queue;

void Queues_Init(void) {
    Queue_Init(&s_event_queue,            s_event_buf,            sizeof(s_event_buf));
    Queue_Init(&s_sms_request_queue,      s_sms_buf,              sizeof(s_sms_buf));
    Queue_Init(&s_call_request_queue,     s_call_buf,             sizeof(s_call_buf));
    Queue_Init(&s_server_event_queue,     s_server_event_buf,     sizeof(s_server_event_buf));
    Queue_Init(&s_telemetry_queue,        s_telemetry_buf,        sizeof(s_telemetry_buf));
    Queue_Init(&s_fault_queue,            s_fault_buf,            sizeof(s_fault_buf));
    Queue_Init(&s_actuator_request_queue, s_actuator_request_buf, sizeof(s_actuator_request_buf));
    Queue_Init(&s_log_queue,              s_log_buf,              sizeof(s_log_buf));
}

Queue* Queues_GetEventQueue(void)           { return &s_event_queue; }
Queue* Queues_GetSmsRequestQueue(void)      { return &s_sms_request_queue; }
Queue* Queues_GetCallRequestQueue(void)     { return &s_call_request_queue; }
Queue* Queues_GetServerEventQueue(void)     { return &s_server_event_queue; }
Queue* Queues_GetTelemetryQueue(void)       { return &s_telemetry_queue; }
Queue* Queues_GetFaultQueue(void)           { return &s_fault_queue; }
Queue* Queues_GetActuatorRequestQueue(void) { return &s_actuator_request_queue; }
Queue* Queues_GetLogQueue(void)             { return &s_log_queue; }