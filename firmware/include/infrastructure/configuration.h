#ifndef INFRA_CONFIGURATION_H
#define INFRA_CONFIGURATION_H
#include <stdint.h>
#include <stdbool.h>

/* Contract source: docs/03-infrastructure-contracts/configuration-infrastructure.md
 *
 * This is an Infrastructure, not a Management (see that document's own
 * section 0): it owns no product Capability, has no FSM, and needs no
 * independent Process() in the Super Loop - it only Loads at Startup and
 * serves Get/Update on demand. It sits next to Time Base / Queue / Storage
 * / Driver Layer, and the project's 10-Management list is unaffected.
 *
 * Flow (section 6/8 of the Contract):
 *   Startup -> Configuration_Init() reads STORAGE_CONFIG and populates the
 *   single Runtime Configuration (DeviceConfig). A owner Management later
 *   calls Configuration_Update() with one already-decided field value;
 *   the new value is written to persistent storage FIRST, and Runtime is
 *   only updated after that write succeeds - so RAM and persistent memory
 *   never disagree. If the write fails, Runtime keeps its old value.
 *
 * No validation here, by explicit user decision: this Infrastructure does
 * not judge whether a value is a legal phone number, a well-formed URL,
 * etc. - that decision belongs entirely to the owner Management (section 9
 * of the Contract: "Configuration Infrastructure does not own the meaning
 * of the data"). The only check this file performs is a byte-length match
 * against the field's known storage size, which is a memory-safety
 * boundary check, not a business rule.
 *
 * v1 field list (PROVISIONAL): the Contract explicitly leaves the final
 * DeviceConfig field list open ("determined at Data Model definition
 * stage" - section 2). The fields below are taken directly from that
 * section's own examples plus the one concrete real consumer already
 * found in the codebase (Allowed Phone Numbers - see
 * managements/call_management.c). Anyone closing the real Data Model
 * later only has to touch this file (one struct member + one ConfigField
 * enum entry + one s_field_layout row in configuration.c) - no consumer
 * above this layer needs to change shape just because a field was added.
 */

/* --- Per-field size limits (each field gets its own named constant,
 * same convention as hardware_input_management_config.h, since each is
 * independently likely to change as the real Data Model is closed). --- */
#define CONFIG_DEVICE_ID_MAX_LENGTH        24u
#define CONFIG_SERIAL_NUMBER_MAX_LENGTH    16u
#define CONFIG_HARDWARE_VERSION_MAX_LENGTH  8u
#define CONFIG_BATCH_INFO_MAX_LENGTH       16u
#define CONFIG_SERVER_ADDRESS_MAX_LENGTH   64u
#define CONFIG_PHONE_NUMBER_MAX_LENGTH     20u
#define CONFIG_MAX_ALLOWED_PHONE_NUMBERS    8u

/* --- Device Identity (Contract section 2/4) --- */
typedef struct {
    char device_id[CONFIG_DEVICE_ID_MAX_LENGTH];
} ConfigDeviceIdentity;

/* --- Hardware / Production Information (Contract section 2/4) --- */
typedef struct {
    char serial_number[CONFIG_SERIAL_NUMBER_MAX_LENGTH];
    char hardware_version[CONFIG_HARDWARE_VERSION_MAX_LENGTH];
    char batch_info[CONFIG_BATCH_INFO_MAX_LENGTH];
} ConfigHardwareProductionInfo;

/* --- Communication Configuration (Contract section 2/4) ---
 * Three environments named explicitly in the Contract: Sandbox,
 * Deployment, Operation. "Communication Protocol / Parameters" (also
 * listed in section 2) is left out of v1 - unlike Server Address and
 * Allowed Phone Numbers, nothing in the codebase needs it yet, and
 * inventing its shape now would be exactly the kind of guess CLAUDE.md's
 * golden rule forbids. */
/* Which of the three environments below server_management.c's
 * mqtt_transport_connect() actually dials - added because Server Address/
 * Port existed for all three environments already (Configuration_Set*()
 * below), but nothing decided which one was "live" at runtime;
 * mqtt_transport_connect() used to just always read *_sandbox at compile
 * time. Default (LoadFactoryDefaults(), configuration.c) is SANDBOX - same
 * "safest choice at this stage of development" reasoning
 * server_management_config.h already documented. */
