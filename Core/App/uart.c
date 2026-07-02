#include "uart.h"

#include "stm32h533xx.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"
#include "stm32h5xx_ll_rcc.h"

#define UART_BAUDRATE 115200U
#define UART_TX_PIN   LL_GPIO_PIN_9

static uint32_t uart_get_clock_hz(void)
{
    uint32_t apb2_divider = 1U;

    switch (LL_RCC_GetAPB2Prescaler())
    {
        case LL_RCC_APB2_DIV_2:
            apb2_divider = 2U;
            break;

        case LL_RCC_APB2_DIV_4:
            apb2_divider = 4U;
            break;

        case LL_RCC_APB2_DIV_8:
            apb2_divider = 8U;
            break;

        case LL_RCC_APB2_DIV_16:
            apb2_divider = 16U;
            break;

        case LL_RCC_APB2_DIV_1:
        default:
            apb2_divider = 1U;
            break;
    }

    return SystemCoreClock / apb2_divider;
}

void uart_init(void)
{
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
    LL_RCC_SetUSARTClockSource(LL_RCC_USART1_CLKSOURCE_PCLK2);

    LL_GPIO_SetPinMode(GPIOA, UART_TX_PIN, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinOutputType(GPIOA, UART_TX_PIN, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(GPIOA, UART_TX_PIN, LL_GPIO_PULL_NO);
    LL_GPIO_SetPinSpeed(GPIOA, UART_TX_PIN, LL_GPIO_SPEED_FREQ_VERY_HIGH);
    LL_GPIO_SetAFPin_8_15(GPIOA, UART_TX_PIN, LL_GPIO_AF_7);

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    USART1->PRESC = 0U;
    USART1->BRR = (uart_get_clock_hz() + (UART_BAUDRATE / 2U)) / UART_BAUDRATE;
    USART1->CR1 = USART_CR1_TE;
    USART1->CR1 |= USART_CR1_UE;

    while ((USART1->ISR & USART_ISR_TEACK) == 0U)
    {
    }
}

void uart_write_char(char ch)
{
    while ((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U)
    {
    }

    USART1->TDR = (uint8_t) ch;
}

void uart_write_str(const char *text)
{
    if (text == 0)
    {
        return;
    }

    while (*text != '\0')
    {
        uart_write_char(*text++);
    }
}

void uart_write_hex(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    uart_write_str("0x");

    for (uint32_t digit = 0; digit < 8U; ++digit)
    {
        uint32_t shift = 28U - (digit * 4U);
        uart_write_char(hex[(value >> shift) & 0x0FU]);
    }
}