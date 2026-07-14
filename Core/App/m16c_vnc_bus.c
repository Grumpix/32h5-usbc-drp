#include "m16c_vnc_bus.h"

#include "main.h"
#include "uart.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/*
 * M16C / VNC1L compatibility bus
 *
 * Aktualni verze:
 * - STM32 -> M16C:
 *     - paced byte read emulation pro bezne testy
 *     - burst sentence mode pro kratke VNC eventy: SDA\r, SDD\r, DD2\r
 *     - automatic VDPS startup banner po zapnuti / po IORESET# rising
 *
 * - M16C -> STM32:
 *     - WR capture je bezpecne armovany jen docasne
 *
 * PA0..PA7 = D0..D7
 * PB1      = USB1_RD#
 * PB2      = USB1_WR
 * PB3      = DATAREQ##
 * PB4      = IORESET#
 * PB5      = TXE# output, active-low
 * PB6      = RXF# output, active-low
 *
 * Dulezite:
 * - PA0..PA7 jsou defaultne Hi-Z.
 * - Pri paced rezimu:
 *     jeden byte -> RXF# LOW -> RD# read -> RXF# HIGH -> delay.
 *
 * - Pri burst rezimu:
 *     RXF# zustava LOW pres celou vetu.
 *     Po RD# rising okamzite vystavime dalsi byte.
 *     Po poslednim bytu RXF# HIGH a bus Hi-Z.
 *
 * CIL:
 * - po startu emulovat VDPS boot report vcetne promptu:
 *     03.68-A03VDPSF On-Line:\r>
 * - nasledne m16c pc doruci "SDA\r" jako jednu souvislou VNC vetu,
 *   aby M16C preslo do PC Control.
 */

#define M16C_BUS_GPIO_Port              GPIOA
#define M16C_BUS_PIN_MASK               (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | \
                                         GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7)

#define M16C_RD_GPIO_Port               GPIOB
#define M16C_RD_Pin                     GPIO_PIN_1

#define M16C_WR_GPIO_Port               GPIOB
#define M16C_WR_Pin                     GPIO_PIN_2

#define M16C_DATAREQ_GPIO_Port          GPIOB
#define M16C_DATAREQ_Pin                GPIO_PIN_3

#define M16C_IORESET_GPIO_Port          GPIOB
#define M16C_IORESET_Pin                GPIO_PIN_4

#define M16C_TXE_GPIO_Port              GPIOB
#define M16C_TXE_Pin                    GPIO_PIN_5

#define M16C_RXF_GPIO_Port              GPIOB
#define M16C_RXF_Pin                    GPIO_PIN_6

#define M16C_TX_FIFO_SIZE               256U
#define M16C_RX_CAPTURE_SIZE            64U
#define M16C_TX_TRACE_SIZE              96U
#define M16C_RD_TRACE_SIZE              96U
#define M16C_PACE_DELAY_MS              4U

/*
 * Auto VDPS boot banner.
 */
#define M16C_AUTO_BOOT_INIT_DELAY_MS    500U
#define M16C_AUTO_BOOT_RESET_DELAY_MS   150U

/*
 * Podle ROM + nastaveni:
 * Show Version Report at Startup + Show Prompt.
 *
 * Drive jsme posilali jen:
 *   "03.68-A03VDPSF On-Line:\r"
 *
 * Nyni pridavame prompt:
 *   "03.68-A03VDPSF On-Line:\r>"
 */
#define M16C_VDPS_BOOT_BANNER           "SDA\r"

#define M16C_WR_CAPTURE_TIMEOUT_MS      1500U
#define M16C_WR_CAPTURE_MAX_EDGES       256U
#define M16C_WR_CAPTURE_MAX_BYTES       64U

/*
 * Podle mereni je WR v klidu LOW.
 * Proto zachytavame data na rising hrane.
 */
#define M16C_CAPTURE_WR_ON_HIGH         1U

typedef enum
{
    M16C_TX_MODE_PACED = 0,
    M16C_TX_MODE_BURST = 1
} m16c_tx_mode_t;

