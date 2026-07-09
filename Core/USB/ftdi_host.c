#include "ftdi_host.h"

#include "main.h"
#include "uart.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>


/*
 * FTDI FT232R TinyUSB HOST APP CLASS DRIVER
 *
 * Stav:
 * - FT232R enumerace OK
 * - FTDI vendor init OK
 * - bulk OUT OK
 * - bulk IN OK
 * - RS232 DB9 loopback pin 2 <-> pin 3 OK
 */


#define FTDI_VID                         0x0403U
#define FTDI_FT232R_PID                  0x6001U

#define FTDI_REQ_RESET                   0x00U
#define FTDI_REQ_MODEM_CTRL              0x01U
#define FTDI_REQ_SET_FLOW_CTRL           0x02U
#define FTDI_REQ_SET_BAUDRATE            0x03U
#define FTDI_REQ_SET_DATA                0x04U
#define FTDI_REQ_SET_LATENCY_TIMER       0x09U

#define FTDI_RESET_SIO                   0x0000U
#define FTDI_MODEM_DTR_RTS_ENABLE        0x0303U
#define FTDI_DATA_8N1                    0x0008U
#define FTDI_BAUD_115200_VALUE           0x001AU

#define FTDI_RX_BUF_SIZE                 64U
#define FTDI_TX_BUF_SIZE                 128U

#define FTDI_HELLO_DELAY_MS              500U
#define FTDI_STATUS_ONLY_DELAY_MS        50U

/*
 * Test po enumeraci:
 *
 * 1 = po initu posle "Hello from STM32 FTDI host\r\n"
 * 0 = neposila automaticky nic
 */
#define FTDI_TEST_HELLO_ENABLE           1U

/*
 * Verbose RX polling log.
 *
 * 0 = normalni tichy provoz
 * 1 = loguje kazdy RX START / RX SUBMIT OK
 */
#define FTDI_VERBOSE_RX_POLL_LOG         0U


typedef enum
{
    FTDI_ENUM_STEP_IDLE = 0,
    FTDI_ENUM_STEP_RESET,
    FTDI_ENUM_STEP_BAUD,
    FTDI_ENUM_STEP_DATA,
    FTDI_ENUM_STEP_FLOW,
    FTDI_ENUM_STEP_LATENCY,
    FTDI_ENUM_STEP_MODEM,
    FTDI_ENUM_STEP_DONE,
    FTDI_ENUM_STEP_ERROR
} ftdi_enum_step_t;


typedef struct
{
    uint8_t active;
    uint8_t mounted;

    uint8_t daddr;
    uint8_t rhport;

    uint8_t interface_number;
    uint16_t control_index;

    uint8_t ep_in;
    uint8_t ep_out;

    uint16_t ep_in_mps;
    uint16_t ep_out_mps;

    uint8_t rx_active;
    uint8_t tx_active;

    uint8_t hello_sent;
    uint32_t hello_due_ms;

    uint32_t next_rx_due_ms;
    uint32_t rx_status_only_count;

    ftdi_enum_step_t enum_step;
} ftdi_host_dev_t;


static ftdi_host_dev_t ftdi_dev;

static ftdi_host_rx_callback_t ftdi_rx_callback = NULL;

static tusb_control_request_t ftdi_ctrl_request;
static tuh_xfer_t ftdi_ctrl_xfer;

CFG_TUH_MEM_SECTION
CFG_TUSB_MEM_ALIGN
static uint8_t ftdi_rx_buf[FTDI_RX_BUF_SIZE];

CFG_TUH_MEM_SECTION
CFG_TUSB_MEM_ALIGN
static uint8_t ftdi_tx_buf[FTDI_TX_BUF_SIZE];


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
        buf[i++] =
            (char)('0' + (value % 10U));

        value /=
            10U;
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


