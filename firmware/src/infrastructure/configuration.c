#include "infrastructure/configuration.h"
#include "infrastructure/storage.h"
#include <stddef.h>
#include <string.h>

/* Contract source: docs/03-infrastructure-contracts/configuration-infrastructure.md
 * See configuration.h for the shape/flow decisions. This file only knows
 * how to move bytes between DeviceConfig (RAM) and STORAGE_CONFIG
 * (Storage Infrastructure) - it has no opinion about what any field means. */

static DeviceConfig s_runtime;

typedef struct {
    uint32_t offset; /* byte offset of the field inside DeviceConfig */
    uint32_t size;   /* exact byte size of the field */
} ConfigFieldLayout;

/* Single source of truth for where each Field lives inside DeviceConfig.
 * offsetof/sizeof are computed from the struct itself, so a field can
 * never silently drift out of sync with its own layout - the only manual
 * work when adding a field is adding its row here. */
static const ConfigFieldLayout s_field_layout[CONFIG_FIELD_COUNT] = {
    [CONFIG_FIELD_DEVICE_ID] = {
        offsetof(DeviceConfig, identity.device_id),
        sizeof(((DeviceConfig*)0)->identity.device_id)
    },
    [CONFIG_FIELD_SERIAL_NUMBER] = {
        offsetof(DeviceConfig, production.serial_number),
        sizeof(((DeviceConfig*)0)->production.serial_number)
    },
    [CONFIG_FIELD_HARDWARE_VERSION] = {
        offsetof(DeviceConfig, production.hardware_version),
        sizeof(((DeviceConfig*)0)->production.hardware_version)
    },
    [CONFIG_FIELD_BATCH_INFO] = {
        offsetof(DeviceConfig, production.batch_info),
        sizeof(((DeviceConfig*)0)->production.batch_info)
    },
    [CONFIG_FIELD_SERVER_ADDRESS_SANDBOX] = {
        offsetof(DeviceConfig, communication.server_address_sandbox),
        sizeof(((DeviceConfig*)0)->communication.server_address_sandbox)
    },
    [CONFIG_FIELD_SERVER_ADDRESS_DEPLOYMENT] = {
        offsetof(DeviceConfig, communication.server_address_deployment),
        sizeof(((DeviceConfig*)0)->communication.server_address_deployment)
    },
    [CONFIG_FIELD_SERVER_ADDRESS_OPERATION] = {
        offsetof(DeviceConfig, communication.server_address_operation),
        sizeof(((DeviceConfig*)0)->communication.server_address_operation)
    },
    [CONFIG_FIELD_SERVER_PORT_SANDBOX] = {
        offsetof(DeviceConfig, communication.server_port_sandbox),
        sizeof(((DeviceConfig*)0)->communication.server_port_sandbox)
    },
    [CONFIG_FIELD_SERVER_PORT_DEPLOYMENT] = {
        offsetof(DeviceConfig, communication.server_port_deployment),
        sizeof(((DeviceConfig*)0)->communication.server_port_deployment)
    },
    [CONFIG_FIELD_SERVER_PORT_OPERATION] = {
        offsetof(DeviceConfig, communication.server_port_operation),
        sizeof(((DeviceConfig*)0)->communication.server_port_operation)
    },
    [CONFIG_FIELD_ACTIVE_ENVIRONMENT] = {
        offsetof(DeviceConfig, communication.active_environment),
        sizeof(((DeviceConfig*)0)->communication.active_environment)
    },
    [CONFIG_FIELD_ALLOWED_PHONE_NUMBERS] = {
        offsetof(DeviceConfig, access.allowed_phone_numbers),
        sizeof(((DeviceConfig*)0)->access.allowed_phone_numbers)
    },
    [CONFIG_FIELD_SMS_ALERT_DESTINATION] = {
        offsetof(DeviceConfig, access.sms_alert_destination),
        sizeof(((DeviceConfig*)0)->access.sms_alert_destination)
    },
    [CONFIG_FIELD_CALL_ALERT_DESTINATION] = {
        offsetof(DeviceConfig, access.call_alert_destination),
        sizeof(((DeviceConfig*)0)->access.call_alert_destination)
    },
};

/* Keeps DeviceConfig from silently growing past the capacity Storage
 * Infrastructure reserves for STORAGE_CONFIG - a compile error here is
 * the intended signal to go bump STORAGE_CONFIG_CAPACITY_BYTES
 * (infrastructure/storage.h), not a runtime surprise. */
_Static_assert(sizeof(DeviceConfig) <= STORAGE_CONFIG_CAPACITY_BYTES,
               "DeviceConfig no longer fits in its reserved Storage capacity");

/* PLACEHOLDER factory defaults - explicit user decision: every Field gets
 * a real (non-empty/non-zero) value here, not a blank string, so a
 * freshly-flashed or never-provisioned device is still recognizable
 * instead of showing up as all-empty. Every value below is a stand-in
 * pending real production data (same spirit as the old
 * call_management_allowed_numbers.h placeholder this replaced) - replace
 * them here, and only here, when the real values are known.
 *
 * This is also the single reuse point for a future Factory Reset
 * feature (not implemented yet - out of scope here): that feature would
 * call this same function and then persist every Field back to Storage,
 * instead of duplicating any of these values elsewhere. */
