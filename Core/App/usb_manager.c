#include "usb_manager.h"

#include "main.h"
#include "uart.h"
#include "tusb.h"

#include <stdbool.h>
#include <stdint.h>


/*
 * Lokální prototypy.
 *
 * Device init u tebe existuje, ale podle linkeru neexistuje:
 * - usb_device_tusb_task()
 * - usb_device_tusb_deinit()
 *
 * Proto je tady zatím nepoužíváme.
 */

void usb_device_tusb_init(void);

void usb_host_tusb_init(void);
void usb_host_tusb_task(void);
void usb_host_tusb_task_log(void);
void usb_host_tusb_deinit(void);
void usb_device_tusb_task(void);
void usb_device_tusb_deinit(void);

static usb_mode_t usb_manager_mode =
    USB_MODE_NONE;


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
     * Mode nastavíme před tud_init(), aby USB interrupt během initu
     * šel správně do device handleru.
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
         * Pro aktuální host bring-up device deinit nepoužíváme,
         * protože v projektu není symbol usb_device_tusb_deinit().
         */
        uart_write_str("[USB-HOST] leaving DEVICE state without device deinit\r\n");

        usb_manager_mode =
            USB_MODE_NONE;
    }

    uart_write_str("[USB-HOST] BEFORE usb_host_tusb_init\r\n");

    usb_host_tusb_init();

    uart_write_str("[USB-HOST] AFTER usb_host_tusb_init\r\n");

    /*
     * Důležité:
     * Mode nastavíme před tuh_init(), aby USB_DRD_FS_IRQHandler()
     * už během host initu směroval interrupt do tuh_int_handler(0).
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
         * Device deinit zatím nepoužíváme, protože v projektu není.
         */
        uart_write_str("[USB] DEVICE stop skipped - no device deinit\r\n");
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
    return (usb_manager_mode == USB_MODE_HOST);
}


bool usb_manager_is_device_active(void)
{
    return (usb_manager_mode == USB_MODE_DEVICE);
}


bool usb_manager_is_active(void)
{
    return (usb_manager_mode != USB_MODE_NONE);
}


usb_mode_t usb_manager_get_mode(void)
{
    return usb_manager_mode;
}