static uint16_t le16_read(const uint8_t *p)
{
    return
        (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}


static void ftdi_host_reset_state(void)
{
    memset(&ftdi_dev, 0, sizeof(ftdi_dev));
    memset(&ftdi_ctrl_request, 0, sizeof(ftdi_ctrl_request));
    memset(&ftdi_ctrl_xfer, 0, sizeof(ftdi_ctrl_xfer));
    memset(ftdi_rx_buf, 0, sizeof(ftdi_rx_buf));
    memset(ftdi_tx_buf, 0, sizeof(ftdi_tx_buf));

    ftdi_dev.enum_step =
        FTDI_ENUM_STEP_IDLE;
}


void ftdi_host_app_init(void)
{
    ftdi_host_reset_state();

    /*
     * Callback neresetujeme, aby aplikace nemusela znovu registrovat callback
     * po deinit/init cyklu host stacku.
     */

    uart_write_str("[FTDI-HOST] app init\r\n");
}


bool ftdi_host_is_mounted(void)
{
    return
        (ftdi_dev.mounted != 0U);
}


bool ftdi_host_is_ready(void)
{
    return
        ((ftdi_dev.mounted != 0U) &&
         (ftdi_dev.enum_step == FTDI_ENUM_STEP_DONE));
}


void ftdi_host_set_rx_callback(
    ftdi_host_rx_callback_t callback)
{
    ftdi_rx_callback =
        callback;
}


static bool ftdi_submit_control(
    uint8_t request,
    uint16_t value,
    uint16_t index,
    const char *name,
    tuh_xfer_cb_t complete_cb)
{
    uart_write_str("[FTDI-HOST] CTRL START ");
    uart_write_str(name);
    uart_write_str(" req=");
    uart_write_hex8(request);
    uart_write_str(" value=");
    uart_write_hex((uint32_t)value);
    uart_write_str(" index=");
    uart_write_hex((uint32_t)index);
    uart_write_str("\r\n");

    memset(&ftdi_ctrl_request, 0, sizeof(ftdi_ctrl_request));
    memset(&ftdi_ctrl_xfer, 0, sizeof(ftdi_ctrl_xfer));

    ftdi_ctrl_request.bmRequestType =
        0x40U;

    ftdi_ctrl_request.bRequest =
        request;

    ftdi_ctrl_request.wValue =
        value;

    ftdi_ctrl_request.wIndex =
        index;

    ftdi_ctrl_request.wLength =
        0U;

    ftdi_ctrl_xfer.daddr =
        ftdi_dev.daddr;

    ftdi_ctrl_xfer.ep_addr =
        0U;

    ftdi_ctrl_xfer.setup =
        &ftdi_ctrl_request;

    ftdi_ctrl_xfer.buffer =
        NULL;

    ftdi_ctrl_xfer.complete_cb =
        complete_cb;

    ftdi_ctrl_xfer.user_data =
        0U;

    if(!tuh_control_xfer(&ftdi_ctrl_xfer))
    {
        uart_write_str("[FTDI-HOST] CTRL SUBMIT FAILED ");
        uart_write_str(name);
        uart_write_str("\r\n");

        return false;
    }

    return true;
}


static bool ftdi_start_next_enum_control(void);


static void ftdi_enum_control_complete_cb(tuh_xfer_t *xfer)
{
    uint8_t ok =
        0U;

    if(xfer != NULL)
    {
        ok =
            (xfer->result == XFER_RESULT_SUCCESS) ? 1U : 0U;
    }

    uart_write_str("[FTDI-HOST] CTRL DONE step=");
    uart_write_dec_u32((uint32_t)ftdi_dev.enum_step);
    uart_write_str(" ok=");
    uart_write_dec_u32((uint32_t)ok);
    uart_write_str("\r\n");

    if(ok == 0U)
    {
        ftdi_dev.enum_step =
            FTDI_ENUM_STEP_ERROR;

        usbh_driver_set_config_complete(
            ftdi_dev.daddr,
            ftdi_dev.interface_number);

        return;
    }

    switch(ftdi_dev.enum_step)
    {
        case FTDI_ENUM_STEP_RESET:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_BAUD;

            break;
        }

        case FTDI_ENUM_STEP_BAUD:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_DATA;

            break;
        }

        case FTDI_ENUM_STEP_DATA:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_FLOW;

            break;
        }

        case FTDI_ENUM_STEP_FLOW:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_LATENCY;

            break;
        }

        case FTDI_ENUM_STEP_LATENCY:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_MODEM;

            break;
        }

        case FTDI_ENUM_STEP_MODEM:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_DONE;

            ftdi_dev.mounted =
                1U;

            ftdi_dev.hello_sent =
                0U;

            ftdi_dev.hello_due_ms =
                HAL_GetTick() + FTDI_HELLO_DELAY_MS;

            ftdi_dev.next_rx_due_ms =
                HAL_GetTick();

            ftdi_dev.rx_status_only_count =
                0U;

            uart_write_str("[FTDI-HOST] INIT DONE - READY 115200 8N1\r\n");

            usbh_driver_set_config_complete(
                ftdi_dev.daddr,
                ftdi_dev.interface_number);

            return;
        }

        default:
        {
            ftdi_dev.enum_step =
                FTDI_ENUM_STEP_ERROR;

            usbh_driver_set_config_complete(
                ftdi_dev.daddr,
                ftdi_dev.interface_number);

            return;
        }
    }

    if(!ftdi_start_next_enum_control())
    {
        ftdi_dev.enum_step =
            FTDI_ENUM_STEP_ERROR;

        usbh_driver_set_config_complete(
            ftdi_dev.daddr,
            ftdi_dev.interface_number);
    }
}


