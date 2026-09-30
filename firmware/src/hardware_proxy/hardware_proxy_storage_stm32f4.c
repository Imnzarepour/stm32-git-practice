#include "hardware_proxy/hardware_proxy_storage.h"
#include "eeprom.h"
#include <string.h>
#include <stdbool.h>

/* Real STM32F4 implementation of the Hardware Proxy Storage domain,
 * backed by ST's EEPROM Emulation middleware (EE_Init/EE_ReadVariable/
 * EE_WriteVariable - the standard "emulate EEPROM in internal Flash"
 * driver ST ships for both the F1 and F4 families under this same name,
 * from the EEPROM Emulation Application Note/STM32Cube example, e.g.
 * AN3969 for F4 / AN2594 for F1). Using the same entry points on both
 * families is exactly why this file's logic is expected to be portable,
 * near-verbatim, to hardware_proxy_storage_stm32f1.c once MCU_F1 has a
 * real CubeIDE project (today MCU_F1/ only has a pin list, no generated
 * project yet - same situation hardware_proxy_digital_io_stm32f1.c is
 * already left pending for, see hardware-proxy.md section 7).
 *
 * eeprom.h/eeprom.c are ST's own middleware, NOT part of firmware/. They
 * must be added to the MCU_F4 CubeIDE project the same manual way
 * target-integration.md documents for firmware/ itself (Include Path +
 * Linked/Source Folder) - typically pulled from the EEPROM Emulation
 * example matching this MCU or generated via STM32CubeMX. This file only
 * calls the standard EE_* entry points; it does not reimplement page
 * management or wear leveling.
 *
 * This file is only meant to be compiled by the MCU_F4 CubeIDE project,
 * never the host (PC) build - same reasoning as
 * hardware_proxy_digital_io_stm32f4.c. CMakeLists.txt already excludes
 * every "*_stm32f4.c" file from the host build; hardware_proxy_storage.c
 * (the host-stub, same function names) is used there instead.
 *
 * ST's EE_ReadVariable/EE_WriteVariable operate on one 16-bit "virtual
 * address" -> 16-bit value slot at a time (virtual address 0 is reserved
 * by ST's own convention, so real slots start at 1). This file packs the
 * Config Region's raw byte range onto consecutive virtual-address slots
 * and unpacks them back on read, so Storage Infrastructure (and
 * everything above it) stays byte-oriented and never has to know
 * virtual addresses exist. */

#define CONFIG_REGION_CAPACITY_BYTES        512u /* must stay >= STORAGE_CONFIG_CAPACITY_BYTES, infrastructure/storage.h */
#define CONFIG_REGION_FIRST_VIRTUAL_ADDRESS 1u    /* virtual address 0 is reserved by EE_* convention */
#define CONFIG_REGION_SLOT_COUNT            ((CONFIG_REGION_CAPACITY_BYTES + 1u) / 2u) /* 2 bytes per virtual-address slot */

static bool s_initialized = false;

HWProxy_StorageResult HWProxy_Storage_Init(void) {
    if (!s_initialized) {
        /* 0 is EE_Init()'s success code in ST's EEPROM Emulation
         * middleware (same convention EE_ReadVariable/EE_WriteVariable
         * use below), not re-declared here as a named constant since
         * that constant lives in ST's own eeprom.h, not this file. */
        s_initialized = (EE_Init() == 0);
    }
    return s_initialized ? HWPROXY_STORAGE_OK : HWPROXY_STORAGE_ERROR;
}

static HWProxy_StorageResult read_slot(uint16_t slot_index, uint16_t* half_word) {
    return (EE_ReadVariable((uint16_t)(CONFIG_REGION_FIRST_VIRTUAL_ADDRESS + slot_index), half_word) == 0)
               ? HWPROXY_STORAGE_OK : HWPROXY_STORAGE_ERROR;
}

