#include "usb_host_tusb.h"

#include "main.h"
#include "tusb.h"

static volatile uint8_t host_device_attached = 0;

static void usb_host_trace_char(char ch)
{
    if (((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0U) ||
        ((ITM->TCR & ITM_TCR_ITMENA_Msk) == 0U) ||
        ((ITM->TER & 1UL) == 0U))
    {
        return;
    }

    ITM_SendChar((uint32_t) ch);
}

static void usb_host_trace_text(char const *text)
{
    while (*text != '\0')
    {
        usb_host_trace_char(*text++);
    }
}

static void usb_host_trace_hex16(uint16_t value)
{
    static char const hex[] = "0123456789ABCDEF";

    for (int shift = 12; shift >= 0; shift -= 4)
    {
        usb_host_trace_char(hex[(value >> shift) & 0x0F]);
    }
}

void usb_host_tusb_init(void)
{
    host_device_attached = 0;
    usb_host_trace_text("USB host init\r\n");
}

void usb_host_tusb_deinit(void)
{
    host_device_attached = 0;
    usb_host_trace_text("USB host deinit\r\n");
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
    usb_host_trace_text("USB host mounted addr=");
    usb_host_trace_char((char) ('0' + daddr));

    if (tuh_vid_pid_get(daddr, &vid, &pid))
    {
        usb_host_trace_text(" vid=0x");
        usb_host_trace_hex16(vid);
        usb_host_trace_text(" pid=0x");
        usb_host_trace_hex16(pid);
    }

    usb_host_trace_text("\r\n");
}

void tuh_umount_cb(uint8_t daddr)
{
    host_device_attached = 0;
    usb_host_trace_text("USB host unmounted addr=");
    usb_host_trace_char((char) ('0' + daddr));
    usb_host_trace_text("\r\n");
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