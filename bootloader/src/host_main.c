#include "bootloader.h"
#include <stdio.h>

/*
 * Real main() only for the host (PC) build - for exercising the full
 * Download -> Install -> Reset -> Program -> CRC -> Bank Switch pipeline
 * end-to-end without real hardware (see CMakeLists.txt), same purpose
 * firmware/'s own host_main.c already serves for that project. This file
 * must never be added to any CubeIDE Bootloader target project
 * (mcu_f4_boot/, later mcu_f1_boot/) - there, CubeMX's own generated
 * main.c/startup code is the real entry point, calling Bootloader_Run()
 * directly right after Reset, before any Application code runs (see
 * bootloader.h's own comment).
 *
 * Unlike firmware/'s Firmware_Main() (an infinite Super Loop that never
 * returns), Bootloader_Run() always returns on the Host double (see
 * boot_hal_host_stub.c's Flash_JumpToApplication()) - either having
 * "jumped" (printed and returned) or having deliberately stayed in
 * Bootloader mode with no valid image anywhere. Either way this process
 * exits normally right after, which is the correct Host simulation of
 * "the Bootloader's job for this boot is done".
 */

int main(void) {
    printf("[bootloader] starting (Host simulation)\n");
    Bootloader_Run();
    printf("[bootloader] Bootloader_Run() returned - exiting\n");
    return 0;
}