static HWProxy_StorageResult write_slot(uint16_t slot_index, uint16_t half_word) {
    return (EE_WriteVariable((uint16_t)(CONFIG_REGION_FIRST_VIRTUAL_ADDRESS + slot_index), half_word) == 0)
               ? HWPROXY_STORAGE_OK : HWPROXY_STORAGE_ERROR;
}

HWProxy_StorageResult HWProxy_Storage_Read(HWProxy_StorageRegion region, uint32_t offset, void* buffer, uint32_t length) {
    if (region != HWPROXY_STORAGE_REGION_CONFIG || buffer == NULL) {
        return HWPROXY_STORAGE_INVALID;
    }
    if (offset > CONFIG_REGION_CAPACITY_BYTES || length > CONFIG_REGION_CAPACITY_BYTES - offset) {
        return HWPROXY_STORAGE_INVALID;
    }
    if (HWProxy_Storage_Init() != HWPROXY_STORAGE_OK) {
        return HWPROXY_STORAGE_ERROR;
    }

    uint8_t* out = (uint8_t*)buffer;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t byte_pos = offset + i;
        uint16_t slot = (uint16_t)(byte_pos / 2u);
        uint16_t half_word;
        if (read_slot(slot, &half_word) != HWPROXY_STORAGE_OK) {
            return HWPROXY_STORAGE_ERROR;
        }
        out[i] = (byte_pos % 2u == 0u) ? (uint8_t)(half_word & 0xFFu) : (uint8_t)(half_word >> 8);
    }
    return HWPROXY_STORAGE_OK;
}

HWProxy_StorageResult HWProxy_Storage_Write(HWProxy_StorageRegion region, uint32_t offset, const void* data, uint32_t length) {
    if (region != HWPROXY_STORAGE_REGION_CONFIG || data == NULL) {
        return HWPROXY_STORAGE_INVALID;
    }
    if (offset > CONFIG_REGION_CAPACITY_BYTES || length > CONFIG_REGION_CAPACITY_BYTES - offset) {
        return HWPROXY_STORAGE_INVALID;
    }
    if (HWProxy_Storage_Init() != HWPROXY_STORAGE_OK) {
        return HWPROXY_STORAGE_ERROR;
    }

    const uint8_t* in = (const uint8_t*)data;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t byte_pos = offset + i;
        uint16_t slot = (uint16_t)(byte_pos / 2u);

        /* Read-modify-write: EE_WriteVariable always writes a full 16-bit
         * slot, so a byte-granular update to one half of a slot must
         * preserve the other half's current value. A slot that has never
         * been written before is treated as zero, not an error. */
        uint16_t half_word;
        if (read_slot(slot, &half_word) != HWPROXY_STORAGE_OK) {
            half_word = 0;
        }

        if (byte_pos % 2u == 0u) {
            half_word = (uint16_t)((half_word & 0xFF00u) | in[i]);
        } else {
            half_word = (uint16_t)((half_word & 0x00FFu) | ((uint16_t)in[i] << 8));
        }

        if (write_slot(slot, half_word) != HWPROXY_STORAGE_OK) {
            return HWPROXY_STORAGE_ERROR;
        }
    }
    return HWPROXY_STORAGE_OK;
}

HWProxy_StorageResult HWProxy_Storage_Erase(HWProxy_StorageRegion region) {
    if (region != HWPROXY_STORAGE_REGION_CONFIG) {
        return HWPROXY_STORAGE_INVALID;
    }
    if (HWProxy_Storage_Init() != HWPROXY_STORAGE_OK) {
        return HWPROXY_STORAGE_ERROR;
    }

    /* ST's EEPROM Emulation manages its own page erase/wear-leveling
     * internally (triggered from EE_WriteVariable once the active page
     * fills up) - there is no direct "erase this variable" primitive, so
     * this writes zero to every slot in the Region instead. */
    for (uint16_t slot = 0; slot < CONFIG_REGION_SLOT_COUNT; slot++) {
        if (write_slot(slot, 0) != HWPROXY_STORAGE_OK) {
            return HWPROXY_STORAGE_ERROR;
        }
    }
    return HWPROXY_STORAGE_OK;
}
