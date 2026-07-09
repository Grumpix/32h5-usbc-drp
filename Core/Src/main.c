#include "main.h"

#include "uart.h"
#include "usb_manager.h"
#include "drp_fsm.h"
#include "ucpd_diag.h"
#include "usb_mode_button.h"


void SystemClock_Config(void);


void MPU_Config(void)
{
}


void MX_GPIO_Init(void)
{
}


void MX_ICACHE_Init(void)
{
}


int main(void)
{
    MPU_Config();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ICACHE_Init();

    uart_init();
    uart_write_str("BOOT UART OK\r\n");

    HAL_Delay(5);

    drp_init();

    usb_manager_init();

    ucpd_diag_init();

    usb_mode_button_init();

    while(1)
    {
        drp_task();

        if(usb_mode_button_pressed())
        {
            ucpd_diag_toggle_role();
        }

        ucpd_diag_task();

        usb_manager_task();
    }
}