typedef enum {
    CONFIG_SERVER_ENV_SANDBOX,
    CONFIG_SERVER_ENV_DEPLOYMENT,
    CONFIG_SERVER_ENV_OPERATION
} ConfigServerEnvironment;

typedef struct {
    char server_address_sandbox[CONFIG_SERVER_ADDRESS_MAX_LENGTH];
    char server_address_deployment[CONFIG_SERVER_ADDRESS_MAX_LENGTH];
    char server_address_operation[CONFIG_SERVER_ADDRESS_MAX_LENGTH];
    /* MQTT Port paired with each Server Address above - added so
     * EC200U_MQTT_Connect() (infrastructure/ec200u_manager.h) has a real
     * Host AND Port to work with; Server Address alone used to sit here
     * without anything in the firmware actually reading it (see the
     * former TODO this replaces in server_management_config.h). */
    uint16_t server_port_sandbox;
    uint16_t server_port_deployment;
    uint16_t server_port_operation;
    ConfigServerEnvironment active_environment;
} ConfigCommunication;

/* --- Access Configuration (Contract section 2/4) ---
 * allowed_phone_numbers: Owner Call Management (Contract section 9 -
 * "each field has one semantic owner") - managements/call_management.c's
 * Call_IsNumberAllowed() decides how the list is used.
 * sms_alert_destination / call_alert_destination: Owner SMS Management /
 * Call Management respectively - the outgoing destination each dials/
 * texts for a Policy-originated alert (shared/event_policy.c's
 * SMS_QUEUE_ITEM_ALERT / CALL_QUEUE_ITEM_ALERT dispatch). This
 * Infrastructure only stores the bytes; it does not judge whether a
 * number is well-formed, same as every other Field (section 9 above). */
typedef struct {
    char number[CONFIG_PHONE_NUMBER_MAX_LENGTH];
} ConfigPhoneNumber;

typedef struct {
    uint32_t          count;
    ConfigPhoneNumber numbers[CONFIG_MAX_ALLOWED_PHONE_NUMBERS];
} ConfigAllowedPhoneNumbers;

typedef struct {
    ConfigAllowedPhoneNumbers allowed_phone_numbers;
    char sms_alert_destination[CONFIG_PHONE_NUMBER_MAX_LENGTH];
    char call_alert_destination[CONFIG_PHONE_NUMBER_MAX_LENGTH];
} ConfigAccess;

/* Product Configuration (the 5th category named in Contract section 4)
 * has no concrete field anywhere in the project yet, so it is
 * intentionally not modeled here - adding it later is a single nested
 * struct field, same pattern as every category below. */
typedef struct {
    ConfigDeviceIdentity         identity;
    ConfigHardwareProductionInfo production;
    ConfigCommunication          communication;
    ConfigAccess                 access;
} DeviceConfig;

/* One entry per updatable field. Order does not matter; adding a field
 * only means adding one value here plus the matching struct member above
 * and one s_field_layout row in configuration.c. */
typedef enum {
    CONFIG_FIELD_DEVICE_ID,
    CONFIG_FIELD_SERIAL_NUMBER,
    CONFIG_FIELD_HARDWARE_VERSION,
    CONFIG_FIELD_BATCH_INFO,
    CONFIG_FIELD_SERVER_ADDRESS_SANDBOX,
    CONFIG_FIELD_SERVER_ADDRESS_DEPLOYMENT,
    CONFIG_FIELD_SERVER_ADDRESS_OPERATION,
    CONFIG_FIELD_SERVER_PORT_SANDBOX,
    CONFIG_FIELD_SERVER_PORT_DEPLOYMENT,
    CONFIG_FIELD_SERVER_PORT_OPERATION,
    CONFIG_FIELD_ACTIVE_ENVIRONMENT,
    CONFIG_FIELD_ALLOWED_PHONE_NUMBERS,
    CONFIG_FIELD_SMS_ALERT_DESTINATION,
    CONFIG_FIELD_CALL_ALERT_DESTINATION,
    CONFIG_FIELD_COUNT
} ConfigField;

typedef enum {
    CONFIG_OK,
    CONFIG_BUSY,   /* Storage is currently busy (Storage_Write() returned STORAGE_BUSY) - Runtime unchanged, caller may retry later */
    CONFIG_ERROR   /* Invalid Field/length/value, or the persistent write failed - Runtime unchanged either way */
} ConfigResult;

