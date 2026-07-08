#include "usb_host_tusb.h"

#include "main.h"
#include "uart.h"
#include "tusb.h"
#include "ftdi_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


static volatile uint8_t host_device_attached = 0U;
static volatile uint8_t attach_flag = 0U;
static volatile uint8_t detach_flag = 0U;
static volatile uint8_t last_daddr = 0U;


/* =========================
   MSC DEBUG
========================= */

#if CFG_TUH_MSC

typedef enum
{
    MSC_DBG_IDLE = 0,
    MSC_DBG_WAIT_INQUIRY,
    MSC_DBG_WAIT_CAPACITY,
    MSC_DBG_WAIT_READ_LBA0,
    MSC_DBG_DONE,
    MSC_DBG_ERROR
} msc_dbg_state_t;


static volatile msc_dbg_state_t msc_state = MSC_DBG_IDLE;

static uint8_t msc_daddr = 0U;
static uint8_t msc_lun = 0U;

static scsi_inquiry_resp_t msc_inquiry_resp;
static scsi_read_capacity10_resp_t msc_capacity_resp;

CFG_TUSB_MEM_ALIGN
static uint8_t msc_lba0[512];

#endif


static void uart_write_dec_u32(uint32_t value)
{
    char buf[11];
    uint32_t i = 0U;

    if(value == 0U)
    {
        uart_write_str("0");
        return;
    }

    while((value > 0U) && (i < sizeof(buf)))
    {
        buf[i++] = (char)('0' + (value % 10U));
        value /= 10U;
    }

    while(i > 0U)
    {
        char c[2];

        i--;

        c[0] =
            buf[i];

        c[1] =
            '\0';

        uart_write_str(c);
    }
}


static void uart_write_hex8(uint8_t value)
{
    uart_write_hex((uint32_t)value);
}


static void uart_write_ascii_fixed(const uint8_t *data, uint32_t len)
{
    for(uint32_t i = 0U; i < len; i++)
    {
        char c[2];

        if((data[i] >= 32U) && (data[i] <= 126U))
        {
            c[0] =
                (char)data[i];
        }
        else if((data[i] == '\r') || (data[i] == '\n') || (data[i] == '\t'))
        {
            c[0] =
                (char)data[i];
        }
        else
        {
            c[0] =
                '.';
        }

        c[1] =
            '\0';

        uart_write_str(c);
    }
}


static uint32_t be32_to_cpu(uint32_t v)
{
    return
          ((v & 0x000000FFUL) << 24)
        | ((v & 0x0000FF00UL) << 8)
        | ((v & 0x00FF0000UL) >> 8)
        | ((v & 0xFF000000UL) >> 24);
}


static void print_vid_pid(uint16_t vid, uint16_t pid)
{
    uart_write_str(" VID=");
    uart_write_hex((uint32_t)vid);

    uart_write_str(" PID=");
    uart_write_hex((uint32_t)pid);

    if((vid == 0x0403U) && (pid == 0x6001U))
    {
        uart_write_str(" FT232R");
    }
}


/* =========================
   MSC DEBUG FLOW
========================= */

#if CFG_TUH_MSC

static void msc_debug_reset(void)
{
    msc_state =
        MSC_DBG_IDLE;

    msc_daddr =
        0U;

    msc_lun =
        0U;

    memset(&msc_inquiry_resp, 0, sizeof(msc_inquiry_resp));
    memset(&msc_capacity_resp, 0, sizeof(msc_capacity_resp));
    memset(msc_lba0, 0, sizeof(msc_lba0));
}


static void msc_print_inquiry(void)
{
    uart_write_str("[TUH-MSC] INQUIRY vendor=\"");
    uart_write_ascii_fixed(msc_inquiry_resp.vendor_id, 8U);

    uart_write_str("\" product=\"");
    uart_write_ascii_fixed(msc_inquiry_resp.product_id, 16U);

    uart_write_str("\" rev=\"");
    uart_write_ascii_fixed(msc_inquiry_resp.product_rev, 4U);

    uart_write_str("\"\r\n");
}


