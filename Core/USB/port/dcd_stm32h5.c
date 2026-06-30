#include "tusb.h"
#include "stm32h5xx.h"

/* extern HAL handle (musí existovat kvůli clock + base addr) */
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* =========================
   INIT
========================= */

void dcd_init(uint8_t rhport)
{
    (void) rhport;

    /* Ensure USB peripheral is clocked */
    __HAL_RCC_USB_CLK_ENABLE();

    /* Reset USB peripheral */
    __HAL_RCC_USB_FORCE_RESET();
    __NOP();
    __HAL_RCC_USB_RELEASE_RESET();
}

/* =========================
   INT HANDLER HOOK
========================= */

void dcd_int_handler(uint8_t rhport)
{
    (void) rhport;

    /* forward to TinyUSB core */
    tud_int_handler(rhport);
}

/* =========================
   PORT CONTROL
========================= */

void dcd_connect(uint8_t rhport)
{
    (void) rhport;
    HAL_PCD_Start(&hpcd_USB_DRD_FS);
}

void dcd_disconnect(uint8_t rhport)
{
    (void) rhport;
    HAL_PCD_Stop(&hpcd_USB_DRD_FS);
}