static bool ftdi_start_next_enum_control(void)
{
    switch(ftdi_dev.enum_step)
    {
        case FTDI_ENUM_STEP_RESET:
        {
            return ftdi_submit_control(
                FTDI_REQ_RESET,
                FTDI_RESET_SIO,
                ftdi_dev.control_index,
                "RESET",
                ftdi_enum_control_complete_cb);
        }

        case FTDI_ENUM_STEP_BAUD:
        {
            return ftdi_submit_control(
                FTDI_REQ_SET_BAUDRATE,
                FTDI_BAUD_115200_VALUE,
                ftdi_dev.control_index,
                "BAUD115200",
                ftdi_enum_control_complete_cb);
        }

        case FTDI_ENUM_STEP_DATA:
        {
            return ftdi_submit_control(
                FTDI_REQ_SET_DATA,
                FTDI_DATA_8N1,
                ftdi_dev.control_index,
                "DATA8N1",
                ftdi_enum_control_complete_cb);
        }

        case FTDI_ENUM_STEP_FLOW:
        {
            return ftdi_submit_control(
                FTDI_REQ_SET_FLOW_CTRL,
                0U,
                ftdi_dev.control_index,
                "FLOW_NONE",
                ftdi_enum_control_complete_cb);
        }

        case FTDI_ENUM_STEP_LATENCY:
        {
            return ftdi_submit_control(
                FTDI_REQ_SET_LATENCY_TIMER,
                1U,
                ftdi_dev.control_index,
                "LATENCY1",
                ftdi_enum_control_complete_cb);
        }

        case FTDI_ENUM_STEP_MODEM:
        {
            return ftdi_submit_control(
                FTDI_REQ_MODEM_CTRL,
                FTDI_MODEM_DTR_RTS_ENABLE,
                ftdi_dev.control_index,
                "DTR_RTS",
                ftdi_enum_control_complete_cb);
        }

        default:
        {
            return false;
        }
    }
}


static bool ftdi_start_rx(void)
{
    if((ftdi_dev.mounted == 0U) || (ftdi_dev.rx_active != 0U))
    {
        return false;
    }

    memset(ftdi_rx_buf, 0, sizeof(ftdi_rx_buf));

    ftdi_dev.rx_active =
        1U;

#if FTDI_VERBOSE_RX_POLL_LOG
    uart_write_str("[FTDI-HOST] RX START ep=");
    uart_write_hex8(ftdi_dev.ep_in);
    uart_write_str(" len=");
    uart_write_dec_u32((uint32_t)sizeof(ftdi_rx_buf));
    uart_write_str("\r\n");
#endif

    if(!usbh_edpt_xfer(
            ftdi_dev.daddr,
            ftdi_dev.ep_in,
            ftdi_rx_buf,
            sizeof(ftdi_rx_buf)))
    {
        uart_write_str("[FTDI-HOST] RX SUBMIT FAILED\r\n");

        ftdi_dev.rx_active =
            0U;

        return false;
    }

#if FTDI_VERBOSE_RX_POLL_LOG
    uart_write_str("[FTDI-HOST] RX SUBMIT OK\r\n");
#endif

    return true;
}


