#include "m16c_vnc_bus.h"

#include "main.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>


/*
 * M16C / VNC1L compatibility bus PACED BYTE READ EMULATION
 *
 * Proc tato verze:
 * - M16C TimerB5 ISR umi v jednom pruchodu cist vice bytu, dokud RXF# zustava LOW.
 * - Nase SW emulace nestihala spolehlive posouvat FIFO tak rychle jako HW VNC1L.
 * - Proto posilame jen jeden byte, pak RXF# vratime HIGH a pockame par ms.
 *
 * Princip:
 *   FIFO ma byte:
 *     PA0..PA7 = output s bytem
 *     RXF# = LOW
 *
 *   M16C precte byte pres RD#:
 *     na RD# rising byte popneme
 *     RXF# = HIGH
 *     PA0..PA7 = Hi-Z
 *     cekame M16C_PACE_DELAY_MS
 *
 *   Po delay:
 *     pokud je ve FIFO dalsi byte, pripravime dalsi byte a RXF# LOW
 *
 * Bezpecnost:
 * - PA0..PA7 jsou output pouze kdyz je aktivne nabizen jeden byte.
 * - Mimo nabizeni bytu jsou PA0..PA7 input / Hi-Z.
 * - D0..D7 maji na test desce 1k seriove odpory.
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

/*
 * M16C cteni odpovedi ma v puvodnim FW timeout kolem 2 ms.
 * Dáme pauzu vetsi nez to, aby se dalsi byte cetl az v dalsim TimerB5 ISR.
 */
#define M16C_PACE_DELAY_MS              4U


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

    uint8_t tx_fifo[M16C_TX_FIFO_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

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
    uint32_t pace_release_count;
    uint32_t pace_hold_count;
} m16c_vnc_bus_runtime_t;


static m16c_vnc_bus_runtime_t bus_rt;


static uint8_t gpio_read_pin_u8(
    GPIO_TypeDef *port,
    uint16_t pin)
{
    return
        (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET) ? 1U : 0U;
}


static uint8_t bus_read_u8(void)
{
    uint32_t idr =
        M16C_BUS_GPIO_Port->IDR;

    return
        (uint8_t)(idr & 0xFFU);
}


static void out_append(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    const char *text)
{
    if((out == NULL) || (pos == NULL) || (text == NULL) || (out_size == 0U))
    {
        return;
    }

    while((*text != '\0') && ((*pos + 1U) < out_size))
    {
        out[*pos] =
            *text;

        (*pos)++;
        text++;
    }

    out[*pos] =
        '\0';
}


static void out_append_u32_dec(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    uint32_t value)
{
    char tmp[11];
    uint32_t i =
        0U;

    if(value == 0U)
    {
        out_append(out, out_size, pos, "0");
        return;
    }

    while((value > 0U) && (i < sizeof(tmp)))
    {
        tmp[i] =
            (char)('0' + (value % 10U));

        i++;
        value /=
            10U;
    }

    while(i > 0U)
    {
        char c[2];

        i--;

        c[0] =
            tmp[i];

        c[1] =
            '\0';

        out_append(out, out_size, pos, c);
    }
}


static void out_append_hex4(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    uint8_t value)
{
    uint8_t nibble =
        value & 0x0FU;

    char c[2];

    if(nibble < 10U)
    {
        c[0] =
            (char)('0' + nibble);
    }
    else
    {
        c[0] =
            (char)('A' + nibble - 10U);
    }

    c[1] =
        '\0';

    out_append(out, out_size, pos, c);
}


static void out_append_hex8(
    char *out,
    uint32_t out_size,
    uint32_t *pos,
    uint8_t value)
{
    out_append(out, out_size, pos, "0x");

    out_append_hex4(
        out,
        out_size,
        pos,
        (uint8_t)(value >> 4));

    out_append_hex4(
        out,
        out_size,
        pos,
        value);
}


static uint16_t tx_fifo_next(
    uint16_t index)
{
    index++;

    if(index >= M16C_TX_FIFO_SIZE)
    {
        index =
            0U;
    }

    return
        index;
}


static uint8_t tx_fifo_is_empty(void)
{
    return
        (bus_rt.tx_head == bus_rt.tx_tail) ? 1U : 0U;
}


