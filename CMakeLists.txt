cmake_minimum_required(VERSION 3.20)

project(stm32h533 C ASM)

set(CMAKE_C_STANDARD 11)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# =========================
# DEFINES
# =========================
add_compile_definitions(
    STM32H533xx
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER

    CFG_TUSB_MCU=OPT_MCU_STM32H5
    CFG_TUSB_OS=OPT_OS_NONE

    CFG_TUSB_RHPORT0_MODE=OPT_MODE_DEVICE
    CFG_TUSB_INIT_MANUAL=1

    TUSB_OPT_DEVICE_ENABLED=1
    TUSB_OPT_HOST_ENABLED=1

    CFG_TUD_NET=0
    CFG_TUD_ENABLED=1
    CFG_TUH_ENABLED=0
)

# =========================
# SOURCES
# =========================
file(GLOB_RECURSE APP_SOURCES
    Core/*.c
)

set(TINYUSB_ROOT ${CMAKE_SOURCE_DIR}/Core/USB/tinyusb)
set(TINYUSB_SRC ${TINYUSB_ROOT}/src)

set(TINYUSB_SOURCES
    ${TINYUSB_SRC}/tusb.c
    ${TINYUSB_SRC}/common/tusb_fifo.c
    ${TINYUSB_SRC}/device/usbd.c
    ${TINYUSB_SRC}/class/cdc/cdc_device.c

    ${TINYUSB_SRC}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
)

set(USB_PORT_SRC
    Core/USB/port/dcd_stm32h5.c
    Core/USB/port/tusb_port.c
)

add_executable(${PROJECT_NAME}.elf
    ${APP_SOURCES}
    ${TINYUSB_SOURCES}
    ${USB_PORT_SRC}
)

# =========================
# INCLUDE PATHS (FIXED)
# =========================
target_include_directories(${PROJECT_NAME}.elf PRIVATE

    # APP
    Core/Inc
    Core/App
    Core/USB
    Core/USB/port
    Core/Board

    # 🔥 TinyUSB ROOT FIX (důležité!)
    ${TINYUSB_SRC}

    # HAL
    Drivers/STM32H5xx_HAL_Driver/Inc
    Drivers/STM32H5xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H5xx/Include
    Drivers/CMSIS/Include
)

# =========================
# FLAGS
# =========================
target_compile_options(${PROJECT_NAME}.elf PRIVATE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard

    -Wall
    -Wextra

    -ffunction-sections
    -fdata-sections

    -O0
    -g3
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32H533RETX_FLASH.ld
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -Wl,--gc-sections
)