static void LoadFactoryDefaults(DeviceConfig* config) {
    memset(config, 0, sizeof(*config));

    strncpy(config->identity.device_id, "UNSET-DEVICE-ID", CONFIG_DEVICE_ID_MAX_LENGTH - 1);

    strncpy(config->production.serial_number, "UNSET-SN", CONFIG_SERIAL_NUMBER_MAX_LENGTH - 1);
    strncpy(config->production.hardware_version, "UNSET", CONFIG_HARDWARE_VERSION_MAX_LENGTH - 1);
    strncpy(config->production.batch_info, "UNSET-BATCH", CONFIG_BATCH_INFO_MAX_LENGTH - 1);

    strncpy(config->communication.server_address_sandbox, "sandbox.example.com", CONFIG_SERVER_ADDRESS_MAX_LENGTH - 1);
    strncpy(config->communication.server_address_deployment, "deployment.example.com", CONFIG_SERVER_ADDRESS_MAX_LENGTH - 1);
    strncpy(config->communication.server_address_operation, "operation.example.com", CONFIG_SERVER_ADDRESS_MAX_LENGTH - 1);

    /* 0 is the PLACEHOLDER port for all three environments - same
     * "recognizable, not real" spirit as the hostnames above, just the
     * numeric-field equivalent of an UNSET string (0 is never a valid
     * destination port). Real values are known هکار infrastructure
     * (server_management_config.h's SERVER_HEKAR_*_MQTT_PORT) but those
     * are documentation only, not a claim about what this device is
     * actually provisioned with - replace via Configuration_SetServerPort*()
     * with real provisioning data, not by changing this default. */
    config->communication.server_port_sandbox    = 0u;
    config->communication.server_port_deployment = 0u;
    config->communication.server_port_operation   = 0u;

    /* Sandbox by default - same "safest choice at this stage of
     * development" reasoning server_management_config.h already
     * documented for the old compile-time-only behavior this replaces. */
    config->communication.active_environment = CONFIG_SERVER_ENV_SANDBOX;

    /* SECURITY-SENSITIVE - explicit user decision (unlike every other
     * Field above, an empty list here is the safer choice per
     * call-management.md's Fail-Closed policy; the user asked for a
     * default owner number anyway). "+980000000099" is the same
     * PLACEHOLDER number call_management_allowed_numbers.h used to carry
     * - it is NOT a real authorized number. Replace it here before this
     * is ever relied on in production. */
    config->access.allowed_phone_numbers.count = 1;
    strncpy(config->access.allowed_phone_numbers.numbers[0].number, "+980000000099", CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);

    /* Same migration as allowed_phone_numbers above, just for the two
     * outgoing alert destinations: these carry over the exact values
     * SMS_ALERT_DESTINATION_PLACEHOLDER / CALL_ALERT_DESTINATION_PLACEHOLDER
     * used before sms_management.c/call_management.c read them from here,
     * so behavior is unchanged until real numbers are provisioned. */
    strncpy(config->access.sms_alert_destination, "+980000000000", CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);
    strncpy(config->access.call_alert_destination, "+980000000001", CONFIG_PHONE_NUMBER_MAX_LENGTH - 1);
}

void Configuration_Init(void) {
    DeviceConfig loaded;
    if (Storage_Read(STORAGE_CONFIG, 0, &loaded, sizeof(loaded)) == STORAGE_OK) {
        s_runtime = loaded;
    } else {
        /* Nothing valid persisted yet (or Storage is not ready) - fall
         * back to the factory defaults above. */
        LoadFactoryDefaults(&s_runtime);
    }
}

const DeviceConfig* Configuration_Get(void) {
    return &s_runtime;
}

ConfigResult Configuration_Update(ConfigField field, const void* value, uint32_t length) {
    if (field >= CONFIG_FIELD_COUNT || value == NULL) {
        return CONFIG_ERROR;
    }

    const ConfigFieldLayout* layout = &s_field_layout[field];
    if (length != layout->size) {
        return CONFIG_ERROR; /* caller passed a size that does not match this Field's own Contract */
    }

    StorageResult write_result = Storage_Write(STORAGE_CONFIG, layout->offset, value, length);
    if (write_result == STORAGE_BUSY) {
        return CONFIG_BUSY;
    }
    if (write_result != STORAGE_OK) {
        return CONFIG_ERROR; /* Runtime stays untouched - no RAM/persistent divergence (Contract section 6/8) */
    }

    memcpy((uint8_t*)&s_runtime + layout->offset, value, length);
    return CONFIG_OK;
}

