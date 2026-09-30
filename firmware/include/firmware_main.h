#ifndef FIRMWARE_MAIN_H
#define FIRMWARE_MAIN_H

/* Contract source: docs/03-infrastructure-contracts/target-integration.md
 *
 * This function is the callable entry point for the whole firmware - it
 * never returns (it is the Super Loop itself). It is deliberately not
 * named `main()` because every real target (each board's STM32CubeIDE
 * project, e.g. MCU_F4 and later MCU_F1) already has its own CubeMX-
 * generated main() in Core/Src/main.c that does HAL_Init/
 * SystemClock_Config/MX_*_Init; that main() must call this function at
 * the end (where CubeMX itself leaves an empty `while(1){}`).
 *
 * The host (PC) build used for local test/development has its own real
 * main() in src/host_main.c, which just calls this function - that file
 * must never be added to a real MCU build (a CubeIDE project), since
 * CubeMX already generates the real main() there. */

void Firmware_Main(void);

#endif /* FIRMWARE_MAIN_H */
