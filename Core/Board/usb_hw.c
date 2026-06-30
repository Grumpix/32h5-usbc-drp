#include "usb_hw.h"
#include "main.h"
#include "stm32h5xx_hal.h"

/* external HAL handle (CubeMX) */
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* =========================
   INIT (BASE CLOCKS READY)
========================= */

void usb_hw_init(void)
{
    /* USB peripheral clock is already enabled in MSP */
    /* Nothing heavy here */
}

/* =========================
   HARD RESET USB PERIPHERAL
   (CRITICAL FOR ROLE SWITCH)
========================= */

void usb_hw_reset_peripheral(void)
{
    /* Stop USB */
    HAL_PCD_Stop(&hpcd_USB_DRD_FS);

    /* Force reset */
    __HAL_RCC_USB_FORCE_RESET();
    for (volatile int i = 0; i < 1000; i++);
    __HAL_RCC_USB_RELEASE_RESET();

    /* Re-init HAL state */
    HAL_PCD_DeInit(&hpcd_USB_DRD_FS);
}

/* =========================
   DEVICE MODE SETUP
========================= */

void usb_hw_enable_device(void)
{
    usb_hw_reset_peripheral();

    /* Re-init peripheral in DEVICE context */
    HAL_PCD_Init(&hpcd_USB_DRD_FS);

    HAL_PCD_Start(&hpcd_USB_DRD_FS);
}

/* =========================
   HOST MODE SETUP
========================= */

void usb_hw_enable_host(void)
{
    usb_hw_reset_peripheral();

    /* NOTE:
       Host mode will NOT use HAL PCD stack in final design
       This is placeholder for UHCI/TinyUSB host init layer
    */

    HAL_PCD_Init(&hpcd_USB_DRD_FS);
    HAL_PCD_Start(&hpcd_USB_DRD_FS);
}

/* =========================
   DEINIT FULL STOP
========================= */

void usb_hw_deinit(void)
{
    HAL_PCD_Stop(&hpcd_USB_DRD_FS);
    HAL_PCD_DeInit(&hpcd_USB_DRD_FS);
}