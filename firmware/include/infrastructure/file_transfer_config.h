#ifndef INFRA_FILE_TRANSFER_CONFIG_H
#define INFRA_FILE_TRANSFER_CONFIG_H

/* All tunable constants for File Transfer Infrastructure, collected in
 * one place - same convention as
 * managements/hardware_input_management_config.h. */

/* Largest single chunk FileTransfer_SupplyChunk() will accept/buffer -
 * an implementation limit of this file, not part of the architecture
 * Contract. */
#define FILE_TRANSFER_MAX_CHUNK_BYTES 256u

/* If no chunk is successfully written for this many ticks while
 * TRANSFERRING, the transfer is failed with FILE_TRANSFER_FAULT_TIMEOUT
 * (file-transfer-infrastructure.md section 7). Value is a placeholder -
 * no product-level timing decision exists for this yet. */
#define FILE_TRANSFER_TIMEOUT_TICKS 30000u /* 30s at 1 tick/ms */

#endif /* INFRA_FILE_TRANSFER_CONFIG_H */
