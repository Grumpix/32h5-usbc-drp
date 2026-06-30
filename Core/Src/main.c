#include "main.h"
#include "usb_manager.h"
#include "drp_fsm.h"

PCD_HandleTypeDef hpcd_USB_DRD_FS;

void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ICACHE_Init(void);
static void MX_UCPD1_Init(void);

int main(void)
{
    MPU_Config();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ICACHE_Init();
    MX_UCPD1_Init();

    HAL_Delay(5);

    drp_init();

    while (!drp_is_ready())
    {
        drp_task();
    }

    usb_manager_init();

    if (drp_get_role() == USB_MODE_DEVICE)
        usb_manager_start_device();
    else if (drp_get_role() == USB_MODE_HOST)
        usb_manager_start_host();

    while (1)
    {
        drp_task();
        usb_manager_task();
    }
}