typedef struct
{
    uint8_t prev_rd;
    uint8_t prev_wr;
    uint8_t prev_datareq;
    uint8_t prev_ioreset;
    uint8_t prev_txe;
    uint8_t prev_rxf;

    uint8_t ready_mode;

    uint8_t bus_driving;
    uint8_t byte_presented;
    uint8_t presented_byte;

    uint8_t pacing_active;
    uint32_t pace_until_ms;

    m16c_tx_mode_t tx_mode;

    uint8_t tx_fifo[M16C_TX_FIFO_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

    uint8_t wr_capture_active;
    uint32_t wr_capture_until_ms;
    uint32_t wr_capture_edge_limit;
    uint32_t wr_capture_byte_limit;

    uint8_t auto_boot_pending;
    uint32_t auto_boot_due_ms;
    uint32_t auto_boot_request_count;
    uint32_t auto_boot_sent_count;

    uint8_t rx_capture[M16C_RX_CAPTURE_SIZE];
    uint8_t rx_capture_count;
    uint32_t rx_capture_total_count;
    uint32_t rx_capture_overflow_count;
    
    uint8_t tx_trace[M16C_TX_TRACE_SIZE];
    uint8_t tx_trace_count;
    uint32_t tx_trace_total_count;
    uint32_t tx_trace_overflow_count;

    uint8_t rd_trace[M16C_RD_TRACE_SIZE];
    uint8_t rd_trace_count;
    uint32_t rd_trace_total_count;
    uint32_t rd_trace_overflow_count;
    
    m16c_vnc_bus_status_t status;

    uint32_t txe_falling_count;
    uint32_t txe_rising_count;
    uint32_t rxf_falling_count;
    uint32_t rxf_rising_count;

    uint32_t tx_fifo_overflow_count;
    uint32_t tx_bytes_presented_count;
    uint32_t tx_bytes_read_count;

    uint32_t rd_irq_count;
    uint32_t rd_poll_count;
    uint32_t extra_rd_count;

    uint32_t wr_irq_count;
    uint32_t wr_poll_count;

    uint32_t pace_release_count;
    uint32_t pace_hold_count;

    uint32_t burst_start_count;
    uint32_t burst_done_count;
} m16c_vnc_bus_runtime_t;

static m16c_vnc_bus_runtime_t bus_rt;

static uint8_t gpio_read_pin_u8(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static uint8_t bus_read_u8(void)
{
    return (uint8_t)(M16C_BUS_GPIO_Port->IDR & 0xFFU);
}

static void out_append(char *out, uint32_t out_size, uint32_t *pos, const char *text)
{
    if ((out == NULL) || (pos == NULL) || (text == NULL) || (out_size == 0U))
    {
        return;
    }

    while ((*text != '\0') && ((*pos + 1U) < out_size))
    {
        out[*pos] = *text;
        (*pos)++;
        text++;
    }

    out[*pos] = '\0';
}

static void out_append_u32_dec(char *out, uint32_t out_size, uint32_t *pos, uint32_t value)
{
    char tmp[11];
    uint32_t i = 0U;

    if (value == 0U)
    {
        out_append(out, out_size, pos, "0");
        return;
    }

    while ((value > 0U) && (i < sizeof(tmp)))
    {
        tmp[i] = (char)('0' + (value % 10U));
        i++;
        value /= 10U;
    }

    while (i > 0U)
    {
        char c[2];

        i--;
        c[0] = tmp[i];
        c[1] = '\0';

        out_append(out, out_size, pos, c);
    }
}

static void out_append_hex4(char *out, uint32_t out_size, uint32_t *pos, uint8_t value)
{
    char c[2];
    uint8_t nibble = value & 0x0FU;

    if (nibble < 10U)
    {
        c[0] = (char)('0' + nibble);
    }
    else
    {
        c[0] = (char)('A' + nibble - 10U);
    }

    c[1] = '\0';

    out_append(out, out_size, pos, c);
}

static void out_append_hex8_raw(char *out, uint32_t out_size, uint32_t *pos, uint8_t value)
{
    out_append_hex4(out, out_size, pos, (uint8_t)(value >> 4));
    out_append_hex4(out, out_size, pos, value);
}

static void out_append_hex8(char *out, uint32_t out_size, uint32_t *pos, uint8_t value)
{
    out_append(out, out_size, pos, "0x");
    out_append_hex8_raw(out, out_size, pos, value);
}

static uint16_t tx_fifo_next(uint16_t index)
{
    index++;

    if (index >= M16C_TX_FIFO_SIZE)
    {
        index = 0U;
    }

    return index;
}

static uint8_t tx_fifo_is_empty(void)
{
    return (bus_rt.tx_head == bus_rt.tx_tail) ? 1U : 0U;
}

static uint8_t tx_fifo_push(uint8_t data)
{
    uint16_t next = tx_fifo_next(bus_rt.tx_head);

    if (next == bus_rt.tx_tail)
    {
        bus_rt.tx_fifo_overflow_count++;
        return 0U;
    }

    bus_rt.tx_fifo[bus_rt.tx_head] = data;
    bus_rt.tx_head = next;

    return 1U;
}

static uint8_t tx_fifo_peek(uint8_t *data)
{
    if (data == NULL)
    {
        return 0U;
    }

    if (tx_fifo_is_empty())
    {
        return 0U;
    }

    *data = bus_rt.tx_fifo[bus_rt.tx_tail];

    return 1U;
}

static uint8_t tx_fifo_pop(uint8_t *data)
{
    if (data == NULL)
    {
        return 0U;
    }

    if (tx_fifo_is_empty())
    {
        return 0U;
    }

    *data = bus_rt.tx_fifo[bus_rt.tx_tail];
    bus_rt.tx_tail = tx_fifo_next(bus_rt.tx_tail);

    return 1U;
}

static uint32_t tx_fifo_count(void)
{
    if (bus_rt.tx_head >= bus_rt.tx_tail)
    {
        return (uint32_t)(bus_rt.tx_head - bus_rt.tx_tail);
    }

    return (uint32_t)(M16C_TX_FIFO_SIZE - bus_rt.tx_tail + bus_rt.tx_head);
}

static void tx_fifo_clear(void)
{
    bus_rt.tx_head = 0U;
    bus_rt.tx_tail = 0U;
}

static void rx_capture_clear(void)
{
    memset(bus_rt.rx_capture, 0, sizeof(bus_rt.rx_capture));

    bus_rt.rx_capture_count = 0U;
    bus_rt.rx_capture_total_count = 0U;
    bus_rt.rx_capture_overflow_count = 0U;
}

static void rx_capture_push(uint8_t data)
{
    bus_rt.rx_capture_total_count++;

    if (bus_rt.rx_capture_count < M16C_RX_CAPTURE_SIZE)
    {
        bus_rt.rx_capture[bus_rt.rx_capture_count] = data;
        bus_rt.rx_capture_count++;
    }
    else
    {
        bus_rt.rx_capture_overflow_count++;
    }
}
static void tx_trace_clear(void)
{
    memset(bus_rt.tx_trace, 0, sizeof(bus_rt.tx_trace));

    bus_rt.tx_trace_count = 0U;
    bus_rt.tx_trace_total_count = 0U;
    bus_rt.tx_trace_overflow_count = 0U;
}

static void tx_trace_push(uint8_t data)
{
    bus_rt.tx_trace_total_count++;

    if (bus_rt.tx_trace_count < M16C_TX_TRACE_SIZE)
    {
        bus_rt.tx_trace[bus_rt.tx_trace_count] = data;
        bus_rt.tx_trace_count++;
    }
    else
    {
        bus_rt.tx_trace_overflow_count++;
    }
}

static void rd_trace_clear(void)
{
    memset(bus_rt.rd_trace, 0, sizeof(bus_rt.rd_trace));

    bus_rt.rd_trace_count = 0U;
    bus_rt.rd_trace_total_count = 0U;
    bus_rt.rd_trace_overflow_count = 0U;
}

static void rd_trace_push(uint8_t data)
{
    bus_rt.rd_trace_total_count++;

    if (bus_rt.rd_trace_count < M16C_RD_TRACE_SIZE)
    {
        bus_rt.rd_trace[bus_rt.rd_trace_count] = data;
        bus_rt.rd_trace_count++;
    }
    else
    {
        bus_rt.rd_trace_overflow_count++;
    }
}
static void wr_exti_disable(void)
{
    HAL_NVIC_DisableIRQ(EXTI2_IRQn);
}

static void wr_exti_enable(void)
{
    __HAL_GPIO_EXTI_CLEAR_IT(M16C_WR_Pin);
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

static void bus_force_hi_z(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_BUS_PIN_MASK;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(M16C_BUS_GPIO_Port, &GPIO_InitStruct);

    bus_rt.bus_driving = 0U;
}

static void bus_write_latch(uint8_t data)
{
    uint32_t odr;

    odr = M16C_BUS_GPIO_Port->ODR;
    odr &= ~0xFFU;
    odr |= data;
    M16C_BUS_GPIO_Port->ODR = odr;
}

static void bus_drive_byte(uint8_t data)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /*
     * Pokud uz bus ridime, jen rychle zmenime ODR.
     * To je dulezite pro burst mezi RD# cykly.
     */
if (bus_rt.bus_driving)
{
    bus_write_latch(data);

    bus_rt.presented_byte = data;
    bus_rt.byte_presented = 1U;
    bus_rt.tx_bytes_presented_count++;
    tx_trace_push(data);

    return;
}

    /*
     * Nejdriv nastavime latch, potom prepneme PA0..PA7 na output.
     */
    bus_write_latch(data);

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_BUS_PIN_MASK;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(M16C_BUS_GPIO_Port, &GPIO_InitStruct);

    bus_rt.bus_driving = 1U;
    bus_rt.presented_byte = data;
    bus_rt.byte_presented = 1U;
    bus_rt.tx_bytes_presented_count++;
    tx_trace_push(data);;
}

static void status_set_txe(void)
{
    HAL_GPIO_WritePin(
        M16C_TXE_GPIO_Port,
        M16C_TXE_Pin,
        bus_rt.ready_mode ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void status_set_rxf_high(void)
{
    HAL_GPIO_WritePin(M16C_RXF_GPIO_Port, M16C_RXF_Pin, GPIO_PIN_SET);
}

static void status_set_rxf_low(void)
{
    HAL_GPIO_WritePin(M16C_RXF_GPIO_Port, M16C_RXF_Pin, GPIO_PIN_RESET);
}

static void wr_capture_stop_internal(void)
{
    bus_rt.wr_capture_active = 0U;
    bus_rt.ready_mode = 0U;

    status_set_txe();
    wr_exti_disable();
}

static uint8_t wr_capture_should_stop(void)
{
    uint32_t now = HAL_GetTick();

    if (!bus_rt.wr_capture_active)
    {
        return 0U;
    }

    if ((int32_t)(now - bus_rt.wr_capture_until_ms) >= 0)
    {
        return 1U;
    }

    if (bus_rt.wr_irq_count >= bus_rt.wr_capture_edge_limit)
    {
        return 1U;
    }

    if (bus_rt.rx_capture_count >= bus_rt.wr_capture_byte_limit)
    {
        return 1U;
    }

    return 0U;
}

static void enter_pace_hold(void)
{
    status_set_rxf_high();
    bus_force_hi_z();

    bus_rt.byte_presented = 0U;
    bus_rt.pacing_active = 1U;
    bus_rt.pace_until_ms = HAL_GetTick() + M16C_PACE_DELAY_MS;
    bus_rt.pace_hold_count++;
}

static uint8_t pace_delay_elapsed(void)
{
    uint32_t now = HAL_GetTick();

    return ((int32_t)(now - bus_rt.pace_until_ms) >= 0) ? 1U : 0U;
}
static void finish_burst(void)
{
    /*
     * Po poslednim platnem byte M16C dela jeste jeden extra RD.
     * Nechceme, aby precetl znovu CR. Testujeme 0xFF jako neplatny byte,
     * ktery by nemel vypadat jako konec dalsi vety.
     */
    bus_drive_byte(0xFFU);

    status_set_rxf_high();

    bus_rt.byte_presented = 0U;
    bus_rt.pacing_active = 0U;
    bus_rt.tx_mode = M16C_TX_MODE_PACED;
    bus_rt.burst_done_count++;
}

static void present_next_byte_if_possible(void)
{
    uint8_t data;

    if (bus_rt.byte_presented)
    {
        return;
    }

    if (bus_rt.pacing_active)
    {
        if (!pace_delay_elapsed())
        {
            return;
        }

        bus_rt.pacing_active = 0U;
        bus_rt.pace_release_count++;
    }

    if (tx_fifo_peek(&data))
    {
        bus_drive_byte(data);
        status_set_rxf_low();
    }
    else
    {
        status_set_rxf_high();
        bus_force_hi_z();
    }
}

static void complete_read_cycle(void)
{
    uint8_t dummy;
    uint8_t next_data;
    uint8_t rd_bus;

    /*
     * Snapshot sbernice presne v okamziku zpracovani RD# rising.
     * Pokud STM32 bus ridi spravne, u validniho cteni by to melo sedet
     * s TX trace.
     */
    rd_bus = bus_read_u8();

    /*
     * Extra RD mimo platny byte.
     */
    if (!bus_rt.byte_presented)
    {
        bus_rt.extra_rd_count++;
        rd_trace_push(rd_bus);
        return;
    }

    rd_trace_push(rd_bus);

    if (tx_fifo_pop(&dummy))
    {
        bus_rt.tx_bytes_read_count++;
    }

    if (bus_rt.tx_mode == M16C_TX_MODE_BURST)
{
    /*
     * Burst sentence:
     * RXF# zustava LOW, pokud mame dalsi byte.
     * Hned po RD# rising prehodime D0..D7 na dalsi byte.
     */
    if (tx_fifo_peek(&next_data))
    {
        bus_drive_byte(next_data);
        status_set_rxf_low();
    }
    else
    {
        finish_burst();
    }

    return;
}

    enter_pace_hold();
}

static void handle_wr_edge(uint8_t wr, uint8_t bus, uint8_t from_irq)
{
    if (from_irq)
    {
        bus_rt.wr_irq_count++;
    }
    else
    {
        bus_rt.wr_poll_count++;
    }

    if (wr != 0U)
    {
        bus_rt.status.wr_rising_count++;
        bus_rt.status.last_wr_bus = bus;

#if M16C_CAPTURE_WR_ON_HIGH
        if (bus_rt.wr_capture_active)
        {
            rx_capture_push(bus);
        }
#endif
    }
    else
    {
        bus_rt.status.wr_falling_count++;
        bus_rt.status.last_wr_bus = bus;

#if !M16C_CAPTURE_WR_ON_HIGH
        if (bus_rt.wr_capture_active)
        {
            rx_capture_push(bus);
        }
#endif
    }

    if (wr_capture_should_stop())
    {
        wr_capture_stop_internal();
    }
}

static void schedule_auto_boot(uint32_t delay_ms)
{
    bus_rt.auto_boot_pending = 1U;
    bus_rt.auto_boot_due_ms = HAL_GetTick() + delay_ms;
    bus_rt.auto_boot_request_count++;
}

static uint8_t auto_boot_due(void)
{
    uint32_t now = HAL_GetTick();

    if (!bus_rt.auto_boot_pending)
    {
        return 0U;
    }

    return ((int32_t)(now - bus_rt.auto_boot_due_ms) >= 0) ? 1U : 0U;
}

static void queue_string_burst(const char *text);

static void process_auto_boot(void)
{
    if (!auto_boot_due())
    {
        return;
    }

    /*
     * Pokud zrovna neco vysilame, auto boot odlozime.
     */
    if (bus_rt.byte_presented || !tx_fifo_is_empty())
    {
        bus_rt.auto_boot_due_ms = HAL_GetTick() + 50U;
        return;
    }

    bus_rt.auto_boot_pending = 0U;
    bus_rt.auto_boot_sent_count++;

    queue_string_burst(M16C_VDPS_BOOT_BANNER);

    uart_write_str("[M16C-VNC] auto VDPS boot banner queued\r\n");
}

static void configure_bus_inputs_no_pull(void)
{
    bus_force_hi_z();
}

static void configure_control_inputs_no_pull(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_DATAREQ_Pin | M16C_IORESET_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_RD_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(M16C_RD_GPIO_Port, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI1_IRQn, 1U, 0U);
    HAL_NVIC_EnableIRQ(EXTI1_IRQn);

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_WR_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;

    HAL_GPIO_Init(M16C_WR_GPIO_Port, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI2_IRQn, 2U, 0U);
    wr_exti_disable();
}

static void configure_status_outputs(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    status_set_txe();
    status_set_rxf_high();

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin = M16C_TXE_Pin | M16C_RXF_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    status_set_txe();
    status_set_rxf_high();
}

static void m16c_vnc_bus_sample_now(void)
{
    bus_rt.status.bus = bus_read_u8();
    bus_rt.status.rd = gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);
    bus_rt.status.wr = gpio_read_pin_u8(M16C_WR_GPIO_Port, M16C_WR_Pin);
    bus_rt.status.datareq = gpio_read_pin_u8(M16C_DATAREQ_GPIO_Port, M16C_DATAREQ_Pin);
    bus_rt.status.ioreset = gpio_read_pin_u8(M16C_IORESET_GPIO_Port, M16C_IORESET_Pin);
    bus_rt.status.txe = gpio_read_pin_u8(M16C_TXE_GPIO_Port, M16C_TXE_Pin);
    bus_rt.status.rxf = gpio_read_pin_u8(M16C_RXF_GPIO_Port, M16C_RXF_Pin);
}

static void handle_wr_poll(uint8_t wr, uint8_t bus)
{
    if (wr != bus_rt.prev_wr)
    {
        handle_wr_edge(wr, bus, 0U);
    }
}

static void handle_misc_poll(uint8_t datareq, uint8_t ioreset, uint8_t txe, uint8_t rxf)
{
    if ((bus_rt.prev_datareq == 1U) && (datareq == 0U))
    {
        bus_rt.status.datareq_falling_count++;
    }
    else if ((bus_rt.prev_datareq == 0U) && (datareq == 1U))
    {
        bus_rt.status.datareq_rising_count++;
    }

    if ((bus_rt.prev_ioreset == 1U) && (ioreset == 0U))
    {
        bus_rt.status.ioreset_falling_count++;
    }
    else if ((bus_rt.prev_ioreset == 0U) && (ioreset == 1U))
    {
        bus_rt.status.ioreset_rising_count++;

        /*
         * VNC IORESET# se uvolnil.
         * Naplanujeme VDPS startup banner do ocekavaneho init okna.
         */
        schedule_auto_boot(M16C_AUTO_BOOT_RESET_DELAY_MS);
    }

    if ((bus_rt.prev_txe == 1U) && (txe == 0U))
    {
        bus_rt.txe_falling_count++;
    }
    else if ((bus_rt.prev_txe == 0U) && (txe == 1U))
    {
        bus_rt.txe_rising_count++;
    }

    if ((bus_rt.prev_rxf == 1U) && (rxf == 0U))
    {
        bus_rt.rxf_falling_count++;
    }
    else if ((bus_rt.prev_rxf == 0U) && (rxf == 1U))
    {
        bus_rt.rxf_rising_count++;
    }
}

static void queue_string_paced(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    bus_rt.tx_mode = M16C_TX_MODE_PACED;

    while (*text != '\0')
    {
        (void)tx_fifo_push((uint8_t)*text);
        text++;
    }

    present_next_byte_if_possible();
}

static void queue_string_burst(const char *text)
{
    if (text == NULL)
    {
        return;
    }

    /*
     * Burst event ma byt jedna souvisla VNC veta.
     * Pro jistotu zacneme z prazdneho TX FIFO.
     */
    tx_fifo_clear();

    bus_rt.byte_presented = 0U;
    bus_rt.pacing_active = 0U;
    bus_rt.tx_mode = M16C_TX_MODE_BURST;

    status_set_rxf_high();
    bus_force_hi_z();

    while (*text != '\0')
    {
        (void)tx_fifo_push((uint8_t)*text);
        text++;
    }

    bus_rt.burst_start_count++;

    present_next_byte_if_possible();
}

void m16c_vnc_bus_init(void)
{
    memset(&bus_rt, 0, sizeof(bus_rt));

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    bus_rt.ready_mode = 0U;
    bus_rt.pacing_active = 0U;
    bus_rt.byte_presented = 0U;
    bus_rt.wr_capture_active = 0U;
    bus_rt.tx_mode = M16C_TX_MODE_PACED;

    configure_bus_inputs_no_pull();
    configure_control_inputs_no_pull();
    configure_status_outputs();

    m16c_vnc_bus_sample_now();

    bus_rt.prev_rd = bus_rt.status.rd;
    bus_rt.prev_wr = bus_rt.status.wr;
    bus_rt.prev_datareq = bus_rt.status.datareq;
    bus_rt.prev_ioreset = bus_rt.status.ioreset;
    bus_rt.prev_txe = bus_rt.status.txe;
    bus_rt.prev_rxf = bus_rt.status.rxf;

    /*
     * Auto banner po startu STM32.
     * Pokud M16C jeste neni pripravene, dalsi sance je IORESET# rising.
     */
    schedule_auto_boot(M16C_AUTO_BOOT_INIT_DELAY_MS);

    uart_write_str("[M16C-VNC] paced/burst read emulation init\r\n");
    uart_write_str("[M16C-VNC] auto VDPS boot banner with prompt enabled\r\n");
    uart_write_str("[M16C-VNC] burst mode for SDA/SDD/DD2 events\r\n");
    uart_write_str("[M16C-VNC] extra RD without presented byte is ignored\r\n");
    uart_write_str("[M16C-VNC] safe WR capture, EXTI2 disabled by default\r\n");
}

void m16c_vnc_bus_task(void)
{
    uint8_t rd;
    uint8_t wr;
    uint8_t datareq;
    uint8_t ioreset;
    uint8_t txe;
    uint8_t rxf;
    uint8_t bus;

    if (wr_capture_should_stop())
    {
        wr_capture_stop_internal();
    }

    status_set_txe();

    process_auto_boot();

    /*
     * V burst modu se dalsi byte vystavuje primarne v RD IRQ.
     * Tady jen nastartujeme prvni byte nebo paced stav.
     */
    present_next_byte_if_possible();

    bus = bus_read_u8();

    rd = gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);
    wr = gpio_read_pin_u8(M16C_WR_GPIO_Port, M16C_WR_Pin);
    datareq = gpio_read_pin_u8(M16C_DATAREQ_GPIO_Port, M16C_DATAREQ_Pin);
    ioreset = gpio_read_pin_u8(M16C_IORESET_GPIO_Port, M16C_IORESET_Pin);
    txe = gpio_read_pin_u8(M16C_TXE_GPIO_Port, M16C_TXE_Pin);
    rxf = gpio_read_pin_u8(M16C_RXF_GPIO_Port, M16C_RXF_Pin);

    if ((bus_rt.prev_rd == 1U) && (rd == 0U))
    {
        bus_rt.status.rd_falling_count++;
        bus_rt.status.last_rd_bus = bus;
        bus_rt.rd_poll_count++;
    }
    else if ((bus_rt.prev_rd == 0U) && (rd == 1U))
    {
        bus_rt.status.rd_rising_count++;
        bus_rt.status.last_rd_bus = bus;
        bus_rt.rd_poll_count++;
        complete_read_cycle();
    }

    if (bus_rt.wr_capture_active)
    {
        handle_wr_poll(wr, bus);
    }

    handle_misc_poll(datareq, ioreset, txe, rxf);

    bus_rt.prev_rd = rd;
    bus_rt.prev_wr = wr;
    bus_rt.prev_datareq = datareq;
    bus_rt.prev_ioreset = ioreset;
    bus_rt.prev_txe = txe;
    bus_rt.prev_rxf = rxf;

    m16c_vnc_bus_sample_now();
}

void m16c_vnc_bus_rd_irq(void)
{
    uint8_t rd;
    uint8_t bus;

    bus_rt.rd_irq_count++;

    rd = gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);
    bus = bus_read_u8();

    if (rd == 0U)
    {
        bus_rt.status.rd_falling_count++;
        bus_rt.status.last_rd_bus = bus;
    }
    else
    {
        bus_rt.status.rd_rising_count++;
        bus_rt.status.last_rd_bus = bus;

        complete_read_cycle();
    }

    bus_rt.prev_rd = rd;
}

void m16c_vnc_bus_wr_irq(void)
{
    uint8_t wr;
    uint8_t bus;

    if (!bus_rt.wr_capture_active)
    {
        wr_exti_disable();
        return;
    }

    wr = gpio_read_pin_u8(M16C_WR_GPIO_Port, M16C_WR_Pin);
    bus = bus_read_u8();

    handle_wr_edge(wr, bus, 1U);

    bus_rt.prev_wr = wr;
}

void m16c_vnc_bus_get_status(m16c_vnc_bus_status_t *status)
{
    if (status == NULL)
    {
        return;
    }

    *status = bus_rt.status;
}

void m16c_vnc_bus_reset_counters(void)
{
    uint8_t last_wr = bus_rt.status.last_wr_bus;
    uint8_t last_rd = bus_rt.status.last_rd_bus;

    memset(&bus_rt.status, 0, sizeof(bus_rt.status));

    bus_rt.status.last_wr_bus = last_wr;
    bus_rt.status.last_rd_bus = last_rd;

    bus_rt.txe_falling_count = 0U;
    bus_rt.txe_rising_count = 0U;
    bus_rt.rxf_falling_count = 0U;
    bus_rt.rxf_rising_count = 0U;

    bus_rt.tx_bytes_presented_count = 0U;
    bus_rt.tx_bytes_read_count = 0U;

    bus_rt.rd_irq_count = 0U;
    bus_rt.rd_poll_count = 0U;
    bus_rt.extra_rd_count = 0U;

    bus_rt.wr_irq_count = 0U;
    bus_rt.wr_poll_count = 0U;

    bus_rt.pace_release_count = 0U;
    bus_rt.pace_hold_count = 0U;

    bus_rt.tx_fifo_overflow_count = 0U;

    bus_rt.burst_start_count = 0U;
    bus_rt.burst_done_count = 0U;

    /*
     * Auto boot counters neresetujeme uplne, aby bylo videt,
     * ze probehl uz pred prikazem reset.
     */

    rx_capture_clear();
    tx_trace_clear();
    rd_trace_clear();
    m16c_vnc_bus_sample_now();

    bus_rt.prev_rd = bus_rt.status.rd;
    bus_rt.prev_wr = bus_rt.status.wr;
    bus_rt.prev_datareq = bus_rt.status.datareq;
    bus_rt.prev_ioreset = bus_rt.status.ioreset;
    bus_rt.prev_txe = bus_rt.status.txe;
    bus_rt.prev_rxf = bus_rt.status.rxf;
}

void m16c_vnc_bus_set_ready(uint8_t ready)
{
    bus_rt.ready_mode = ready ? 1U : 0U;

    status_set_txe();
    m16c_vnc_bus_sample_now();

    if (bus_rt.ready_mode)
    {
        uart_write_str("[M16C-VNC] ready mode: TXE#=0\r\n");
    }
    else
    {
        uart_write_str("[M16C-VNC] idle mode: TXE#=1\r\n");
    }
}

void m16c_vnc_bus_arm_wr_capture(void)
{
    rx_capture_clear();

    bus_rt.wr_irq_count = 0U;
    bus_rt.wr_poll_count = 0U;

    bus_rt.status.wr_falling_count = 0U;
    bus_rt.status.wr_rising_count = 0U;

    bus_rt.wr_capture_edge_limit = M16C_WR_CAPTURE_MAX_EDGES;
    bus_rt.wr_capture_byte_limit = M16C_WR_CAPTURE_MAX_BYTES;
    bus_rt.wr_capture_until_ms = HAL_GetTick() + M16C_WR_CAPTURE_TIMEOUT_MS;

    bus_rt.wr_capture_active = 1U;
    bus_rt.ready_mode = 1U;

    m16c_vnc_bus_sample_now();
    bus_rt.prev_wr = bus_rt.status.wr;

    status_set_txe();
    wr_exti_enable();

    uart_write_str("[M16C-VNC] WR capture armed, TXE#=0\r\n");
}

void m16c_vnc_bus_stop_wr_capture(void)
{
    wr_capture_stop_internal();
    uart_write_str("[M16C-VNC] WR capture stopped, TXE#=1\r\n");
}

uint8_t m16c_vnc_bus_is_ready(void)
{
    return bus_rt.ready_mode ? 1U : 0U;
}

void m16c_vnc_bus_clear_tx_fifo(void)
{
    tx_fifo_clear();

    bus_rt.byte_presented = 0U;
    bus_rt.pacing_active = 0U;
    bus_rt.tx_mode = M16C_TX_MODE_PACED;

    status_set_rxf_high();
    bus_force_hi_z();
}

void m16c_vnc_bus_queue_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (data == NULL)
    {
        return;
    }

    bus_rt.tx_mode = M16C_TX_MODE_PACED;

    for (i = 0U; i < len; i++)
    {
        (void)tx_fifo_push(data[i]);
    }

    present_next_byte_if_possible();
}

