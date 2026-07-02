#include "usb_hw.h"
#include "main.h"
#include <stdbool.h>

/* =========================================================
   VBUS power switch (output control)
   ========================================================= */

void usb_hw_set_vbus(bool enabled)
{
    HAL_GPIO_WritePin(VBUS_ON_GPIO_Port,
                      VBUS_ON_Pin,
                      enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* =========================================================
   VBUS sense (NUCLEO = PC4)
   ========================================================= */

void board_vbus_init(void)
{
    /* Enable GPIOC clock */
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* Configure PC4 as input (VBUS detect) */
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin  = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

bool board_vbus_present(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET);
}

/* =========================================================
   USB peripheral init
   ========================================================= */

void usb_hw_init(void)
{
    HAL_PWREx_EnableVddUSB();

    __HAL_RCC_USB_CLK_ENABLE();

    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void usb_hw_reset_peripheral(void)
{
    __HAL_RCC_USB_FORCE_RESET();

    for (volatile uint32_t i = 0; i < 64; i++)
    {
        __NOP();
    }

    __HAL_RCC_USB_RELEASE_RESET();
}

/* =========================================================
   Mode control
   ========================================================= */

void usb_hw_enable_device(void)
{
    usb_hw_set_vbus(false);
    usb_hw_init();
    usb_hw_reset_peripheral();
}

void usb_hw_enable_host(void)
{
    usb_hw_set_vbus(true);
    HAL_Delay(20);

    usb_hw_init();
    usb_hw_reset_peripheral();
}

void usb_hw_deinit(void)
{
    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    usb_hw_set_vbus(false);
    __HAL_RCC_USB_CLK_DISABLE();
}