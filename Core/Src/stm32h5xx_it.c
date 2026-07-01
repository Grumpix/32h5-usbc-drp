#include "main.h"
#include "stm32h5xx_it.h"
#include "tusb.h"
#include "usb_manager.h"

/* =========================
   Cortex handlers
========================= */
void NMI_Handler(void) { while (1) {} }
void HardFault_Handler(void) { while (1) {} }
void MemManage_Handler(void) { while (1) {} }
void BusFault_Handler(void) { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* =========================
   USB IRQ (TINYUSB OWNED)
========================= */
void USB_DRD_FS_IRQHandler(void)
{
    if (usb_manager_get_mode() == USB_MODE_HOST)
    {
        tuh_int_handler(0);
    }
    else if (usb_manager_get_mode() == USB_MODE_DEVICE)
    {
        tud_int_handler(0);
    }
}

/* =========================
   UCPD DRP IRQ
========================= */
extern void drp_irq_handler(void);

void UCPD1_IRQHandler(void)
{
    drp_irq_handler();
}