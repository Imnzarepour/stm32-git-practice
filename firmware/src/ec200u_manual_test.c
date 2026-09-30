#include "ec200u_manual_test.h"
#include "infrastructure/ec200u_manager.h"
#include "infrastructure/time_base.h"
#include <string.h>

/* See ec200u_manual_test.h's own top comment - this file is a deliberately
 * simple, Blocking bring-up test harness for real MCU_F4 hardware, not
 * part of any Management or the production Super Loop. It only ever calls
 * through infrastructure/ec200u_manager.h's public Capability Contract -
 * it never touches AT Commands/UART directly, same boundary every other
 * consumer of EC200U Manager already respects. */

#define EC200UTEST_MODULE_READY_TIMEOUT_MS 20000u /* Power-Settle + full AT Init sequence, generous */
#define EC200UTEST_SMS_SEND_TIMEOUT_MS     20000u
#define EC200UTEST_CALL_TIMEOUT_MS         30000u
#define EC200UTEST_CALL_HANGUP_TIMEOUT_MS   5000u
#define EC200UTEST_MQTT_CONNECT_TIMEOUT_MS 30000u
#define EC200UTEST_MQTT_PUBLISH_PUMP_MS     5000u

void EC200UTest_Init(void) {
    Time_Init();
    EC200U_Init();
}

bool EC200UTest_WaitModuleReady(uint32_t timeout_ms) {
    TimeTick start = Time_GetTick();
    while (!Time_Elapsed(start, timeout_ms)) {
        EC200U_Process();
        if (EC200U_GetModuleState() == EC200U_MODULE_READY) {
            return true;
        }
    }
    return false;
}

bool EC200UTest_SendHelloWorldSms(const char* destination_number) {
    EC200U_SmsData sms;
    TimeTick        start;

    if (destination_number == NULL) {
        return false;
    }

    sms.destination    = destination_number;
    sms.payload          = "Hello World";
    sms.payload_length   = (uint32_t)strlen(sms.payload);

    if (EC200U_SMS_Send(&sms) != EC200U_OK) {
        return false; /* module not READY, or a send is already in flight */
    }

    start = Time_GetTick();
    while (!Time_Elapsed(start, EC200UTEST_SMS_SEND_TIMEOUT_MS)) {
        EC200U_Process();
        EC200U_Result status = EC200U_SMS_GetSendStatus();
        if (status == EC200U_OK) {
            return true;
        }
        if (status == EC200U_ERROR) {
            return false;
        }
        /* EC200U_BUSY - still in progress (module prompt/SMSC round trip), keep waiting */
    }
    return false; /* timeout */
}

bool EC200UTest_WaitForIncomingSms(char* sender_out, uint32_t sender_out_size,
                                    char* text_out, uint32_t text_out_size,
                                    uint32_t timeout_ms) {
    TimeTick start;

    if (sender_out == NULL || sender_out_size == 0u || text_out == NULL || text_out_size == 0u) {
        return false;
    }

    start = Time_GetTick();
    while (!Time_Elapsed(start, timeout_ms)) {
        EC200U_Process();

        EC200U_SmsData sms;
        if (EC200U_SMS_GetReceived(&sms) == EC200U_OK) {
            strncpy(sender_out, sms.destination ? sms.destination : "", sender_out_size - 1u);
            sender_out[sender_out_size - 1u] = '\0';

            uint32_t copy_len = sms.payload_length;
            if (copy_len > text_out_size - 1u) {
                copy_len = text_out_size - 1u;
            }
            if (sms.payload != NULL && copy_len > 0u) {
                memcpy(text_out, sms.payload, copy_len);
            }
            text_out[copy_len] = '\0';
            return true;
        }
    }
    return false; /* nothing arrived within timeout_ms */
}

