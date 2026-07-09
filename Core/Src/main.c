#include "main.h"

#include "uart.h"
#include "usb_manager.h"
#include "drp_fsm.h"
#include "ucpd_diag.h"
#include "usb_role_policy.h"
#include "app_usb_status_reporter.h"
#include "app_cli_transport.h"


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

    usb_role_policy_init();

    app_usb_status_reporter_init();

    app_cli_transport_init();

    while(1)
    {
        drp_task();

        usb_role_policy_task();

        ucpd_diag_task();

        usb_manager_task();

        app_usb_status_reporter_task();

        app_cli_transport_task();
    }
}