bool ftdi_host_send(
    const uint8_t *data,
    uint32_t len)
{
    if(!ftdi_host_is_ready())
    {
        uart_write_str("[FTDI-HOST] SEND blocked - not ready\r\n");
        return false;
    }

    if(ftdi_dev.tx_active != 0U)
    {
        uart_write_str("[FTDI-HOST] SEND blocked - TX busy\r\n");
        return false;
    }

    if((data == NULL) || (len == 0U))
    {
        return false;
    }

    if(len > FTDI_TX_BUF_SIZE)
    {
        len =
            FTDI_TX_BUF_SIZE;
    }

    memcpy(ftdi_tx_buf, data, len);

    ftdi_dev.tx_active =
        1U;

    uart_write_str("[FTDI-HOST] TX START len=");
    uart_write_dec_u32(len);
    uart_write_str(" ep=");
    uart_write_hex8(ftdi_dev.ep_out);
    uart_write_str("\r\n");

    if(!usbh_edpt_xfer(
            ftdi_dev.daddr,
            ftdi_dev.ep_out,
            ftdi_tx_buf,
            (uint16_t)len))
    {
        uart_write_str("[FTDI-HOST] TX SUBMIT FAILED\r\n");

        ftdi_dev.tx_active =
            0U;

        return false;
    }

    uart_write_str("[FTDI-HOST] TX SUBMIT OK\r\n");

    return true;
}


static void ftdi_send_hello(void)
{
    static const uint8_t hello[] =
        "Hello from STM32 FTDI host\r\n";

    (void)ftdi_host_send(
        hello,
        (uint32_t)(sizeof(hello) - 1U));
}


void ftdi_host_task(void)
{
    uint32_t now;

    if(ftdi_dev.mounted == 0U)
    {
        return;
    }

    now =
        HAL_GetTick();

#if FTDI_TEST_HELLO_ENABLE
    if(ftdi_dev.hello_sent == 0U)
    {
        if((int32_t)(now - ftdi_dev.hello_due_ms) >= 0)
        {
            ftdi_dev.hello_sent =
                1U;

            uart_write_str("[FTDI-HOST] HELLO send now\r\n");

            ftdi_send_hello();
        }
    }
#else
    ftdi_dev.hello_sent =
        1U;
#endif

    if(ftdi_dev.rx_active == 0U)
    {
        if((int32_t)(now - ftdi_dev.next_rx_due_ms) >= 0)
        {
            (void)ftdi_start_rx();
        }
    }
}


/* =========================
   TinyUSB app class driver
========================= */

static bool ftdi_host_init(void)
{
    ftdi_host_reset_state();

    uart_write_str("[FTDI-HOST] class init\r\n");

    return true;
}


static bool ftdi_host_deinit(void)
{
    ftdi_host_reset_state();

    uart_write_str("[FTDI-HOST] class deinit\r\n");

    return true;
}


