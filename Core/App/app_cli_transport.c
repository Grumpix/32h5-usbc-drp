#include "app_cli_transport.h"

#include "main.h"

#include "app_cli.h"
#include "uart.h"

#include "usb_device_tusb.h"
#include "ftdi_host.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


#define APP_CLI_LINE_MAX              96U

#define APP_CLI_CDC_RX_QUEUE_SIZE     256U

#define APP_CLI_FTDI_TX_QUEUE_SIZE    512U
#define APP_CLI_FTDI_TX_CHUNK_SIZE    48U
#define APP_CLI_FTDI_TX_RETRY_MS      5U


typedef struct
{
    char line[APP_CLI_LINE_MAX];
    uint32_t pos;
} app_cli_line_state_t;


static app_cli_line_state_t cdc_line;
static app_cli_line_state_t ftdi_line;


/*
 * CDC RX queue:
 *
 * TinyUSB CDC RX callback must not execute CLI and write response directly.
 * Callback only pushes bytes here.
 * app_cli_transport_task() consumes them from main loop context.
 */
static volatile uint16_t cdc_rx_head = 0U;
static volatile uint16_t cdc_rx_tail = 0U;
static uint8_t cdc_rx_queue[APP_CLI_CDC_RX_QUEUE_SIZE];


/*
 * FTDI TX queue:
 *
 * FTDI bulk OUT accepts only one active transfer at a time.
 * Long CLI responses must be split and retried from main loop context.
 */
static volatile uint16_t ftdi_tx_head = 0U;
static volatile uint16_t ftdi_tx_tail = 0U;
static uint8_t ftdi_tx_queue[APP_CLI_FTDI_TX_QUEUE_SIZE];

static uint8_t ftdi_tx_pending_active = 0U;
static uint8_t ftdi_tx_pending_buf[APP_CLI_FTDI_TX_CHUNK_SIZE];
static uint32_t ftdi_tx_pending_len = 0U;
static uint32_t ftdi_tx_next_try_ms = 0U;


static void cli_line_reset(app_cli_line_state_t *state)
{
    if(state == NULL)
    {
        return;
    }

    memset(state->line, 0, sizeof(state->line));

    state->pos =
        0U;
}


static bool is_input_char(uint8_t c)
{
    return
        (c >= 32U) &&
        (c <= 126U);
}


/* =========================
   FTDI TX QUEUE
========================= */

static uint16_t ftdi_tx_queue_next(uint16_t index)
{
    index++;

    if(index >= APP_CLI_FTDI_TX_QUEUE_SIZE)
    {
        index =
            0U;
    }

    return index;
}


static bool ftdi_tx_queue_is_empty(void)
{
    return
        (ftdi_tx_tail == ftdi_tx_head);
}


static bool ftdi_tx_queue_push(uint8_t value)
{
    uint16_t next =
        ftdi_tx_queue_next(ftdi_tx_head);

    if(next == ftdi_tx_tail)
    {
        return false;
    }

    ftdi_tx_queue[ftdi_tx_head] =
        value;

    ftdi_tx_head =
        next;

    return true;
}


static bool ftdi_tx_queue_pop(uint8_t *value)
{
    if(value == NULL)
    {
        return false;
    }

    if(ftdi_tx_tail == ftdi_tx_head)
    {
        return false;
    }

    *value =
        ftdi_tx_queue[ftdi_tx_tail];

    ftdi_tx_tail =
        ftdi_tx_queue_next(ftdi_tx_tail);

    return true;
}


static void ftdi_tx_queue_reset(void)
{
    ftdi_tx_head =
        0U;

    ftdi_tx_tail =
        0U;

    memset(ftdi_tx_queue, 0, sizeof(ftdi_tx_queue));

    ftdi_tx_pending_active =
        0U;

    ftdi_tx_pending_len =
        0U;

    memset(ftdi_tx_pending_buf, 0, sizeof(ftdi_tx_pending_buf));

    ftdi_tx_next_try_ms =
        0U;
}