static void msc_print_capacity(void)
{
    uint32_t last_lba =
        be32_to_cpu(msc_capacity_resp.last_lba);

    uint32_t block_size =
        be32_to_cpu(msc_capacity_resp.block_size);

    uint32_t block_count =
        last_lba + 1U;

    uint32_t total_mb =
        0U;

    if(block_size != 0U)
    {
        total_mb =
            (uint32_t)(((uint64_t)block_count * (uint64_t)block_size) / (1024ULL * 1024ULL));
    }

    uart_write_str("[TUH-MSC] CAPACITY last_lba=");
    uart_write_dec_u32(last_lba);

    uart_write_str(" blocks=");
    uart_write_dec_u32(block_count);

    uart_write_str(" block_size=");
    uart_write_dec_u32(block_size);

    uart_write_str(" total_MB=");
    uart_write_dec_u32(total_mb);

    uart_write_str("\r\n");
}


static void msc_print_lba0_result(void)
{
    uart_write_str("[TUH-MSC] READ10 LBA0 OK\r\n");

    uart_write_str("[TUH-MSC] LBA0 first16=");

    for(uint32_t i = 0U; i < 16U; i++)
    {
        uart_write_hex8(msc_lba0[i]);

        if(i != 15U)
        {
            uart_write_str(" ");
        }
    }

    uart_write_str("\r\n");

    uart_write_str("[TUH-MSC] MBR signature=");

    uart_write_hex8(msc_lba0[510]);
    uart_write_str(" ");
    uart_write_hex8(msc_lba0[511]);

    if((msc_lba0[510] == 0x55U) && (msc_lba0[511] == 0xAAU))
    {
        uart_write_str(" OK");
    }
    else
    {
        uart_write_str(" NOT_FOUND");
    }

    uart_write_str("\r\n");
}


static bool msc_start_inquiry(void);
static bool msc_start_capacity(void);
static bool msc_start_read_lba0(void);


static bool msc_complete_cb(uint8_t daddr, tuh_msc_complete_data_t const *cb_data)
{
    bool ok =
        false;

    uart_write_str("[TUH-MSC] COMPLETE daddr=");
    uart_write_dec_u32((uint32_t)daddr);

    uart_write_str(" state=");
    uart_write_dec_u32((uint32_t)msc_state);

    if((cb_data != NULL) && (cb_data->csw != NULL))
    {
        uart_write_str(" status=");
        uart_write_dec_u32((uint32_t)cb_data->csw->status);

        ok =
            (cb_data->csw->status == 0U);
    }
    else
    {
        uart_write_str(" status=NULL");

        ok =
            false;
    }

    uart_write_str("\r\n");

    if(!ok)
    {
        uart_write_str("[TUH-MSC] ERROR transfer failed\r\n");

        msc_state =
            MSC_DBG_ERROR;

        return true;
    }

    switch(msc_state)
    {
        case MSC_DBG_WAIT_INQUIRY:
        {
            msc_print_inquiry();

            if(msc_start_capacity())
            {
                msc_state =
                    MSC_DBG_WAIT_CAPACITY;
            }
            else
            {
                uart_write_str("[TUH-MSC] ERROR capacity submit failed\r\n");

                msc_state =
                    MSC_DBG_ERROR;
            }

            break;
        }

        case MSC_DBG_WAIT_CAPACITY:
        {
            msc_print_capacity();

            if(msc_start_read_lba0())
            {
                msc_state =
                    MSC_DBG_WAIT_READ_LBA0;
            }
            else
            {
                uart_write_str("[TUH-MSC] ERROR read10 submit failed\r\n");

                msc_state =
                    MSC_DBG_ERROR;
            }

            break;
        }

        case MSC_DBG_WAIT_READ_LBA0:
        {
            msc_print_lba0_result();

            uart_write_str("[TUH-MSC] FLASH BASIC TEST DONE\r\n");

            msc_state =
                MSC_DBG_DONE;

            break;
        }

        default:
        {
            uart_write_str("[TUH-MSC] WARN complete in unexpected state=");
            uart_write_dec_u32((uint32_t)msc_state);
            uart_write_str("\r\n");

            break;
        }
    }

    return true;
}


