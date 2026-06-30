#include "tusb.h"
#include "stm32h5xx.h"
#include "stm32h5xx_hal.h"

/* extern HAL handle */
extern PCD_HandleTypeDef hpcd_USB_DRD_FS;

/* =========================
   INIT
========================= */
void dcd_init(uint8_t rhport)
{
  (void) rhport;

  __HAL_RCC_USB_CLK_ENABLE();

  __HAL_RCC_USB_FORCE_RESET();
  for (volatile int i = 0; i < 50; i++) __NOP();
  __HAL_RCC_USB_RELEASE_RESET();
}

/* =========================
   IRQ HANDLER (REAL FIX)
========================= */
void dcd_int_handler(uint8_t rhport)
{
  (void) rhport;

  /* THIS is correct STM32 HAL hook */
  HAL_PCD_IRQHandler(&hpcd_USB_DRD_FS);
}

/* =========================
   CONNECT / DISCONNECT
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