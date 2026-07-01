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


    CFG_TUD_NET=0
    CFG_TUD_ENABLED=1
    CFG_TUH_ENABLED=0
)

# =========================
# SOURCES
# =========================
file(GLOB CORE_SOURCES Core/Src/*.c)
file(GLOB APP_LAYER_SOURCES Core/App/*.c)
file(GLOB BOARD_SOURCES Core/Board/*.c)

set(APP_SOURCES
    ${CORE_SOURCES}
    ${APP_LAYER_SOURCES}
    ${BOARD_SOURCES}
    Core/USB/usb_device_tusb.c
    Core/Startup/startup_stm32h533retx.s
)

set(TINYUSB_ROOT ${CMAKE_SOURCE_DIR}/Core/USB/tinyusb)
set(TINYUSB_SRC ${TINYUSB_ROOT}/src)

set(TINYUSB_SOURCES
    ${TINYUSB_SRC}/tusb.c
    ${TINYUSB_SRC}/common/tusb_fifo.c
    ${TINYUSB_SRC}/device/usbd.c
    ${TINYUSB_SRC}/class/cdc/cdc_device.c

    ${TINYUSB_SRC}/portable/st/stm32_fsdev/fsdev_common.c
    ${TINYUSB_SRC}/portable/st/stm32_fsdev/dcd_stm32_fsdev.c
)

set(HAL_SOURCES
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_cortex.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_dma_ex.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_exti.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_flash_ex.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_gpio.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_icache.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_pwr_ex.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc.c
    Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_rcc_ex.c
)

add_executable(${PROJECT_NAME}.elf
    ${APP_SOURCES}
    ${TINYUSB_SOURCES}
    ${HAL_SOURCES}
)

# =========================
# INCLUDE PATHS (FIXED)
# =========================
target_include_directories(${PROJECT_NAME}.elf PRIVATE

    # APP
    Core/Inc
    Core/App
    Core/USB
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