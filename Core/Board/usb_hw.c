#include "usb_hw.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_hal_pcd.h"
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* ========================= */
void usb_hw_init(void)
{
    /* nothing for now */
}

/* =========================
   SOFT RESET (SAFE FOR TINYUSB)
========================= */
void usb_hw_reset_peripheral(void)
{
    /* stop peripheral */
    HAL_PCD_Stop(&hpcd_USB_DRD_FS);

    /* DO NOT full DeInit (breaks TinyUSB state machine) */
    HAL_PCD_Suspend(&hpcd_USB_DRD_FS);

    /* small settle delay */
    for (volatile int i = 0; i < 2000; i++)
        __NOP();
}

/* =========================
   DEVICE MODE
========================= */
void usb_hw_enable_device(void)
{
    usb_hw_reset_peripheral();

    /* re-enable peripheral only */
    HAL_PCD_Init(&hpcd_USB_DRD_FS);
    HAL_PCD_Start(&hpcd_USB_DRD_FS);
}

/* =========================
   HOST MODE (placeholder)
========================= */
void usb_hw_enable_host(void)
{
    usb_hw_reset_peripheral();

    /* TODO later:
       - HCD init (UHCI-like or TinyUSB host stack)
    */
}

/* ========================= */
void usb_hw_deinit(void)
{
    HAL_PCD_Stop(&hpcd_USB_DRD_FS);
}