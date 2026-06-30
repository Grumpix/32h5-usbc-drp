#include "main.h"
#include "stm32h5xx_it.h"

/* External variables */
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* Cortex handlers (beze změny) */
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

/**
  * USB interrupt
  *
  * CRITICAL CHANGE:
  * - HAL_PCD_IRQHandler removed from control path
  * - interrupt is now hook point for TinyUSB / USB manager
  */
void USB_DRD_FS_IRQHandler(void)
{
    /* NO HAL_PCD_IRQHandler here */

    tud_int_handler(0);   // TinyUSB ISR entry (device)
}
extern void drp_irq_handler(void);

void UCPD1_IRQHandler(void)
{
  /* USER CODE BEGIN UCPD1_IRQn 0 */

  /* USER CODE END UCPD1_IRQn 0 */

  drp_irq_handler();

  /* USER CODE BEGIN UCPD1_IRQn 1 */

  /* USER CODE END UCPD1_IRQn 1 */
}