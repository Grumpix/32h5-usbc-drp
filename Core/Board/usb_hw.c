#include "usb_hw.h"
#include "main.h"

void usb_hw_irq_enable(void)
{
    HAL_NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void usb_hw_irq_disable(void)
{
    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
}

void usb_hw_init(void)
{
    HAL_PWREx_EnableVddUSB();
    usb_hw_irq_disable();
    __HAL_RCC_USB_CLK_ENABLE();
}

void usb_hw_reset_peripheral(void)
{
    usb_hw_irq_disable();
    __HAL_RCC_USB_FORCE_RESET();

    for (volatile uint32_t delay = 0; delay < 64; ++delay)
    {
        __NOP();
    }

    __HAL_RCC_USB_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
}

void usb_hw_enable_device(void)
{
    usb_hw_init();
    usb_hw_reset_peripheral();
}

void usb_hw_enable_host(void)
{
    usb_hw_init();
    usb_hw_reset_peripheral();
}

void usb_hw_deinit(void)
{
    usb_hw_irq_disable();
    usb_hw_reset_peripheral();
    __HAL_RCC_USB_CLK_DISABLE();
}