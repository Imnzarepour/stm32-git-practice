#include "firmware_main.h"

/*
 * Real main() only for the host (PC) build - for compiling/testing/
 * developing locally without hardware (see CMakeLists.txt). This file
 * must never be added to any CubeIDE target project (MCU_F4, later
 * MCU_F1); there, CubeMX already generates the real main() in
 * Core/Src/main.c, and that one must call Firmware_Main() directly.
 * Full explanation in docs/03-infrastructure-contracts/target-integration.md.
 */

int main(void) {
    Firmware_Main();
    return 0;
}
