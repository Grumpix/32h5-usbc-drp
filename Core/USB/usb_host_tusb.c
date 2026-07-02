#include "usb_host_tusb.h"

#include "uart.h"
#include "tusb.h"

static volatile uint8_t host_device_attached = 0;

void usb_host_tusb_init(void)
{
    host_device_attached = 0;
}

void usb_host_tusb_deinit(void)
{
    host_device_attached = 0;
}

bool usb_host_tusb_is_device_attached(void)
{
    return (host_device_attached != 0U);
}

void tuh_mount_cb(uint8_t daddr)
{
    (void) daddr;
    host_device_attached = 1;
    uart_write_str("[USB] ATTACH\r\n");
}

void tuh_umount_cb(uint8_t daddr)
{
    (void) daddr;
    host_device_attached = 0;
    uart_write_str("[USB] DETACH\r\n");
}

void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void) rhport;
    (void) eventid;
    (void) in_isr;
}

void tuh_enum_descriptor_device_cb(uint8_t daddr, const tusb_desc_device_t *desc_device)
{
    (void) daddr;
    (void) desc_device;
}

bool tuh_enum_descriptor_configuration_cb(uint8_t daddr, uint8_t cfg_index,
                                          const tusb_desc_configuration_t *desc_config)
{
    (void) daddr;
    (void) cfg_index;
    (void) desc_config;
    return true;
}