static uint16_t ftdi_host_open(
    uint8_t rhport,
    uint8_t dev_addr,
    const tusb_desc_interface_t *itf_desc,
    uint16_t max_len)
{
    uint16_t vid =
        0U;

    uint16_t pid =
        0U;

    const uint8_t *p;
    const uint8_t *end;
    uint8_t ep_in =
        0U;

    uint8_t ep_out =
        0U;

    uint16_t ep_in_mps =
        0U;

    uint16_t ep_out_mps =
        0U;

    uint16_t drv_len =
        0U;


    if((itf_desc == NULL) || (max_len < sizeof(tusb_desc_interface_t)))
    {
        return 0U;
    }

    if(!tuh_vid_pid_get(dev_addr, &vid, &pid))
    {
        return 0U;
    }

    if((vid != FTDI_VID) || (pid != FTDI_FT232R_PID))
    {
        return 0U;
    }

    if(itf_desc->bInterfaceClass != 0xFFU)
    {
        return 0U;
    }

    uart_write_str("[FTDI-HOST] OPEN daddr=");
    uart_write_dec_u32((uint32_t)dev_addr);
    uart_write_str(" VID=");
    uart_write_hex((uint32_t)vid);
    uart_write_str(" PID=");
    uart_write_hex((uint32_t)pid);
    uart_write_str(" if=");
    uart_write_dec_u32((uint32_t)itf_desc->bInterfaceNumber);
    uart_write_str("\r\n");


    p =
        (const uint8_t *)itf_desc;

    end =
        p + max_len;

    drv_len =
        itf_desc->bLength;

    p +=
        itf_desc->bLength;


    while((p + 2U) <= end)
    {
        uint8_t len =
            p[0];

        uint8_t type =
            p[1];

        if(len < 2U)
        {
            break;
        }

        if((p + len) > end)
        {
            break;
        }

        if(type == TUSB_DESC_INTERFACE)
        {
            break;
        }

        if(type == TUSB_DESC_ENDPOINT)
        {
            if(len >= 7U)
            {
                const tusb_desc_endpoint_t *ep =
                    (const tusb_desc_endpoint_t *)p;

                uint8_t ep_addr =
                    ep->bEndpointAddress;

                uint8_t ep_attr =
                    p[3];

                uint16_t ep_mps =
                    le16_read(&p[4]);

                uint8_t ep_type =
                    ep_attr & 0x03U;

                uart_write_str("[FTDI-HOST] EP addr=");
                uart_write_hex8(ep_addr);
                uart_write_str(" attr=");
                uart_write_hex8(ep_attr);
                uart_write_str(" mps=");
                uart_write_dec_u32((uint32_t)ep_mps);
                uart_write_str("\r\n");

                if(ep_type == TUSB_XFER_BULK)
                {
                    if(ep_addr & 0x80U)
                    {
                        ep_in =
                            ep_addr;

                        ep_in_mps =
                            ep_mps;
                    }
                    else
                    {
                        ep_out =
                            ep_addr;

                        ep_out_mps =
                            ep_mps;
                    }

                    if(!tuh_edpt_open(dev_addr, ep))
                    {
                        uart_write_str("[FTDI-HOST] EP OPEN FAILED ");
                        uart_write_hex8(ep_addr);
                        uart_write_str("\r\n");

                        return 0U;
                    }

                    uart_write_str("[FTDI-HOST] EP OPEN OK ");
                    uart_write_hex8(ep_addr);
                    uart_write_str("\r\n");
                }
            }
        }

        drv_len =
            (uint16_t)(drv_len + len);

        p +=
            len;
    }

    if((ep_in == 0U) || (ep_out == 0U))
    {
        uart_write_str("[FTDI-HOST] OPEN FAILED - endpoints missing\r\n");

        return 0U;
    }

    memset(&ftdi_dev, 0, sizeof(ftdi_dev));

    ftdi_dev.active =
        1U;

    ftdi_dev.daddr =
        dev_addr;

    ftdi_dev.rhport =
        rhport;

    ftdi_dev.interface_number =
        itf_desc->bInterfaceNumber;

    ftdi_dev.control_index =
        (uint16_t)(itf_desc->bInterfaceNumber + 1U);

    ftdi_dev.ep_in =
        ep_in;

    ftdi_dev.ep_out =
        ep_out;

    ftdi_dev.ep_in_mps =
        ep_in_mps;

    ftdi_dev.ep_out_mps =
        ep_out_mps;

    ftdi_dev.enum_step =
        FTDI_ENUM_STEP_IDLE;


    uart_write_str("[FTDI-HOST] OPEN OK if=");
    uart_write_dec_u32((uint32_t)ftdi_dev.interface_number);
    uart_write_str(" ep_in=");
    uart_write_hex8(ftdi_dev.ep_in);
    uart_write_str(" ep_out=");
    uart_write_hex8(ftdi_dev.ep_out);
    uart_write_str(" drv_len=");
    uart_write_dec_u32((uint32_t)drv_len);
    uart_write_str("\r\n");

    return drv_len;
}


static bool ftdi_host_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    if((ftdi_dev.active == 0U) || (ftdi_dev.daddr != dev_addr))
    {
        return false;
    }

    uart_write_str("[FTDI-HOST] SET_CONFIG daddr=");
    uart_write_dec_u32((uint32_t)dev_addr);
    uart_write_str(" itf=");
    uart_write_dec_u32((uint32_t)itf_num);
    uart_write_str("\r\n");

    ftdi_dev.enum_step =
        FTDI_ENUM_STEP_RESET;

    if(!ftdi_start_next_enum_control())
    {
        ftdi_dev.enum_step =
            FTDI_ENUM_STEP_ERROR;

        usbh_driver_set_config_complete(
            dev_addr,
            itf_num);

        return false;
    }

    return true;
}