static uint8_t tx_fifo_push(
    uint8_t data)
{
    uint16_t next =
        tx_fifo_next(bus_rt.tx_head);

    if(next == bus_rt.tx_tail)
    {
        bus_rt.tx_fifo_overflow_count++;
        return
            0U;
    }

    bus_rt.tx_fifo[bus_rt.tx_head] =
        data;

    bus_rt.tx_head =
        next;

    return
        1U;
}


static uint8_t tx_fifo_peek(
    uint8_t *data)
{
    if(data == NULL)
    {
        return
            0U;
    }

    if(tx_fifo_is_empty())
    {
        return
            0U;
    }

    *data =
        bus_rt.tx_fifo[bus_rt.tx_tail];

    return
        1U;
}


static uint8_t tx_fifo_pop(
    uint8_t *data)
{
    if(data == NULL)
    {
        return
            0U;
    }

    if(tx_fifo_is_empty())
    {
        return
            0U;
    }

    *data =
        bus_rt.tx_fifo[bus_rt.tx_tail];

    bus_rt.tx_tail =
        tx_fifo_next(bus_rt.tx_tail);

    return
        1U;
}


static uint32_t tx_fifo_count(void)
{
    if(bus_rt.tx_head >= bus_rt.tx_tail)
    {
        return
            (uint32_t)(bus_rt.tx_head - bus_rt.tx_tail);
    }

    return
        (uint32_t)(M16C_TX_FIFO_SIZE - bus_rt.tx_tail + bus_rt.tx_head);
}


static void bus_force_hi_z(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin =
        M16C_BUS_PIN_MASK;

    GPIO_InitStruct.Mode =
        GPIO_MODE_INPUT;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        M16C_BUS_GPIO_Port,
        &GPIO_InitStruct);

    bus_rt.bus_driving =
        0U;
}


static void bus_drive_byte(
    uint8_t data)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    uint32_t odr;

    /*
     * Nejdriv nastavime latch, potom prepneme PA0..PA7 na output.
     */
    odr =
        M16C_BUS_GPIO_Port->ODR;

    odr &=
        ~0xFFU;

    odr |=
        data;

    M16C_BUS_GPIO_Port->ODR =
        odr;

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin =
        M16C_BUS_PIN_MASK;

    GPIO_InitStruct.Mode =
        GPIO_MODE_OUTPUT_PP;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(
        M16C_BUS_GPIO_Port,
        &GPIO_InitStruct);

    bus_rt.bus_driving =
        1U;

    bus_rt.presented_byte =
        data;

    bus_rt.byte_presented =
        1U;

    bus_rt.tx_bytes_presented_count++;
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
    HAL_GPIO_WritePin(
        M16C_RXF_GPIO_Port,
        M16C_RXF_Pin,
        GPIO_PIN_SET);
}


static void status_set_rxf_low(void)
{
    HAL_GPIO_WritePin(
        M16C_RXF_GPIO_Port,
        M16C_RXF_Pin,
        GPIO_PIN_RESET);
}


static void enter_pace_hold(void)
{
    /*
     * Po jednom prectenem bytu skryjeme data a zvedneme RXF#.
     * Dalsi byte nabidneme az po M16C_PACE_DELAY_MS.
     */
    status_set_rxf_high();
    bus_force_hi_z();

    bus_rt.byte_presented =
        0U;

    bus_rt.pacing_active =
        1U;

    bus_rt.pace_until_ms =
        HAL_GetTick() + M16C_PACE_DELAY_MS;

    bus_rt.pace_hold_count++;
}


static uint8_t pace_delay_elapsed(void)
{
    uint32_t now =
        HAL_GetTick();

    return
        ((int32_t)(now - bus_rt.pace_until_ms) >= 0) ? 1U : 0U;
}


