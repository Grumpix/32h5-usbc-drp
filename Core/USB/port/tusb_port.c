#include "tusb.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_ll_pwr.h"
#include "stm32h5xx_ll_bus.h"

/* =========================
   LOW LEVEL INIT (STM32H5)
========================= */
static inline void usb_lowlevel_init(void)
{
    /* USB clock MUST be enabled (jinak HAL PCD spadne) */
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

    /* Power / USB regulator safety (H5 requirement) */
    __HAL_RCC_PWR_CLK_ENABLE();
}

/* =========================
   DEVICE INIT ENTRY
========================= */
void usb_port_init(void)
{
    usb_lowlevel_init();

    /* IMPORTANT:
       ONLY TinyUSB device stack init
       (host init nesmí být tady)
    */
    tud_init(0);
}