static bool ftdi_host_xfer_cb(
    uint8_t dev_addr,
    uint8_t ep_addr,
    xfer_result_t result,
    uint32_t xferred_bytes)
{
    if((ftdi_dev.active == 0U) || (ftdi_dev.daddr != dev_addr))
    {
        return false;
    }

    if(ep_addr == ftdi_dev.ep_in)
    {
        ftdi_dev.rx_active =
            0U;

        if(result == XFER_RESULT_SUCCESS)
        {
            if(xferred_bytes > 2U)
            {
                uint8_t *payload =
                    &ftdi_rx_buf[2];

                uint32_t payload_len =
                    xferred_bytes - 2U;

                ftdi_dev.next_rx_due_ms =
                    HAL_GetTick();

                uart_write_str("[FTDI-HOST] RX DONE result=");
                uart_write_dec_u32((uint32_t)result);
                uart_write_str(" len=");
                uart_write_dec_u32(xferred_bytes);
                uart_write_str("\r\n");

                uart_write_str("[FTDI-HOST] RX STATUS=");
                uart_write_hex8(ftdi_rx_buf[0]);
                uart_write_str(" ");
                uart_write_hex8(ftdi_rx_buf[1]);
                uart_write_str("\r\n");

                uart_write_str("[FTDI-HOST] RX DATA \"");
                uart_write_ascii_fixed(payload, payload_len);
                uart_write_str("\"\r\n");

                if(ftdi_rx_callback != NULL)
                {
                    ftdi_rx_callback(
                        payload,
                        payload_len);
                }
            }
            else
            {
                ftdi_dev.rx_status_only_count++;

                if(
                    (ftdi_dev.rx_status_only_count <= 3U) ||
                    ((ftdi_dev.rx_status_only_count % 50U) == 0U)
                )
                {
                    uart_write_str("[FTDI-HOST] RX status-only count=");
                    uart_write_dec_u32(ftdi_dev.rx_status_only_count);
                    uart_write_str("\r\n");
                }

                ftdi_dev.next_rx_due_ms =
                    HAL_GetTick() + FTDI_STATUS_ONLY_DELAY_MS;
            }
        }
        else
        {
            uart_write_str("[FTDI-HOST] RX ERROR result=");
            uart_write_dec_u32((uint32_t)result);
            uart_write_str(" len=");
            uart_write_dec_u32(xferred_bytes);
            uart_write_str("\r\n");

            ftdi_dev.next_rx_due_ms =
                HAL_GetTick() + FTDI_STATUS_ONLY_DELAY_MS;
        }

        return true;
    }

    if(ep_addr == ftdi_dev.ep_out)
    {
        ftdi_dev.tx_active =
            0U;

        uart_write_str("[FTDI-HOST] TX DONE result=");
        uart_write_dec_u32((uint32_t)result);
        uart_write_str(" len=");
        uart_write_dec_u32(xferred_bytes);
        uart_write_str("\r\n");

        return true;
    }

    uart_write_str("[FTDI-HOST] XFER unknown ep=");
    uart_write_hex8(ep_addr);
    uart_write_str("\r\n");

    return false;
}


void ftdi_host_on_umount(uint8_t daddr)
{
    /*
     * Hotplug safety:
     *
     * TinyUSB by mel zavolat class .close(), ale pri vlastnim app class driveru
     * nechceme spolehat jen na to.
     *
     * Pri fyzickem UMOUNT explicitne zahodime FTDI runtime state, pokud:
     * - FTDI bylo aktivni/mounted
     * - nebo adresa odpovida poslednimu FTDI daddr
     *
     * Callback ftdi_rx_callback zustava zachovany, proto volame jen reset_state().
     */
    if(
        (ftdi_dev.active != 0U) ||
        (ftdi_dev.mounted != 0U) ||
        (ftdi_dev.daddr == daddr)
    )
    {
        uart_write_str("[FTDI-HOST] UMOUNT reset state daddr=");
        uart_write_dec_u32((uint32_t)daddr);
        uart_write_str("\r\n");

        ftdi_host_reset_state();
    }
}

static void ftdi_host_close(uint8_t dev_addr)
{
    if((ftdi_dev.active != 0U) && (ftdi_dev.daddr == dev_addr))
    {
        uart_write_str("[FTDI-HOST] CLOSE daddr=");
        uart_write_dec_u32((uint32_t)dev_addr);
        uart_write_str("\r\n");

        ftdi_host_reset_state();
    }
}


static usbh_class_driver_t const ftdi_app_driver[] =
{
    {
        .name       = "FTDI-APP",
        .init       = ftdi_host_init,
        .deinit     = ftdi_host_deinit,
        .open       = ftdi_host_open,
        .set_config = ftdi_host_set_config,
        .xfer_cb    = ftdi_host_xfer_cb,
        .close      = ftdi_host_close
    }
};


usbh_class_driver_t const *usbh_app_driver_get_cb(uint8_t *driver_count)
{
    if(driver_count != NULL)
    {
        *driver_count =
            (uint8_t)(sizeof(ftdi_app_driver) / sizeof(ftdi_app_driver[0]));
    }

    return ftdi_app_driver;
}