/* Shared body for every single-string setter below: copies `value` into a
 * `buffer_size`-byte local buffer (zero-filled first, so a short string is
 * always fully NUL-padded, and a `buffer_size - 1`-byte-or-longer one is
 * still NUL-terminated - truncated, never overflowed), then hands it to
 * Configuration_Update() for the given Field. */
static ConfigResult SetFixedString(ConfigField field, uint32_t buffer_size, const char* value) {
    if (value == NULL) {
        return CONFIG_ERROR;
    }

    char buffer[CONFIG_SERVER_ADDRESS_MAX_LENGTH]; /* large enough for every string Field today */
    memset(buffer, 0, sizeof(buffer));
    strncpy(buffer, value, buffer_size - 1u);

    return Configuration_Update(field, buffer, buffer_size);
}

ConfigResult Configuration_SetDeviceId(const char* device_id) {
    return SetFixedString(CONFIG_FIELD_DEVICE_ID, CONFIG_DEVICE_ID_MAX_LENGTH, device_id);
}

ConfigResult Configuration_SetSerialNumber(const char* serial_number) {
    return SetFixedString(CONFIG_FIELD_SERIAL_NUMBER, CONFIG_SERIAL_NUMBER_MAX_LENGTH, serial_number);
}

ConfigResult Configuration_SetHardwareVersion(const char* hardware_version) {
    return SetFixedString(CONFIG_FIELD_HARDWARE_VERSION, CONFIG_HARDWARE_VERSION_MAX_LENGTH, hardware_version);
}

ConfigResult Configuration_SetBatchInfo(const char* batch_info) {
    return SetFixedString(CONFIG_FIELD_BATCH_INFO, CONFIG_BATCH_INFO_MAX_LENGTH, batch_info);
}

ConfigResult Configuration_SetServerAddressSandbox(const char* address) {
    return SetFixedString(CONFIG_FIELD_SERVER_ADDRESS_SANDBOX, CONFIG_SERVER_ADDRESS_MAX_LENGTH, address);
}

ConfigResult Configuration_SetServerAddressDeployment(const char* address) {
    return SetFixedString(CONFIG_FIELD_SERVER_ADDRESS_DEPLOYMENT, CONFIG_SERVER_ADDRESS_MAX_LENGTH, address);
}

ConfigResult Configuration_SetServerAddressOperation(const char* address) {
    return SetFixedString(CONFIG_FIELD_SERVER_ADDRESS_OPERATION, CONFIG_SERVER_ADDRESS_MAX_LENGTH, address);
}

ConfigResult Configuration_SetServerPortSandbox(uint16_t port) {
    return Configuration_Update(CONFIG_FIELD_SERVER_PORT_SANDBOX, &port, sizeof(port));
}

ConfigResult Configuration_SetServerPortDeployment(uint16_t port) {
    return Configuration_Update(CONFIG_FIELD_SERVER_PORT_DEPLOYMENT, &port, sizeof(port));
}

ConfigResult Configuration_SetServerPortOperation(uint16_t port) {
    return Configuration_Update(CONFIG_FIELD_SERVER_PORT_OPERATION, &port, sizeof(port));
}

ConfigResult Configuration_SetActiveServerEnvironment(ConfigServerEnvironment environment) {
    if (environment != CONFIG_SERVER_ENV_SANDBOX
        && environment != CONFIG_SERVER_ENV_DEPLOYMENT
        && environment != CONFIG_SERVER_ENV_OPERATION) {
        return CONFIG_ERROR;
    }
    return Configuration_Update(CONFIG_FIELD_ACTIVE_ENVIRONMENT, &environment, sizeof(environment));
}

ConfigResult Configuration_SetSmsAlertDestination(const char* number) {
    return SetFixedString(CONFIG_FIELD_SMS_ALERT_DESTINATION, CONFIG_PHONE_NUMBER_MAX_LENGTH, number);
}

ConfigResult Configuration_SetCallAlertDestination(const char* number) {
    return SetFixedString(CONFIG_FIELD_CALL_ALERT_DESTINATION, CONFIG_PHONE_NUMBER_MAX_LENGTH, number);
}

ConfigResult Configuration_SetAllowedPhoneNumbers(const char* const* numbers, uint32_t count) {
    if (numbers == NULL) {
        return CONFIG_ERROR;
    }

    ConfigAllowedPhoneNumbers value;
    memset(&value, 0, sizeof(value));

    uint32_t usable_count = (count > CONFIG_MAX_ALLOWED_PHONE_NUMBERS) ? CONFIG_MAX_ALLOWED_PHONE_NUMBERS : count;
    for (uint32_t i = 0; i < usable_count; i++) {
        if (numbers[i] == NULL) {
            usable_count = i; /* a NULL entry ends the list here - never trust a count past it */
            break;
        }
        strncpy(value.numbers[i].number, numbers[i], CONFIG_PHONE_NUMBER_MAX_LENGTH - 1u);
    }
    value.count = usable_count;

    return Configuration_Update(CONFIG_FIELD_ALLOWED_PHONE_NUMBERS, &value, sizeof(value));
}