/* Startup only (Contract section 5): reads STORAGE_CONFIG and populates
 * Runtime Configuration. If the read fails (e.g. nothing persisted yet,
 * or Storage itself is not ready), Runtime falls back to the factory
 * defaults (configuration.c's LoadFactoryDefaults() - explicit user
 * decision: real placeholder values, not all-zero/blank, and the single
 * place a future Factory Reset feature will also call). */
void Configuration_Init(void);

/* Read-Only, Non-Blocking, no side effects - same access pattern as
 * Power_GetMode(). Never returns NULL after Configuration_Init(). */
const DeviceConfig* Configuration_Get(void);

/* Persists `value` (exactly `length` bytes, matching the Field's own
 * size) to STORAGE_CONFIG, then - only if that write succeeds - copies it
 * into Runtime Configuration. Covers the Contract's "Update Configuration"
 * and "Persist Configuration" as one combined step, matching the single
 * flow diagrammed in section 8 (write-then-update-runtime is not two
 * separately callable operations here). */
ConfigResult Configuration_Update(ConfigField field, const void* value, uint32_t length);

/* --- Small single-field setters (explicit user request) ---
 * Each one is exactly Configuration_Update() underneath, for the one
 * Field its name says - same write-then-update-runtime behavior, same
 * CONFIG_OK/CONFIG_BUSY/CONFIG_ERROR contract. They exist so a caller can
 * just say "set the serial number" with a plain C-string, without ever
 * touching ConfigField, sizeof(), or this Field's own buffer size.
 *
 * The given string is copied into this Field's fixed-size buffer and
 * always NUL-terminated; an oversized string is silently truncated to
 * fit rather than rejected - the same convention SMS/Call Management
 * already use for oversized input (see e.g.
 * managements/sms_management.c). A NULL pointer is rejected
 * (CONFIG_ERROR), same as Configuration_Update() itself. */
ConfigResult Configuration_SetDeviceId(const char* device_id);
ConfigResult Configuration_SetSerialNumber(const char* serial_number);
ConfigResult Configuration_SetHardwareVersion(const char* hardware_version);
ConfigResult Configuration_SetBatchInfo(const char* batch_info);
ConfigResult Configuration_SetServerAddressSandbox(const char* address);
ConfigResult Configuration_SetServerAddressDeployment(const char* address);
ConfigResult Configuration_SetServerAddressOperation(const char* address);

/* Paired MQTT Port setters - see ConfigCommunication's comment above for
 * why these exist alongside the Server Address setters. */
ConfigResult Configuration_SetServerPortSandbox(uint16_t port);
ConfigResult Configuration_SetServerPortDeployment(uint16_t port);
ConfigResult Configuration_SetServerPortOperation(uint16_t port);

/* Selects which environment's Address/Port pair mqtt_transport_connect()
 * (server_management.c) actually dials. Unlike the plain string/port
 * setters above, this one DOES reject a value outside ConfigServerEnvironment
 * (CONFIG_ERROR) - an out-of-range byte here is not "unusual input to
 * store as-is" like an oversized string, it is a value the caller-side
 * switch (server_management.c) cannot safely act on at all. */
ConfigResult Configuration_SetActiveServerEnvironment(ConfigServerEnvironment environment);

/* Replaces the whole Allowed Phone Numbers list in one call. `numbers` is
 * a plain array of C-strings, `count` is how many entries it has; a
 * `count` above CONFIG_MAX_ALLOWED_PHONE_NUMBERS is silently capped
 * (truncate, not reject - same convention as the string setters above),
 * and a NULL entry found before reaching `count` stops the list there
 * rather than trusting whatever garbage `count` claims past it. The
 * caller never has to know ConfigPhoneNumber/ConfigAllowedPhoneNumbers
 * exist. */
ConfigResult Configuration_SetAllowedPhoneNumbers(const char* const* numbers, uint32_t count);

/* SMS/Call Management's own outgoing alert destination - see ConfigAccess's
 * comment above. Same truncate-not-reject convention as the other string
 * setters. */
ConfigResult Configuration_SetSmsAlertDestination(const char* number);
ConfigResult Configuration_SetCallAlertDestination(const char* number);

#endif /* INFRA_CONFIGURATION_H */
