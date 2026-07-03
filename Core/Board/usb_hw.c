#include "usb_hw.h"
#include "main.h"
#include <stdbool.h>

/* =========================================================
   INTERNAL RESET (shared)
   ========================================================= */

static void usb_hw_force_stop(void)
{
    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);

    __HAL_RCC_USB_FORCE_RESET();
    for (volatile int i = 0; i < 1000; i++) { __NOP(); }
    __HAL_RCC_USB_RELEASE_RESET();

    __HAL_RCC_USB_CLK_DISABLE();

    HAL_Delay(10);
}

/* =========================================================
   EXPORTED RESET (FIX for linker)
   ========================================================= */

void usb_hw_reset_peripheral(void)
{
    /* THIS MUST EXIST because usb_manager calls it */
    usb_hw_force_stop();
}

/* =========================================================
   VBUS
   ========================================================= */

void usb_hw_set_vbus(bool enabled)
{
    HAL_GPIO_WritePin(VBUS_ON_GPIO_Port,
                      VBUS_ON_Pin,
                      enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* =========================================================
   VBUS sense
   ========================================================= */

void board_vbus_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

bool board_vbus_present(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET);
}

/* =========================================================
   INIT BASE
   ========================================================= */

static void usb_hw_base_init(void)
{
    HAL_PWREx_EnableVddUSB();

    __HAL_RCC_USB_CLK_ENABLE();

    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

/* =========================================================
   DEVICE
   ========================================================= */

void usb_hw_enable_device(void)
{
    usb_hw_reset_peripheral();

    usb_hw_set_vbus(false);
    HAL_Delay(20);

    usb_hw_base_init();
}

/* =========================================================
   HOST
   ========================================================= */

void usb_hw_enable_host(void)
{
    usb_hw_reset_peripheral();

    usb_hw_set_vbus(true);
    HAL_Delay(80);

    usb_hw_base_init();
}

/* =========================================================
   DEINIT
   ========================================================= */

void usb_hw_deinit(void)
{
    usb_hw_reset_peripheral();
    usb_hw_set_vbus(false);
}