#include "usb_device_tusb.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "uart.h"
#include "app_cli_transport.h"


#define USB_VID 0xCafe
#define USB_PID 0x4000
#define USB_BCD 0x0100


#define CDC_TX_QUEUE_SIZE 512U
#define CDC_TX_CHUNK_SIZE 64U


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

static volatile uint8_t device_mounted = 0U;
static volatile uint8_t cdc_connected = 0U;


/* =========================
   CDC TX QUEUE
========================= */

static volatile uint16_t cdc_tx_head = 0U;
static volatile uint16_t cdc_tx_tail = 0U;
static uint8_t cdc_tx_queue[CDC_TX_QUEUE_SIZE];


static uint16_t cdc_tx_next(uint16_t index)
{
    index++;

    if(index >= CDC_TX_QUEUE_SIZE)
    {
        index =
            0U;
    }

    return index;
}


static void cdc_tx_queue_reset(void)
{
    cdc_tx_head =
        0U;

    cdc_tx_tail =
        0U;

    memset(cdc_tx_queue, 0, sizeof(cdc_tx_queue));
}


static bool cdc_tx_queue_push(uint8_t value)
{
    uint16_t next =
        cdc_tx_next(cdc_tx_head);

    if(next == cdc_tx_tail)
    {
        return false;
    }

    cdc_tx_queue[cdc_tx_head] =
        value;

    cdc_tx_head =
        next;

    return true;
}


static bool cdc_tx_queue_pop(uint8_t *value)
{
    if(value == NULL)
    {
        return false;
    }

    if(cdc_tx_tail == cdc_tx_head)
    {
        return false;
    }

    *value =
        cdc_tx_queue[cdc_tx_tail];

    cdc_tx_tail =
        cdc_tx_next(cdc_tx_tail);

    return true;
}


static bool cdc_tx_queue_is_empty(void)
{
    return
        (cdc_tx_tail == cdc_tx_head);
}


static void cdc_tx_task(void)
{
    uint8_t chunk[CDC_TX_CHUNK_SIZE];
    uint32_t count =
        0U;

    if((device_mounted == 0U) || (cdc_connected == 0U))
    {
        return;
    }

    if(!tud_cdc_connected())
    {
        return;
    }

    while((count < sizeof(chunk)) && !cdc_tx_queue_is_empty())
    {
        uint8_t b;

        if(!cdc_tx_queue_pop(&b))
        {
            break;
        }

        chunk[count] =
            b;

        count++;
    }

    if(count == 0U)
    {
        return;
    }

    /*
     * TinyUSB TX is now called only from usb_device_tusb_task(),
     * never directly from CDC callbacks.
     */
    uint32_t written =
        tud_cdc_write(chunk, count);

    (void)written;

    tud_cdc_write_flush();
}


static void cdc_write_text(char const *text)
{
    (void)usb_device_tusb_cdc_send_str(text);
}


/* =========================
   DESCRIPTORS
========================= */

uint8_t const *tud_descriptor_device_cb(void)
{
    return
        (uint8_t const *) &desc_device;
}


uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;

    return
        desc_configuration;
}


uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    uint8_t chr_count;

    (void)langid;

    if(index == 0)
    {
        desc_string[1] =
            0x0409;

        chr_count =
            1U;
    }
    else
    {
        const char *str;

        if(index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0])))
        {
            return NULL;
        }

        str =
            string_desc_arr[index];

        chr_count =
            (uint8_t)strlen(str);

        if(chr_count > 31U)
        {
            chr_count =
                31U;
        }

        for(uint8_t i = 0U; i < chr_count; ++i)
        {
            desc_string[1U + i] =
                str[i];
        }
    }

    desc_string[0] =
        (uint16_t)((TUSB_DESC_STRING << 8) | (2U * chr_count + 2U));

    return
        desc_string;
}


/* =========================
   DEVICE CALLBACKS CDC
========================= */

void tud_mount_cb(void)
{
    device_mounted =
        1U;

    cdc_connected =
        0U;

    cdc_tx_queue_reset();

    uart_write_str("[USB] ATTACH\r\n");
}


void tud_umount_cb(void)
{
    device_mounted =
        0U;

    cdc_connected =
        0U;

    cdc_tx_queue_reset();

    uart_write_str("[USB] DETACH\r\n");
}


void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
}


void tud_resume_cb(void)
{
}


void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)rts;

    if(dtr)
    {
        cdc_connected =
            1U;

        cdc_tx_queue_reset();

        uart_write_str("[USB-CDC] CONNECTED DTR=1\r\n");

        /*
         * Safe now:
         * usb_device_tusb_cdc_send_str() only queues bytes.
         * Actual tud_cdc_write/flush happens in usb_device_tusb_task().
         */
        cdc_write_text("CDC CLI ready. Type help\r\n");
    }
    else
    {
        cdc_connected =
            0U;

        cdc_tx_queue_reset();

        uart_write_str("[USB-CDC] DISCONNECTED DTR=0\r\n");
    }
}


/* =========================
   CDC RX HANDLER
========================= */

void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;

    uint8_t buf[64];

    while(tud_cdc_available() != 0U)
    {
        uint32_t count =
            tud_cdc_read(buf, sizeof(buf));

        if(count == 0U)
        {
            break;
        }

        /*
         * Must only queue RX. No parser output directly here.
         */
        app_cli_transport_cdc_rx(
            buf,
            count);
    }
}


/* =========================
   APP WRAPPERS
========================= */

void usb_device_tusb_init(void)
{
    device_mounted =
        0U;

    cdc_connected =
        0U;

    cdc_tx_queue_reset();

    uart_write_str("[USB-DEVICE] app init\r\n");
}


void usb_device_tusb_task(void)
{
    tud_task_ext(0, false);

    cdc_tx_task();
}


void usb_device_tusb_deinit(void)
{
    device_mounted =
        0U;

    cdc_connected =
        0U;

    cdc_tx_queue_reset();

    uart_write_str("[USB-DEVICE] app deinit skipped\r\n");
}


bool usb_device_tusb_is_mounted(void)
{
    return
        (device_mounted != 0U);
}


bool usb_device_tusb_is_cdc_connected(void)
{
    return
        (device_mounted != 0U) &&
        (cdc_connected != 0U);
}


bool usb_device_tusb_cdc_send(
    const uint8_t *data,
    uint32_t len)
{
    bool ok =
        true;

    if((data == NULL) || (len == 0U))
    {
        return false;
    }

    if((device_mounted == 0U) || (cdc_connected == 0U))
    {
        return false;
    }

    /*
     * Queue only. Do not call TinyUSB TX here.
     */
    for(uint32_t i = 0U; i < len; i++)
    {
        if(!cdc_tx_queue_push(data[i]))
        {
            ok =
                false;

            break;
        }
    }

    return ok;
}


bool usb_device_tusb_cdc_send_str(
    const char *text)
{
    if(text == NULL)
    {
        return false;
    }

    return
        usb_device_tusb_cdc_send(
            (const uint8_t *)text,
            (uint32_t)strlen(text));
}