static bool msc_start_inquiry(void)
{
    bool submitted;

    uart_write_str("[TUH-MSC] START INQUIRY\r\n");

    memset(&msc_inquiry_resp, 0, sizeof(msc_inquiry_resp));

    msc_state =
        MSC_DBG_WAIT_INQUIRY;

    submitted =
        tuh_msc_inquiry(
            msc_daddr,
            msc_lun,
            &msc_inquiry_resp,
            msc_complete_cb,
            0U);

    uart_write_str("[TUH-MSC] INQUIRY submit=");
    uart_write_dec_u32(submitted ? 1U : 0U);
    uart_write_str("\r\n");

    return submitted;
}


static bool msc_start_capacity(void)
{
    bool submitted;

    uart_write_str("[TUH-MSC] START READ CAPACITY\r\n");

    memset(&msc_capacity_resp, 0, sizeof(msc_capacity_resp));

    submitted =
        tuh_msc_read_capacity(
            msc_daddr,
            msc_lun,
            &msc_capacity_resp,
            msc_complete_cb,
            0U);

    uart_write_str("[TUH-MSC] CAPACITY submit=");
    uart_write_dec_u32(submitted ? 1U : 0U);
    uart_write_str("\r\n");

    return submitted;
}


static bool msc_start_read_lba0(void)
{
    bool submitted;

    uart_write_str("[TUH-MSC] START READ10 LBA0\r\n");

    memset(msc_lba0, 0, sizeof(msc_lba0));

    submitted =
        tuh_msc_read10(
            msc_daddr,
            msc_lun,
            msc_lba0,
            0U,
            1U,
            msc_complete_cb,
            0U);

    uart_write_str("[TUH-MSC] READ10 submit=");
    uart_write_dec_u32(submitted ? 1U : 0U);
    uart_write_str("\r\n");

    return submitted;
}


static void msc_debug_start_now(uint8_t daddr)
{
    msc_daddr =
        daddr;

    msc_lun =
        0U;

    memset(&msc_inquiry_resp, 0, sizeof(msc_inquiry_resp));
    memset(&msc_capacity_resp, 0, sizeof(msc_capacity_resp));
    memset(msc_lba0, 0, sizeof(msc_lba0));

    uart_write_str("[TUH-MSC] DEBUG START NOW daddr=");
    uart_write_dec_u32((uint32_t)daddr);
    uart_write_str(" lun=0\r\n");

    if(!msc_start_inquiry())
    {
        uart_write_str("[TUH-MSC] ERROR inquiry submit failed\r\n");

        msc_state =
            MSC_DBG_ERROR;
    }
}

#endif


/* =========================
   PUBLIC API
========================= */

void usb_host_tusb_init(void)
{
    host_device_attached =
        0U;

    attach_flag =
        0U;

    detach_flag =
        0U;

    last_daddr =
        0U;

    ftdi_host_app_init();

#if CFG_TUH_MSC
    msc_debug_reset();
#endif

    uart_write_str("[HOST] INIT\r\n");
}


void usb_host_tusb_task_log(void)
{
    if(attach_flag)
    {
        attach_flag =
            0U;

        uart_write_str("[USB] ATTACH daddr=");
        uart_write_dec_u32((uint32_t)last_daddr);
        uart_write_str("\r\n");
    }

    if(detach_flag)
    {
        detach_flag =
            0U;

        uart_write_str("[USB] DETACH daddr=");
        uart_write_dec_u32((uint32_t)last_daddr);
        uart_write_str("\r\n");
    }
}


void usb_host_tusb_task(void)
{
    tuh_task_ext(0, false);

    ftdi_host_task();
}


