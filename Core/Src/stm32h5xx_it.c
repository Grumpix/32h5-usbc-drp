#include "main.h"
#include "stm32h5xx_it.h"
#include "tusb.h"
#include "usb_manager.h"
#include "ucpd_diag.h"
#include "m16c_vnc_bus.h"

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
    ucpd_diag_irq();
}

/* =========================
   M16C / VNC1L BUS IRQ

   PB1 = USB1_RD#
   EXTI1 je pouzite pro zachyceni hran RD#.
   V m16c_vnc_bus.c se podle hrany RD# posouva paced FIFO.
========================= */
void EXTI1_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_1) != 0U)
    {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_1);
        m16c_vnc_bus_rd_irq();
    }
}