#include "main.h"
#include "uart.h"
#include "usb_manager.h"
#include "drp_fsm.h"
#include "ucpd_diag.h"
#include "usb_mode_button.h"

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
    ucpd_diag_init();
    usb_mode_button_init();

    usb_manager_init();

    /* DEFAULT USB STATE */
    usb_manager_start_device();

    while (1)
    {
        /* BUTTON override */
        if (usb_mode_button_pressed())
        {
            if (usb_manager_toggle_mode())
            {
                drp_request_role(
                    (usb_manager_get_mode() == USB_MODE_HOST)
                        ? DRP_ROLE_HOST
                        : DRP_ROLE_DEVICE
                );
            }
        }

        /* TASKS */
        drp_task();
        ucpd_diag_task();
        usb_manager_task();
    }
}