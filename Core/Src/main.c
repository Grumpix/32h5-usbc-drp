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

    /*
     * UCPD sink/device init.
     */
    ucpd_diag_init();

    /*
     * PA0 button -> PC13 VBUS FET test.
     *
     * DULEZITE:
     * usb_mode_button_pressed() se musi volat v main loopu,
     * protoze uvnitr teto funkce probiha polling PA0 a toggle PC13.
     */
    usb_mode_button_init();

    usb_manager_init();

    /*
     * DEFAULT USB STATE
     *
     * Pozor:
     * usb_manager_start_device() muze pres usb_hw nastavit VBUS OFF.
     * To je pro device rezim v poradku.
     * Tlacitko pak PC13 prepina rucne.
     */
    usb_manager_start_device();

    while (1)
    {
        /*
         * Tohle uz NENI USB mode override.
         *
         * V testovaci verzi usb_mode_button.c:
         * - cte PA0
         * - loguje zmeny
         * - prepina PC13
         * - vzdy vraci false
         */
        (void)usb_mode_button_pressed();

        drp_task();
        ucpd_diag_task();
        usb_manager_task();
    }
}