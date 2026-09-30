cmake_minimum_required(VERSION 3.16)
project(saipa_tcu_firmware C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

file(GLOB_RECURSE SOURCES
    src/*.c
)

# Files with a real-target suffix (e.g. *_stm32f4.c) call real HAL/CMSIS
# functions and only compile with the matching ARM toolchain + STM32 HAL
# headers (wired into that board's CubeIDE project). They must not be
# part of this host (PC) build, which uses plain gcc; the matching
# *_stm32f4.c is compiled for MCU_F4 instead of the host-stub with the
# same base name. See docs/03-infrastructure-contracts/target-integration.md.
list(FILTER SOURCES EXCLUDE REGEX ".*_stm32f4\\.c$")
list(FILTER SOURCES EXCLUDE REGEX ".*_stm32f1\\.c$")

add_executable(firmware ${SOURCES})

# نکته: این یک اسکلت Build میزبان (Host) برای کامپایل و تست اولیه Contractهاست،
# نه Build نهایی برای MCU هدف. Toolchain واقعی (Cross Compiler) باید جداگانه
# توسط تیم embedded تنظیم شود.