bool EC200UTest_MakeCall(const char* number, uint32_t hold_ms) {
    TimeTick start;

    if (EC200U_Call_Dial(number) != EC200U_OK) {
        return false;
    }

    start = Time_GetTick();
    while (!Time_Elapsed(start, EC200UTEST_CALL_TIMEOUT_MS)) {
        EC200U_Process();
        EC200U_CallState state = EC200U_Call_GetState();

        if (state == EC200U_CALL_ACTIVE) {
            TimeTick hold_start = Time_GetTick();
            while (!Time_Elapsed(hold_start, hold_ms)) {
                EC200U_Process();
                if (EC200U_Call_GetState() != EC200U_CALL_ACTIVE) {
                    break; /* far end hung up early */
                }
            }
            (void)EC200U_Call_Hangup();
            TimeTick hangup_start = Time_GetTick();
            while (!Time_Elapsed(hangup_start, EC200UTEST_CALL_HANGUP_TIMEOUT_MS)
                   && EC200U_Call_GetState() != EC200U_CALL_ENDED) {
                EC200U_Process();
            }
            return true;
        }
        if (state == EC200U_CALL_ENDED) {
            return false; /* rejected/failed before ever becoming ACTIVE */
        }
    }
    return false; /* timeout - never answered */
}

bool EC200UTest_WaitForIncomingCallAndAnswer(uint32_t timeout_ms, uint32_t hold_ms) {
    TimeTick start = Time_GetTick();

    while (!Time_Elapsed(start, timeout_ms)) {
        EC200U_Process();

        if (EC200U_Call_GetState() == EC200U_CALL_RINGING) {
            if (EC200U_Call_Answer() != EC200U_OK) {
                return false;
            }

            TimeTick answer_start = Time_GetTick();
            while (!Time_Elapsed(answer_start, EC200UTEST_CALL_HANGUP_TIMEOUT_MS)) {
                EC200U_Process();
                if (EC200U_Call_GetState() == EC200U_CALL_ACTIVE) {
                    TimeTick hold_start = Time_GetTick();
                    while (!Time_Elapsed(hold_start, hold_ms)) {
                        EC200U_Process();
                        if (EC200U_Call_GetState() != EC200U_CALL_ACTIVE) {
                            break;
                        }
                    }
                    (void)EC200U_Call_Hangup();
                    TimeTick hangup_start = Time_GetTick();
                    while (!Time_Elapsed(hangup_start, EC200UTEST_CALL_HANGUP_TIMEOUT_MS)
                           && EC200U_Call_GetState() != EC200U_CALL_ENDED) {
                        EC200U_Process();
                    }
                    return true;
                }
            }
            return false; /* answered locally but never actually became ACTIVE */
        }
    }
    return false; /* nothing rang within timeout_ms */
}

bool EC200UTest_ConnectServer(const char* client_id, const char* host, uint16_t port,
                               uint32_t timeout_ms) {
    TimeTick start;

    if (EC200U_MQTT_Connect(client_id, host, port) != EC200U_OK) {
        return false;
    }

    start = Time_GetTick();
    while (!Time_Elapsed(start, timeout_ms)) {
        EC200U_Process();
        EC200U_MqttState state = EC200U_MQTT_GetState();
        if (state == EC200U_MQTT_CONNECTED) {
            return true;
        }
        if (state == EC200U_MQTT_DISCONNECTED) {
            return false; /* QMTOPEN or QMTCONN itself was rejected - see mqtt_process() in ec200u_manager.c */
        }
    }
    return false; /* timeout */
}

bool EC200UTest_SendHelloWorldToServer(const char* topic) {
    static const uint8_t payload[] = "Hello World";
    EC200U_MqttMessage    message;
    TimeTick              start;

    message.topic          = topic;
    message.payload        = payload;
    message.payload_length = (uint32_t)(sizeof(payload) - 1u); /* exclude the string's own NUL */

    if (EC200U_MQTT_Publish(&message) != EC200U_OK) {
        return false;
    }

    /* Fire-and-forget Capability (see ec200u_manager.h's own comment on
     * EC200U_MQTT_Publish) - just pump Process() long enough for the real
     * AT+QMTPUB sequence to actually leave the UART, do not wait for a
     * Backend acknowledgment that does not exist at this layer. */
    start = Time_GetTick();
    while (!Time_Elapsed(start, EC200UTEST_MQTT_PUBLISH_PUMP_MS)) {
        EC200U_Process();
    }
    return true;
}