static bool ftdi_queue_output(const uint8_t *data, uint32_t len)
{
    bool ok =
        true;

    if((data == NULL) || (len == 0U))
    {
        return false;
    }

    for(uint32_t i = 0U; i < len; i++)
    {
        if(!ftdi_tx_queue_push(data[i]))
        {
            ok =
                false;

            break;
        }
    }

    return ok;
}


static void ftdi_prepare_pending_chunk(void)
{
    if(ftdi_tx_pending_active != 0U)
    {
        return;
    }

    ftdi_tx_pending_len =
        0U;

    while(
        (ftdi_tx_pending_len < sizeof(ftdi_tx_pending_buf)) &&
        (!ftdi_tx_queue_is_empty())
    )
    {
        uint8_t b;

        if(!ftdi_tx_queue_pop(&b))
        {
            break;
        }

        ftdi_tx_pending_buf[ftdi_tx_pending_len] =
            b;

        ftdi_tx_pending_len++;
    }

    if(ftdi_tx_pending_len > 0U)
    {
        ftdi_tx_pending_active =
            1U;
    }
}


static void ftdi_tx_task(void)
{
    uint32_t now =
        HAL_GetTick();

    if((int32_t)(now - ftdi_tx_next_try_ms) < 0)
    {
        return;
    }

    if(!ftdi_host_is_ready())
    {
        return;
    }

    ftdi_prepare_pending_chunk();

    if(ftdi_tx_pending_active == 0U)
    {
        return;
    }

    /*
     * ftdi_host_send() returns false while the previous OUT transfer is busy.
     * Keep the pending chunk intact and retry later.
     */
    if(ftdi_host_send(ftdi_tx_pending_buf, ftdi_tx_pending_len))
    {
        ftdi_tx_pending_active =
            0U;

        ftdi_tx_pending_len =
            0U;

        memset(ftdi_tx_pending_buf, 0, sizeof(ftdi_tx_pending_buf));

        ftdi_tx_next_try_ms =
            now + APP_CLI_FTDI_TX_RETRY_MS;
    }
    else
    {
        ftdi_tx_next_try_ms =
            now + APP_CLI_FTDI_TX_RETRY_MS;
    }
}


/* =========================
   CDC RX QUEUE
========================= */

static uint16_t cdc_rx_queue_next(uint16_t index)
{
    index++;

    if(index >= APP_CLI_CDC_RX_QUEUE_SIZE)
    {
        index =
            0U;
    }

    return index;
}


static bool cdc_rx_queue_push(uint8_t value)
{
    uint16_t next =
        cdc_rx_queue_next(cdc_rx_head);

    if(next == cdc_rx_tail)
    {
        /*
         * Queue full.
         * Drop byte rather than blocking inside USB callback.
         */
        return false;
    }

    cdc_rx_queue[cdc_rx_head] =
        value;

    cdc_rx_head =
        next;

    return true;
}


static bool cdc_rx_queue_pop(uint8_t *value)
{
    if(value == NULL)
    {
        return false;
    }

    if(cdc_rx_tail == cdc_rx_head)
    {
        return false;
    }

    *value =
        cdc_rx_queue[cdc_rx_tail];

    cdc_rx_tail =
        cdc_rx_queue_next(cdc_rx_tail);

    return true;
}


static void cdc_rx_queue_reset(void)
{
    cdc_rx_head =
        0U;

    cdc_rx_tail =
        0U;

    memset(cdc_rx_queue, 0, sizeof(cdc_rx_queue));
}


static void cdc_process_queued_rx(void)
{
    uint8_t chunk[32];
    uint32_t count =
        0U;

    while(count < sizeof(chunk))
    {
        uint8_t b;

        if(!cdc_rx_queue_pop(&b))
        {
            break;
        }

        chunk[count] =
            b;

        count++;
    }

    if(count > 0U)
    {
        /*
         * Main-loop context.
         * Safe place to parse CLI and queue CDC response.
         */
        extern void app_cli_transport_cdc_rx_process(
            const uint8_t *data,
            uint32_t len);

        app_cli_transport_cdc_rx_process(
            chunk,
            count);
    }
}


/* =========================
   CLI CORE
========================= */

