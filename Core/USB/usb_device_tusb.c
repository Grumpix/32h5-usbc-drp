#include <string.h>

#include "tusb.h"
#include "uart.h"

#define USB_VID 0xCafe
#define USB_PID 0x4000
#define USB_BCD 0x0100

enum
{
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL
};

enum
{
    EPNUM_CDC_NOTIF = 0x81,
    EPNUM_CDC_OUT = 0x02,
    EPNUM_CDC_IN = 0x82
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const tusb_desc_device_t desc_device =
{
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_BCD,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

static const uint8_t desc_configuration[] =
{
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static const char *const string_desc_arr[] =
{
    (const char[]) { 0x09, 0x04 },
    "novak",
    "STM32H533 TinyUSB CDC",
    "0001",
    "TinyUSB CDC"
};

static uint16_t desc_string[32];

static void cdc_write_text(char const *text)
{
    if (!tud_cdc_connected())
    {
        return;
    }

    tud_cdc_write_str(text);
    tud_cdc_write_flush();
}

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *) &desc_device;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void) index;
    return desc_configuration;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    uint8_t chr_count;

    (void) langid;

    if (index == 0)
    {
        desc_string[1] = 0x0409;
        chr_count = 1;
    }
    else
    {
        const char *str;

        if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        {
            return NULL;
        }

        str = string_desc_arr[index];
        chr_count = (uint8_t) strlen(str);

        if (chr_count > 31)
        {
            chr_count = 31;
        }

        for (uint8_t i = 0; i < chr_count; ++i)
        {
            desc_string[1 + i] = str[i];
        }
    }

    desc_string[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return desc_string;
}

/* =========================
   DEVICE CALLBACKS (CDC)
========================= */

void tud_mount_cb(void)
{
    uart_write_str("[USB] ATTACH\r\n");
}

void tud_umount_cb(void)
{
    uart_write_str("[USB] DETACH\r\n");
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
}

void tud_resume_cb(void)
{
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void) itf;
    (void) rts;

    if (dtr)
    {
        cdc_write_text("CDC echo ready\r\n");
    }
}

/* =========================
   CDC RX HANDLER
========================= */

void tud_cdc_rx_cb(uint8_t itf)
{
    (void) itf;

    uint8_t buf[64];

    while (tud_cdc_available() != 0)
    {
        uint32_t count = tud_cdc_read(buf, sizeof(buf));

        if (count == 0)
        {
            break;
        }

        tud_cdc_write(buf, count);
    }

    tud_cdc_write_flush();
}