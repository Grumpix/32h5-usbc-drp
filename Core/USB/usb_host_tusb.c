#include "usb_host_tusb.h"

#include "main.h"
#include "tusb.h"
#include "class/msc/msc_host.h"
#include "uart_log.h"
#include "usb_manager.h"

static volatile uint8_t host_device_attached = 0;

void usb_host_tusb_log(char const *text)
{
    uart_log_write(text);
}

static void usb_host_trace_hex16(uint16_t value)
{
    static char const hex[] = "0123456789ABCDEF";
    char text[5];

    text[0] = hex[(value >> 12) & 0x0F];
    text[1] = hex[(value >> 8) & 0x0F];
    text[2] = hex[(value >> 4) & 0x0F];
    text[3] = hex[value & 0x0F];
    text[4] = '\0';
    uart_log_write(text);
}

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
    uint16_t vid = 0;
    uint16_t pid = 0;

    host_device_attached = 1;
    usb_manager_host_attached(daddr);
    usb_host_tusb_log("DEVICE ATTACHED addr=");
    uart_log_write_u32((uint32_t) daddr);

    if (tuh_vid_pid_get(daddr, &vid, &pid))
    {
        usb_host_tusb_log(" vid=0x");
        usb_host_trace_hex16(vid);
        usb_host_tusb_log(" pid=0x");
        usb_host_trace_hex16(pid);
    }

    usb_host_tusb_log("\r\n");
}

void tuh_umount_cb(uint8_t daddr)
{
    host_device_attached = 0;
    usb_manager_host_removed(daddr);
    usb_host_tusb_log("DEVICE REMOVED addr=");
    uart_log_write_u32((uint32_t) daddr);
    usb_host_tusb_log("\r\n");
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

void tuh_msc_mount_cb(uint8_t daddr)
{
    usb_host_tusb_log("MSC READY addr=");
    uart_log_write_u32((uint32_t) daddr);
    usb_host_tusb_log("\r\n");
}

void tuh_msc_umount_cb(uint8_t daddr)
{
    usb_host_tusb_log("MSC REMOVED addr=");
    uart_log_write_u32((uint32_t) daddr);
    usb_host_tusb_log("\r\n");
}