cmake_minimum_required(VERSION 3.20)
project(stm32h533 C ASM)

set(CMAKE_C_STANDARD 11)

# =========================
# MCU DEFINE
# =========================
add_definitions(
    -DSTM32H533xx
    -DUSE_HAL_DRIVER
    -DCFG_TUSB_MCU=OPT_MCU_STM32H5
)

# =========================
# SOURCES (CubeMX)
# =========================
file(GLOB_RECURSE SOURCES
    Core/*.c
    Drivers/*.c
    USB/port/*.c
)

# =========================
# HAL + CMSIS INCLUDES
# =========================
include_directories(
    Core/Inc
    Drivers/STM32H5xx_HAL_Driver/Inc
    Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H5xx/Include
    Drivers/CMSIS/Include

    USB/tinyusb/src
    USB/port
)

# =========================
# EXECUTABLE
# =========================
add_executable(${PROJECT_NAME}.elf ${SOURCES})

# =========================
# LINKER SCRIPT
# =========================
target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32H533RETX_FLASH.ld
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -Wl,--gc-sections
)

# =========================
# COMPILER FLAGS
# =========================
target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -Wall
    -ffunction-sections
    -fdata-sections
    -O0
    -g3
)