static void present_next_byte_if_possible(void)
{
    uint8_t data;

    if(bus_rt.byte_presented)
    {
        return;
    }

    if(bus_rt.pacing_active)
    {
        if(!pace_delay_elapsed())
        {
            return;
        }

        bus_rt.pacing_active =
            0U;

        bus_rt.pace_release_count++;
    }

    if(tx_fifo_peek(&data))
    {
        /*
         * Data nejdrive vystavit na D0..D7,
         * az potom RXF# LOW.
         */
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

    if(bus_rt.byte_presented)
    {
        if(tx_fifo_pop(&dummy))
        {
            bus_rt.tx_bytes_read_count++;
        }
    }

    enter_pace_hold();
}


static void configure_bus_inputs_no_pull(void)
{
    bus_force_hi_z();
}


static void configure_control_inputs_no_pull(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /*
     * WR / DATAREQ / IORESET jako bezne vstupy.
     */
    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin =
        M16C_WR_Pin |
        M16C_DATAREQ_Pin |
        M16C_IORESET_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_INPUT;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        GPIOB,
        &GPIO_InitStruct);

    /*
     * RD# pres EXTI rising/falling.
     * Falling je diagnostika, rising posouva FIFO.
     */
    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin =
        M16C_RD_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_IT_RISING_FALLING;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    HAL_GPIO_Init(
        M16C_RD_GPIO_Port,
        &GPIO_InitStruct);

    HAL_NVIC_SetPriority(
        EXTI1_IRQn,
        1U,
        0U);

    HAL_NVIC_EnableIRQ(
        EXTI1_IRQn);
}


static void configure_status_outputs(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /*
     * Pred prepnoutim pinu do output nastavime safe latch.
     */
    status_set_txe();
    status_set_rxf_high();

    memset(&GPIO_InitStruct, 0, sizeof(GPIO_InitStruct));

    GPIO_InitStruct.Pin =
        M16C_TXE_Pin |
        M16C_RXF_Pin;

    GPIO_InitStruct.Mode =
        GPIO_MODE_OUTPUT_PP;

    GPIO_InitStruct.Pull =
        GPIO_NOPULL;

    GPIO_InitStruct.Speed =
        GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(
        GPIOB,
        &GPIO_InitStruct);

    status_set_txe();
    status_set_rxf_high();
}


static void m16c_vnc_bus_sample_now(void)
{
    bus_rt.status.bus =
        bus_read_u8();

    bus_rt.status.rd =
        gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);

    bus_rt.status.wr =
        gpio_read_pin_u8(M16C_WR_GPIO_Port, M16C_WR_Pin);

    bus_rt.status.datareq =
        gpio_read_pin_u8(M16C_DATAREQ_GPIO_Port, M16C_DATAREQ_Pin);

    bus_rt.status.ioreset =
        gpio_read_pin_u8(M16C_IORESET_GPIO_Port, M16C_IORESET_Pin);

    bus_rt.status.txe =
        gpio_read_pin_u8(M16C_TXE_GPIO_Port, M16C_TXE_Pin);

    bus_rt.status.rxf =
        gpio_read_pin_u8(M16C_RXF_GPIO_Port, M16C_RXF_Pin);
}


static void handle_wr_poll(
    uint8_t wr,
    uint8_t bus)
{
    if((bus_rt.prev_wr == 1U) && (wr == 0U))
    {
        bus_rt.status.wr_falling_count++;
        bus_rt.status.last_wr_bus =
            bus;
    }
    else if((bus_rt.prev_wr == 0U) && (wr == 1U))
    {
        bus_rt.status.wr_rising_count++;
        bus_rt.status.last_wr_bus =
            bus;
    }
}


static void handle_misc_poll(
    uint8_t datareq,
    uint8_t ioreset,
    uint8_t txe,
    uint8_t rxf)
{
    if((bus_rt.prev_datareq == 1U) && (datareq == 0U))
    {
        bus_rt.status.datareq_falling_count++;
    }
    else if((bus_rt.prev_datareq == 0U) && (datareq == 1U))
    {
        bus_rt.status.datareq_rising_count++;
    }

    if((bus_rt.prev_ioreset == 1U) && (ioreset == 0U))
    {
        bus_rt.status.ioreset_falling_count++;
    }
    else if((bus_rt.prev_ioreset == 0U) && (ioreset == 1U))
    {
        bus_rt.status.ioreset_rising_count++;
    }

    if((bus_rt.prev_txe == 1U) && (txe == 0U))
    {
        bus_rt.txe_falling_count++;
    }
    else if((bus_rt.prev_txe == 0U) && (txe == 1U))
    {
        bus_rt.txe_rising_count++;
    }

    if((bus_rt.prev_rxf == 1U) && (rxf == 0U))
    {
        bus_rt.rxf_falling_count++;
    }
    else if((bus_rt.prev_rxf == 0U) && (rxf == 1U))
    {
        bus_rt.rxf_rising_count++;
    }
}