void usb_host_tusb_deinit(void)
{
    host_device_attached =
        0U;

    attach_flag =
        0U;

    detach_flag =
        0U;

    last_daddr =
        0U;

    ftdi_host_app_init();

#if CFG_TUH_MSC
    msc_debug_reset();
#endif

    uart_write_str("[HOST] DEINIT\r\n");
}


bool usb_host_tusb_is_device_attached(void)
{
    return
        (host_device_attached != 0U);
}


/* =========================
   TINYUSB COMMON HOST CALLBACKS
========================= */

void tuh_mount_cb(uint8_t daddr)
{
    host_device_attached =
        1U;

    attach_flag =
        1U;

    last_daddr =
        daddr;

    uart_write_str("[TUH] MOUNT daddr=");
    uart_write_dec_u32((uint32_t)daddr);

    if(ftdi_host_is_mounted())
    {
        uart_write_str(" FTDI-HOST");
    }

    uart_write_str("\r\n");
}


void tuh_umount_cb(uint8_t daddr)
{
    host_device_attached =
        0U;

    detach_flag =
        1U;

    last_daddr =
        daddr;

    uart_write_str("[TUH] UMOUNT daddr=");
    uart_write_dec_u32((uint32_t)daddr);
    uart_write_str("\r\n");

#if CFG_TUH_MSC
    if(msc_daddr == daddr)
    {
        msc_debug_reset();
    }
#endif
}


void tuh_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr)
{
    (void)rhport;
    (void)eventid;
    (void)in_isr;
}


void tuh_enum_descriptor_device_cb(uint8_t daddr, const tusb_desc_device_t *desc_device)
{
    uart_write_str("[TUH] DEVICE DESC daddr=");
    uart_write_dec_u32((uint32_t)daddr);

    if(desc_device != NULL)
    {
        print_vid_pid(desc_device->idVendor, desc_device->idProduct);

        uart_write_str(" class=");
        uart_write_hex8(desc_device->bDeviceClass);

        uart_write_str(" sub=");
        uart_write_hex8(desc_device->bDeviceSubClass);

        uart_write_str(" proto=");
        uart_write_hex8(desc_device->bDeviceProtocol);

        uart_write_str(" mps0=");
        uart_write_dec_u32((uint32_t)desc_device->bMaxPacketSize0);

        uart_write_str(" configs=");
        uart_write_dec_u32((uint32_t)desc_device->bNumConfigurations);
    }
    else
    {
        uart_write_str(" NULL");
    }

    uart_write_str("\r\n");
}


bool tuh_enum_descriptor_configuration_cb(
    uint8_t daddr,
    uint8_t cfg_index,
    const tusb_desc_configuration_t *desc_config)
{
    uart_write_str("[TUH] CONFIG DESC daddr=");
    uart_write_dec_u32((uint32_t)daddr);

    uart_write_str(" cfg=");
    uart_write_dec_u32((uint32_t)cfg_index);

    if(desc_config != NULL)
    {
        uart_write_str(" interfaces=");
        uart_write_dec_u32((uint32_t)desc_config->bNumInterfaces);

        uart_write_str(" total_len=");
        uart_write_dec_u32((uint32_t)desc_config->wTotalLength);

        uart_write_str(" attr=");
        uart_write_hex8(desc_config->bmAttributes);

        uart_write_str(" max_power=");
        uart_write_dec_u32((uint32_t)desc_config->bMaxPower);

        uart_write_str("\r\n");
    }
    else
    {
        uart_write_str(" NULL\r\n");
    }

    return true;
}


/* =========================
   MSC CALLBACKS
========================= */

#if CFG_TUH_MSC

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    uart_write_str("[TUH-MSC] MOUNT daddr=");
    uart_write_dec_u32((uint32_t)dev_addr);
    uart_write_str("\r\n");

    msc_debug_start_now(dev_addr);
}


void tuh_msc_umount_cb(uint8_t dev_addr)
{
    uart_write_str("[TUH-MSC] UMOUNT daddr=");
    uart_write_dec_u32((uint32_t)dev_addr);
    uart_write_str("\r\n");
}

#endif