void m16c_vnc_bus_queue_string(const char *text)
{
    queue_string_paced(text);
}

void m16c_vnc_bus_queue_boot_banner(void)
{
    queue_string_paced(">\r");
}

void m16c_vnc_bus_queue_pc_attached(void)
{
    /*
     * PC Control target:
     * dorucit SDA\r jako jednu souvislou VNC vetu.
     */
    queue_string_burst("SDA\r");
}

void m16c_vnc_bus_queue_pc6_and_arm_wr(void)
{
    /*
     * Atomicky:
     * - arm WR capture
     * - pak poslat FTDI short event + prompt:
     *     SDA\r>\r
     *
     * Cilem je zachytit, zda M16C po PC attach eventu zapise do VNC
     * prikaz jako napr. SC S\r nebo podobny.
     */
    m16c_vnc_bus_arm_wr_capture();
    m16c_vnc_bus_queue_pc_attached_sda_prompt_cr();
}

void m16c_vnc_bus_queue_pc_attached_prompt(void)
{
    /*
     * Varianta:
     *   SDA\r>
     */
    queue_string_burst("SDA\r>");
}

void m16c_vnc_bus_queue_pc_attached_drive_prompt(void)
{
    /*
     * Varianta:
     *   SDA\rD:\>
     */
    queue_string_burst("SDA\rD:\\>");
}