void m16c_vnc_bus_init(void)
{
    memset(&bus_rt, 0, sizeof(bus_rt));

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /*
     * Default safe state:
     * TXE# HIGH, RXF# HIGH, PA0..PA7 Hi-Z.
     */
    bus_rt.ready_mode =
        0U;

    bus_rt.pacing_active =
        0U;

    bus_rt.byte_presented =
        0U;

    configure_bus_inputs_no_pull();

    configure_control_inputs_no_pull();

    configure_status_outputs();

    m16c_vnc_bus_sample_now();

    bus_rt.prev_rd =
        bus_rt.status.rd;

    bus_rt.prev_wr =
        bus_rt.status.wr;

    bus_rt.prev_datareq =
        bus_rt.status.datareq;

    bus_rt.prev_ioreset =
        bus_rt.status.ioreset;

    bus_rt.prev_txe =
        bus_rt.status.txe;

    bus_rt.prev_rxf =
        bus_rt.status.rxf;

    uart_write_str("[M16C-VNC] paced byte read emulation init\r\n");
    uart_write_str("[M16C-VNC] one byte per RXF# pulse, paced by delay\r\n");
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

    status_set_txe();

    /*
     * Hlavni paced state machine.
     */
    present_next_byte_if_possible();

    bus =
        bus_read_u8();

    rd =
        gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);

    wr =
        gpio_read_pin_u8(M16C_WR_GPIO_Port, M16C_WR_Pin);

    datareq =
        gpio_read_pin_u8(M16C_DATAREQ_GPIO_Port, M16C_DATAREQ_Pin);

    ioreset =
        gpio_read_pin_u8(M16C_IORESET_GPIO_Port, M16C_IORESET_Pin);

    txe =
        gpio_read_pin_u8(M16C_TXE_GPIO_Port, M16C_TXE_Pin);

    rxf =
        gpio_read_pin_u8(M16C_RXF_GPIO_Port, M16C_RXF_Pin);

    /*
     * Polling fallback pro RD#.
     */
    if((bus_rt.prev_rd == 1U) && (rd == 0U))
    {
        bus_rt.status.rd_falling_count++;
        bus_rt.status.last_rd_bus =
            bus;

        bus_rt.rd_poll_count++;
    }
    else if((bus_rt.prev_rd == 0U) && (rd == 1U))
    {
        bus_rt.status.rd_rising_count++;
        bus_rt.rd_poll_count++;

        complete_read_cycle();
    }

    handle_wr_poll(wr, bus);

    handle_misc_poll(datareq, ioreset, txe, rxf);

    bus_rt.prev_rd =
        rd;

    bus_rt.prev_wr =
        wr;

    bus_rt.prev_datareq =
        datareq;

    bus_rt.prev_ioreset =
        ioreset;

    bus_rt.prev_txe =
        txe;

    bus_rt.prev_rxf =
        rxf;

    m16c_vnc_bus_sample_now();
}


void m16c_vnc_bus_rd_irq(void)
{
    uint8_t rd;
    uint8_t bus;

    bus_rt.rd_irq_count++;

    rd =
        gpio_read_pin_u8(M16C_RD_GPIO_Port, M16C_RD_Pin);

    bus =
        bus_read_u8();

    if(rd == 0U)
    {
        bus_rt.status.rd_falling_count++;
        bus_rt.status.last_rd_bus =
            bus;
    }
    else
    {
        bus_rt.status.rd_rising_count++;

        complete_read_cycle();
    }

    bus_rt.prev_rd =
        rd;
}


void m16c_vnc_bus_get_status(
    m16c_vnc_bus_status_t *status)
{
    if(status == NULL)
    {
        return;
    }

    *status =
        bus_rt.status;
}


