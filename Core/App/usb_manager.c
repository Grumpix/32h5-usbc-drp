#include "usb_manager.h"

#include "main.h"
#include "uart.h"
#include "tusb.h"

#include <stdbool.h>
#include <stdint.h>


/*
 * Local app hooks.
 */
void usb_device_tusb_init(void);
void usb_device_tusb_task(void);
void usb_device_tusb_deinit(void);

void usb_host_tusb_init(void);
void usb_host_tusb_task(void);
void usb_host_tusb_task_log(void);
void usb_host_tusb_deinit(void);


static usb_mode_t usb_manager_mode =
    USB_MODE_NONE;


static void usb_manager_force_usb_drd_reset(const char *reason)
{
    uart_write_str("[USB-DRD] FORCE RESET: ");
    uart_write_str(reason);
    uart_write_str("\r\n");

    /*
     * DULEZITE:
     *
     * Tento reset pouzivame jen pro realny prechod DEVICE -> HOST,
     * tedy az po tom, co CDC device stack uz bezel.
     *
     * Nepouzivat pri:
     * - usb_manager_init()
     * - start_device()
     * - beznem bootu
     *
     * Jinak se rozbije CDC enumerace.
     */

    HAL_NVIC_DisableIRQ(
        USB_DRD_FS_IRQn);

    HAL_NVIC_ClearPendingIRQ(
        USB_DRD_FS_IRQn);

    __DSB();
    __ISB();

    __HAL_RCC_USB_FORCE_RESET();

    for(volatile uint32_t i = 0U; i < 50000U; i++)
    {
        __NOP();
    }

    __HAL_RCC_USB_RELEASE_RESET();

    for(volatile uint32_t i = 0U; i < 50000U; i++)
    {
        __NOP();
    }

    /*
     * Po peripheral resetu nechame USB clock zapnuty.
     * TinyUSB init si periferii znovu nastaví pro host/device.
     */
    __HAL_RCC_USB_CLK_ENABLE();

    HAL_NVIC_ClearPendingIRQ(
        USB_DRD_FS_IRQn);

    HAL_NVIC_EnableIRQ(
        USB_DRD_FS_IRQn);

    uart_write_str("[USB-DRD] FORCE RESET DONE\r\n");
}


void usb_manager_init(void)
{
    usb_manager_mode =
        USB_MODE_NONE;

    uart_write_str("[USB] manager init\r\n");
}


bool usb_manager_start_device(void)
{
    uart_write_str("[USB-DEVICE] START\r\n");

    if(usb_manager_mode == USB_MODE_DEVICE)
    {
        uart_write_str("[USB-DEVICE] already active\r\n");
        return true;
    }

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB-DEVICE] stopping host first\r\n");

        usb_host_tusb_deinit();

        usb_manager_mode =
            USB_MODE_NONE;
    }

    uart_write_str("[USB-DEVICE] BEFORE usb_device_tusb_init\r\n");

    usb_device_tusb_init();

    uart_write_str("[USB-DEVICE] AFTER usb_device_tusb_init\r\n");

    /*
     * Mode nastavime pred tud_init(), aby USB interrupt behem initu
     * sel spravne do device handleru.
     */
    usb_manager_mode =
        USB_MODE_DEVICE;

    uart_write_str("[USB-DEVICE] BEFORE tud_init\r\n");

    tud_init(0);

    uart_write_str("[USB-DEVICE] AFTER tud_init\r\n");

    uart_write_str("[USB] DEVICE mode\r\n");
    uart_write_str("[USB-DEVICE] START DONE\r\n");

    return true;
}


bool usb_manager_start_host(void)
{
    uart_write_str("[USB-HOST] START\r\n");

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB-HOST] already active\r\n");
        return true;
    }

    if(usb_manager_mode == USB_MODE_DEVICE)
    {
        /*
         * Fallback ochrana:
         *
         * Normalne se DEVICE -> HOST deje pres usb_manager_stop().
         * Kdyby ale nekdo zavolal start_host() primo z DEVICE,
         * musime USB DRD taky fyzicky resetovat.
         */
        uart_write_str("[USB-HOST] leaving DEVICE -> force USB DRD reset first\r\n");

        usb_device_tusb_deinit();

        usb_manager_force_usb_drd_reset(
            "direct DEVICE -> HOST");

        usb_manager_mode =
            USB_MODE_NONE;
    }

    uart_write_str("[USB-HOST] BEFORE usb_host_tusb_init\r\n");

    usb_host_tusb_init();

    uart_write_str("[USB-HOST] AFTER usb_host_tusb_init\r\n");

    /*
     * Mode nastavime pred tuh_init(), aby USB_DRD_FS_IRQHandler()
     * uz behem host initu smeroval interrupt do tuh_int_handler(0).
     */
    usb_manager_mode =
        USB_MODE_HOST;

    uart_write_str("[USB-HOST] BEFORE tuh_init\r\n");

    tuh_init(0);

    uart_write_str("[USB-HOST] AFTER tuh_init\r\n");

    uart_write_str("[USB] HOST mode\r\n");
    uart_write_str("[USB-HOST] START DONE\r\n");

    return true;
}


void usb_manager_stop(void)
{
    uart_write_str("[USB] STOP\r\n");

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB] stopping host\r\n");

        usb_host_tusb_deinit();
    }
    else if(usb_manager_mode == USB_MODE_DEVICE)
    {
        /*
         * Toto je klicovy fix:
         *
         * Po CDC enumeraci nestaci jen prepnout software stav.
         * USB DRD peripheral zustane ve stavu device stacku a host se po
         * naslednem tuh_init() nechytne.
         *
         * Proto pri opusteni DEVICE rezimu udelame fyzicky peripheral reset.
         */
        uart_write_str("[USB] stopping device -> force USB DRD reset\r\n");

        usb_device_tusb_deinit();

        usb_manager_force_usb_drd_reset(
            "DEVICE stop");
    }
    else
    {
        uart_write_str("[USB] already stopped\r\n");
    }

    usb_manager_mode =
        USB_MODE_NONE;

    uart_write_str("[USB] STOP DONE\r\n");
}


void usb_manager_task(void)
{
    if(usb_manager_mode == USB_MODE_HOST)
    {
        usb_host_tusb_task();
        usb_host_tusb_task_log();
    }
    else if(usb_manager_mode == USB_MODE_DEVICE)
    {
        usb_device_tusb_task();
    }
}


bool usb_manager_toggle_mode(void)
{
    bool ok;

    if(usb_manager_mode == USB_MODE_HOST)
    {
        uart_write_str("[USB] toggle HOST -> DEVICE\r\n");

        usb_manager_stop();

        ok =
            usb_manager_start_device();
    }
    else
    {
        uart_write_str("[USB] toggle -> HOST\r\n");

        usb_manager_stop();

        ok =
            usb_manager_start_host();
    }

    return ok;
}


bool usb_manager_is_host_active(void)
{
    return
        (usb_manager_mode == USB_MODE_HOST);
}


bool usb_manager_is_device_active(void)
{
    return
        (usb_manager_mode == USB_MODE_DEVICE);
}


bool usb_manager_is_active(void)
{
    return
        (usb_manager_mode != USB_MODE_NONE);
}


usb_mode_t usb_manager_get_mode(void)
{
    return
        usb_manager_mode;
}