static void send_output(
    app_cli_port_t port,
    const char *out)
{
    uint32_t len;

    if(out == NULL)
    {
        return;
    }

    len =
        (uint32_t)strlen(out);

    if(len == 0U)
    {
        return;
    }

    if(port == APP_CLI_PORT_FTDI)
    {
        (void)ftdi_queue_output(
            (const uint8_t *)out,
            len);
    }
    else
    {
        /*
         * CDC send only queues bytes in usb_device_tusb.c.
         * Actual TinyUSB write/flush happens in usb_device_tusb_task().
         */
        (void)usb_device_tusb_cdc_send(
            (const uint8_t *)out,
            len);
    }
}


static void execute_line(
    app_cli_port_t port,
    app_cli_line_state_t *state)
{
    char out[APP_CLI_OUTPUT_MAX];

    if(state == NULL)
    {
        return;
    }

    state->line[state->pos] =
        '\0';

    app_cli_execute_line(
        port,
        state->line,
        out,
        sizeof(out));

    send_output(
        port,
        out);

    cli_line_reset(state);
}


static void cli_rx_bytes(
    app_cli_port_t port,
    app_cli_line_state_t *state,
    const uint8_t *data,
    uint32_t len)
{
    if((state == NULL) || (data == NULL))
    {
        return;
    }

    for(uint32_t i = 0U; i < len; i++)
    {
        uint8_t c =
            data[i];

        if((c == '\r') || (c == '\n'))
        {
            if(state->pos > 0U)
            {
                execute_line(
                    port,
                    state);
            }
            else
            {
                cli_line_reset(state);
            }
        }
        else if((c == 0x08U) || (c == 0x7FU))
        {
            if(state->pos > 0U)
            {
                state->pos--;
                state->line[state->pos] =
                    '\0';
            }
        }
        else if(is_input_char(c))
        {
            if((state->pos + 1U) < APP_CLI_LINE_MAX)
            {
                state->line[state->pos] =
                    (char)c;

                state->pos++;
            }
            else
            {
                cli_line_reset(state);

                send_output(
                    port,
                    "ERR line too long\r\n");
            }
        }
        else
        {
            /*
             * Ignore non-printable chars.
             */
        }
    }
}


/*
 * This function is intentionally separate from app_cli_transport_cdc_rx().
 *
 * app_cli_transport_cdc_rx() is called from TinyUSB callback and may only queue.
 * This one is called from app_cli_transport_task() and may execute CLI.
 */
void app_cli_transport_cdc_rx_process(
    const uint8_t *data,
    uint32_t len)
{
    cli_rx_bytes(
        APP_CLI_PORT_CDC,
        &cdc_line,
        data,
        len);
}


/* =========================
   FTDI RX
========================= */

static void ftdi_rx_cb(
    const uint8_t *data,
    uint32_t len)
{
    /*
     * FTDI RX callback is in host transfer callback context.
     *
     * It is OK to parse here because send_output(FTDI) only queues bytes.
     * Actual ftdi_host_send() happens later in app_cli_transport_task().
     */
    cli_rx_bytes(
        APP_CLI_PORT_FTDI,
        &ftdi_line,
        data,
        len);
}


/* =========================
   Public API
========================= */

void app_cli_transport_init(void)
{
    cli_line_reset(&cdc_line);
    cli_line_reset(&ftdi_line);

    cdc_rx_queue_reset();
    ftdi_tx_queue_reset();

    ftdi_host_set_rx_callback(ftdi_rx_cb);

    uart_write_str("[APP-CLI] transport init CDC+FTDI\r\n");
}


void app_cli_transport_task(void)
{
    cdc_process_queued_rx();

    ftdi_tx_task();
}


void app_cli_transport_cdc_rx(
    const uint8_t *data,
    uint32_t len)
{
    if(data == NULL)
    {
        return;
    }

    /*
     * Called from TinyUSB CDC RX callback.
     * Do NOT parse CLI here.
     * Do NOT call tud_cdc_write/tud_cdc_write_flush here.
     */
    for(uint32_t i = 0U; i < len; i++)
    {
        (void)cdc_rx_queue_push(data[i]);
    }
}