void m16c_vnc_bus_reset_counters(void)
{
    uint8_t last_wr =
        bus_rt.status.last_wr_bus;

    uint8_t last_rd =
        bus_rt.status.last_rd_bus;

    memset(&bus_rt.status, 0, sizeof(bus_rt.status));

    bus_rt.status.last_wr_bus =
        last_wr;

    bus_rt.status.last_rd_bus =
        last_rd;

    bus_rt.txe_falling_count =
        0U;

    bus_rt.txe_rising_count =
        0U;

    bus_rt.rxf_falling_count =
        0U;

    bus_rt.rxf_rising_count =
        0U;

    bus_rt.tx_bytes_presented_count =
        0U;

    bus_rt.tx_bytes_read_count =
        0U;

    bus_rt.rd_irq_count =
        0U;

    bus_rt.rd_poll_count =
        0U;

    bus_rt.pace_release_count =
        0U;

    bus_rt.pace_hold_count =
        0U;

    m16c_vnc_bus_sample_now();

    bus_rt.prev_rd =
        bus_rt.status.rd;

    bus_rt.prev_wr =
        bus_rt.status.wr;

    bus_rt.prev_datareq =
        bus_rt.status.datareq;

    bus_rt.prev_ioreset =
        bus_rt.status.ioreset;

    bus_rt.prev_txe =
        bus_rt.status.txe;

    bus_rt.prev_rxf =
        bus_rt.status.rxf;
}


void m16c_vnc_bus_set_ready(
    uint8_t ready)
{
    bus_rt.ready_mode =
        ready ? 1U : 0U;

    status_set_txe();

    m16c_vnc_bus_sample_now();

    if(bus_rt.ready_mode)
    {
        uart_write_str("[M16C-VNC] ready mode: TXE#=0\r\n");
    }
    else
    {
        uart_write_str("[M16C-VNC] idle mode: TXE#=1\r\n");
    }
}


uint8_t m16c_vnc_bus_is_ready(void)
{
    return
        bus_rt.ready_mode ? 1U : 0U;
}


void m16c_vnc_bus_clear_tx_fifo(void)
{
    bus_rt.tx_head =
        0U;

    bus_rt.tx_tail =
        0U;

    bus_rt.byte_presented =
        0U;

    bus_rt.pacing_active =
        0U;

    status_set_rxf_high();

    bus_force_hi_z();
}


void m16c_vnc_bus_queue_bytes(
    const uint8_t *data,
    uint32_t len)
{
    uint32_t i;

    if(data == NULL)
    {
        return;
    }

    for(i = 0U; i < len; i++)
    {
        (void)tx_fifo_push(data[i]);
    }

    present_next_byte_if_possible();
}


void m16c_vnc_bus_queue_string(
    const char *text)
{
    if(text == NULL)
    {
        return;
    }

    while(*text != '\0')
    {
        (void)tx_fifo_push((uint8_t)*text);
        text++;
    }

    present_next_byte_if_possible();
}


void m16c_vnc_bus_queue_boot_banner(void)
{
    /*
     * Testovaci minimalni prompt.
     */
    m16c_vnc_bus_queue_string(">\r");
}


void m16c_vnc_bus_queue_pc_attached(void)
{
    m16c_vnc_bus_queue_string("SDA\r");
}


void m16c_vnc_bus_queue_pc_detached(void)
{
    m16c_vnc_bus_queue_string("SDD\r");
}


void m16c_vnc_bus_queue_host_device_attached(void)
{
    m16c_vnc_bus_queue_string("DD2\r");
}


void m16c_vnc_bus_format_status(
    char *out,
    uint32_t out_size)
{
    uint32_t pos =
        0U;

    m16c_vnc_bus_status_t s;

    if((out == NULL) || (out_size == 0U))
    {
        return;
    }

    out[0] =
        '\0';

    m16c_vnc_bus_get_status(&s);

    out_append(out, out_size, &pos, "m16c ");

    out_append(out, out_size, &pos, "mode=");
    out_append(out, out_size, &pos, bus_rt.ready_mode ? "ready" : "idle");

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

    out_append(out, out_size, &pos, " hold=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.pace_hold_count);

    out_append(out, out_size, &pos, " rel=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.pace_release_count);

    out_append(out, out_size, &pos, " ovf=");
    out_append_u32_dec(out, out_size, &pos, bus_rt.tx_fifo_overflow_count);

    out_append(out, out_size, &pos, "\r\n");

    out_append(out, out_size, &pos, "last ");

    out_append(out, out_size, &pos, "wr_bus=");
    out_append_hex8(out, out_size, &pos, s.last_wr_bus);

    out_append(out, out_size, &pos, " rd_bus=");
    out_append_hex8(out, out_size, &pos, s.last_rd_bus);

    out_append(out, out_size, &pos, "\r\n");
}