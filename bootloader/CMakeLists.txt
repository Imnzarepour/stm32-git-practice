cmake_minimum_required(VERSION 3.16)
project(saipa_tcu_bootloader C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

# Real per-target implementations of boot_hal.h (mcu_f4_boot/, later
# mcu_f1_boot/) do not exist yet - only boot_hal_host_stub.c does. When a
# real "*_stm32f4.c" boot_hal implementation is added under src/, exclude
# it here the same way firmware/CMakeLists.txt already excludes its own
# per-target Hardware Proxy files, and stop building boot_hal_host_stub.c
# into any CubeIDE target project (same host-stub-vs-target-file split
# docs/03-infrastructure-contracts/hardware-proxy.md section 7 documents
# for firmware/).
add_executable(bootloader
    src/bootloader.c
    src/boot_hal_host_stub.c
    src/host_main.c
)

# نکته: این یک اسکلت Build میزبان (Host) برای اجرای کامل Pipeline بدون
# سخت‌افزار واقعی است (Download -> Install -> Reset -> Program -> CRC ->
# Bank Switch، هر دو Process جدا - نگاه کن boot_hal_host_stub.c)، نه Build
# نهایی برای MCU هدف. Toolchain واقعی (Cross Compiler) و پروژه CubeIDE
# (mcu_f4_boot/) باید جداگانه توسط تیم embedded تنظیم شود.