void m16c_vnc_bus_queue_pc_attached_double_prompt(void)
{
    /*
     * Varianta:
     *   SDA\r\r>
     */
    queue_string_burst("SDA\r\r>");
}

void m16c_vnc_bus_queue_pc_attached_slave_enabled_prompt(void)
{
    /*
     * FTDI extended event varianta:
     *   Slave Enabled\r>\r
     */
    queue_string_burst("Slave Enabled\r>\r");
}

void m16c_vnc_bus_queue_pc_attached_sda_prompt_cr(void)
{
    /*
     * FTDI short event + prompt varianta:
     *   SDA\r>\r
     */
    queue_string_burst("SDA\r>\r");
}

void m16c_vnc_bus_queue_pc_attached_slow(void)
{
    /*
     * Varianta:
     *   SDA\r
     * ale paced/FIFO style, ne burst.
     *
     * Cilem je overit, jestli M16C event parser nechce spis postupne
     * FIFO chovani misto jedne souvisle burst vety.
     */
    queue_string_paced("SDA\r");
}


void m16c_vnc_bus_queue_pc_detached(void)
{
    queue_string_burst("SDD\r");
}

void m16c_vnc_bus_queue_host_device_attached(void)
{
    queue_string_burst("DD2\r");
}

void m16c_vnc_bus_format_status(char *out, uint32_t out_size)
{
    uint32_t pos = 0U;
    uint8_t i;
    m16c_vnc_bus_status_t s;

    if ((out == NULL) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';

    m16c_vnc_bus_get_status(&s);

    out_append(out, out_size, &pos, "m16c ");

    out_append(out, out_size, &pos, "mode=");
    out_append(out, out_size, &pos, bus_rt.ready_mode ? "ready" : "idle");

    out_append(out, out_size, &pos, " wrarm=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.wr_capture_active);

    out_append(out, out_size, &pos, " txmode=");
    out_append(out, out_size, &pos, (bus_rt.tx_mode == M16C_TX_MODE_BURST) ? "burst" : "paced");

    out_append(out, out_size, &pos, " boot_pending=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.auto_boot_pending);

    out_append(out, out_size, &pos, " boot_req=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.auto_boot_request_count);

    out_append(out, out_size, &pos, " boot_sent=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.auto_boot_sent_count);

    out_append(out, out_size, &pos, " fifo=");
    out_append_u32_dec(out, out_size, &pos, tx_fifo_count());

    out_append(out, out_size, &pos, " present=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.byte_presented);

    out_append(out, out_size, &pos, " pacing=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.pacing_active);

    out_append(out, out_size, &pos, " busdrv=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.bus_driving);

    out_append(out, out_size, &pos, " bus=");
    out_append_hex8(out, out_size, &pos, s.bus);

    out_append(out, out_size, &pos, " rd=");
    out_append_u32_dec(out, out_size, &pos, s.rd);

    out_append(out, out_size, &pos, " wr=");
    out_append_u32_dec(out, out_size, &pos, s.wr);

    out_append(out, out_size, &pos, " datareq=");
    out_append_u32_dec(out, out_size, &pos, s.datareq);

    out_append(out, out_size, &pos, " reset=");
    out_append_u32_dec(out, out_size, &pos, s.ioreset);

    out_append(out, out_size, &pos, " txe=");
    out_append_u32_dec(out, out_size, &pos, s.txe);

    out_append(out, out_size, &pos, " rxf=");
    out_append_u32_dec(out, out_size, &pos, s.rxf);

    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "edges ");

    out_append(out, out_size, &pos, "rd_f=");
    out_append_u32_dec(out, out_size, &pos, s.rd_falling_count);

    out_append(out, out_size, &pos, " rd_r=");
    out_append_u32_dec(out, out_size, &pos, s.rd_rising_count);

    out_append(out, out_size, &pos, " wr_f=");
    out_append_u32_dec(out, out_size, &pos, s.wr_falling_count);

    out_append(out, out_size, &pos, " wr_r=");
    out_append_u32_dec(out, out_size, &pos, s.wr_rising_count);

    out_append(out, out_size, &pos, " drq_f=");
    out_append_u32_dec(out, out_size, &pos, s.datareq_falling_count);

    out_append(out, out_size, &pos, " drq_r=");
    out_append_u32_dec(out, out_size, &pos, s.datareq_rising_count);

    out_append(out, out_size, &pos, " rst_f=");
    out_append_u32_dec(out, out_size, &pos, s.ioreset_falling_count);

    out_append(out, out_size, &pos, " rst_r=");
    out_append_u32_dec(out, out_size, &pos, s.ioreset_rising_count);

    out_append(out, out_size, &pos, " txe_f=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.txe_falling_count);

    out_append(out, out_size, &pos, " txe_r=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.txe_rising_count);

    out_append(out, out_size, &pos, " rxf_f=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rxf_falling_count);

    out_append(out, out_size, &pos, " rxf_r=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rxf_rising_count);

    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "xfer ");

    out_append(out, out_size, &pos, "shown=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_bytes_presented_count);

    out_append(out, out_size, &pos, " read=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_bytes_read_count);

    out_append(out, out_size, &pos, " rd_irq=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rd_irq_count);

    out_append(out, out_size, &pos, " rd_poll=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rd_poll_count);

    out_append(out, out_size, &pos, " extra_rd=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.extra_rd_count);

    out_append(out, out_size, &pos, " hold=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.pace_hold_count);

    out_append(out, out_size, &pos, " rel=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.pace_release_count);

    out_append(out, out_size, &pos, " burst_start=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.burst_start_count);

    out_append(out, out_size, &pos, " burst_done=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.burst_done_count);

    out_append(out, out_size, &pos, " ovf=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_fifo_overflow_count);

    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "rx ");

    out_append(out, out_size, &pos, "wr_irq=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.wr_irq_count);

    out_append(out, out_size, &pos, " wr_poll=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.wr_poll_count);

    out_append(out, out_size, &pos, " count=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rx_capture_count);

    out_append(out, out_size, &pos, " total=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rx_capture_total_count);

    out_append(out, out_size, &pos, " ovf=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rx_capture_overflow_count);

    out_append(out, out_size, &pos, " data=");

    if (bus_rt.rx_capture_count == 0U)
    {
        out_append(out, out_size, &pos, "-");
    }
    else
    {
        for (i = 0U; i < bus_rt.rx_capture_count; i++)
        {
            if (i != 0U)
            {
                out_append(out, out_size, &pos, " ");
            }

            out_append_hex8_raw(out, out_size, &pos, bus_rt.rx_capture[i]);
        }
    }

    out_append(out, out_size, &pos, "\r\n");
    out_append(out, out_size, &pos, "txtrace ");

out_append(out, out_size, &pos, "count=");
out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_count);

out_append(out, out_size, &pos, " total=");
out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_total_count);

out_append(out, out_size, &pos, " ovf=");
out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_overflow_count);

out_append(out, out_size, &pos, " data=");

if (bus_rt.tx_trace_count == 0U)
{
    out_append(out, out_size, &pos, "-");
}
else
{
    for (i = 0U; i < bus_rt.tx_trace_count; i++)
    {
        if (i != 0U)
        {
            out_append(out, out_size, &pos, " ");
        }

        out_append_hex8_raw(out, out_size, &pos, bus_rt.tx_trace[i]);
    }
}

out_append(out, out_size, &pos, "\r\n");

out_append(out, out_size, &pos, "rdtrace ");

out_append(out, out_size, &pos, "count=");
out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_count);

out_append(out, out_size, &pos, " total=");
out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_total_count);

out_append(out, out_size, &pos, " ovf=");
out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_overflow_count);

out_append(out, out_size, &pos, " data=");

if (bus_rt.rd_trace_count == 0U)
{
    out_append(out, out_size, &pos, "-");
}
else
{
    for (i = 0U; i < bus_rt.rd_trace_count; i++)
    {
        if (i != 0U)
        {
            out_append(out, out_size, &pos, " ");
        }

        out_append_hex8_raw(out, out_size, &pos, bus_rt.rd_trace[i]);
    }
}

out_append(out, out_size, &pos, "\r\n");
    out_append(out, out_size, &pos, "last ");

    out_append(out, out_size, &pos, "wr_bus=");
    out_append_hex8(out, out_size, &pos, s.last_wr_bus);

    out_append(out, out_size, &pos, " rd_bus=");
    out_append_hex8(out, out_size, &pos, s.last_rd_bus);

    out_append(out, out_size, &pos, "\r\n");
}
void m16c_vnc_bus_format_trace(char *out, uint32_t out_size)
{
    uint32_t pos = 0U;
    uint8_t i;

    if ((out == NULL) || (out_size == 0U))
    {
        return;
    }

    out[0] = '\0';

    out_append(out, out_size, &pos, "txtrace ");

    out_append(out, out_size, &pos, "count=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_count);

    out_append(out, out_size, &pos, " total=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_total_count);

    out_append(out, out_size, &pos, " ovf=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_trace_overflow_count);

    out_append(out, out_size, &pos, " data=");

    if (bus_rt.tx_trace_count == 0U)
    {
        out_append(out, out_size, &pos, "-");
    }
    else
    {
        for (i = 0U; i < bus_rt.tx_trace_count; i++)
        {
            if (i != 0U)
            {
                out_append(out, out_size, &pos, " ");
            }

            out_append_hex8_raw(out, out_size, &pos, bus_rt.tx_trace[i]);
        }
    }

    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "rdtrace ");

    out_append(out, out_size, &pos, "count=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_count);

    out_append(out, out_size, &pos, " total=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_total_count);

    out_append(out, out_size, &pos, " ovf=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.rd_trace_overflow_count);

    out_append(out, out_size, &pos, " data=");

    if (bus_rt.rd_trace_count == 0U)
    {
        out_append(out, out_size, &pos, "-");
    }
    else
    {
        for (i = 0U; i < bus_rt.rd_trace_count; i++)
        {
            if (i != 0U)
            {
                out_append(out, out_size, &pos, " ");
            }

            out_append_hex8_raw(out, out_size, &pos, bus_rt.rd_trace[i]);
        }
    }

    out_append(out, out_size, &pos, "\r\n");
}