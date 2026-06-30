#include "main.h"

/**
  * Initialize global MSP
  */
void HAL_MspInit(void)
{
  /* USER CODE BEGIN MspInit 0 */
  /* USER CODE END MspInit 0 */

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* USER CODE BEGIN MspInit 1 */
  /* USER CODE END MspInit 1 */
}

/**
  * USB PCD MSP Init (CLEAN - no USB stack logic)
  */
void HAL_PCD_MspInit(PCD_HandleTypeDef* hpcd)
{
  if (hpcd->Instance == USB_DRD_FS)
  {
    /* Enable VDDUSB early (required for USB PHY) */
    HAL_PWREx_EnableVddUSB();

    /* Enable USB peripheral clock */
    __HAL_RCC_USB_CLK_ENABLE();

    /* USB IRQ enable (handled later by TinyUSB or wrapper) */
    HAL_NVIC_SetPriority(USB_DRD_FS_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
  }
}

/**
  * USB PCD MSP DeInit
  */
void HAL_PCD_MspDeInit(PCD_HandleTypeDef* hpcd)
{
  if (hpcd->Instance == USB_DRD_FS)
  {
    __HAL_RCC_USB_CLK_DISABLE();

    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
  }
}