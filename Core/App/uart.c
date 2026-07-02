#include "uart.h"

#include "stm32h533xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"

#define UART_BAUDRATE 115200U
#define UART_TX_PIN   LL_GPIO_PIN_9

/* =========================
   CLOCK helper (PCLK2 safe)
   ========================= */
static uint32_t uart_get_pclk2_hz(void)
{
    uint32_t presc = LL_RCC_GetAPB2Prescaler();

    uint32_t divider = 1U;

    switch (presc)
    {
        case LL_RCC_APB2_DIV_2:  divider = 2U;  break;
        case LL_RCC_APB2_DIV_4:  divider = 4U;  break;
        case LL_RCC_APB2_DIV_8:  divider = 8U;  break;
        case LL_RCC_APB2_DIV_16: divider = 16U; break;
        default:                 divider = 1U;  break;
    }

    return (SystemCoreClock / divider);
}

/* =========================
   INIT
   ========================= */
void uart_init(void)
{
    /* clocks */
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);

    /* IMPORTANT: force safe GPIO state first */
    LL_GPIO_SetPinMode(GPIOA, UART_TX_PIN, LL_GPIO_MODE_ANALOG);
    LL_GPIO_SetPinPull(GPIOA, UART_TX_PIN, LL_GPIO_PULL_NO);

    /* AF7 for USART1 TX */
    LL_GPIO_SetPinMode(GPIOA, UART_TX_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA, UART_TX_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinSpeed(GPIOA, UART_TX_PIN, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetAFPin_8_15(GPIOA, UART_TX_PIN, LL_GPIO_AF_7);

    /* reset USART */
    USART1->CR1 = 0;
    USART1->CR2 = 0;
    USART1->CR3 = 0;
    USART1->PRESC = 0;

    /* NOTE: intentionally using PCLK2 directly */
    uint32_t clk = uart_get_pclk2_hz();

    USART1->BRR = (clk + (UART_BAUDRATE / 2U)) / UART_BAUDRATE;

    /* enable TX */
    USART1->CR1 = USART_CR1_TE;

    /* enable USART */
    USART1->CR1 |= USART_CR1_UE;

    /* wait for TX ready */
    while ((USART1->ISR & USART_ISR_TEACK) == 0U) {}
}

/* =========================
   TX
   ========================= */
void uart_write_char(char ch)
{
    while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U) {}

    USART1->TDR = (uint8_t)ch;
}

void uart_write_str(const char *text)
{
    if (!text) return;

    while (*text)
    {
        if (*text == '\n')
            uart_write_char('\r');

        uart_write_char(*text++);
    }
}

void uart_write_hex(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    uart_write_str("0x");

    for (int i = 7; i >= 0; i--)
    {
        uart_write_char(hex[(value >> (i * 4)) & 0xF]);
    }
}