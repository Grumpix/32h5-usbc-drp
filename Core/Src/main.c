#include "main.h"
#include "uart.h"
#include "usb_manager.h"
#include "drp_fsm.h"
#include "ucpd_diag.h"


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
     * HOST/SOURCE bring-up test:
     *
     * ucpd_diag_init() nastavi USB-C Source/Rp rezim.
     * TinyUSB host se NESPOUSTI hned.
     * Spusti se az po:
     * - CC attach
     * - VBUS FET ON
     * - VBUS PRESENT
     * - kratke prodleve
     */
    ucpd_diag_init();

    usb_manager_init();

    /*
     * DULEZITE:
     *
     * Pro tento HOST test uz NEstartujeme device jako default.
     *
     * NE:
     * usb_manager_start_device();
     *
     * Host se spusti z ucpd_diag_task(), az bude validni Type-C source attach.
     */

    while (1)
    {
        drp_task();

        ucpd_diag_task();

        usb_manager_task();
    }
}