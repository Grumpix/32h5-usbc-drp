#include "uart_log.h"

#include "main.h"
#include "stm32h5xx_hal_gpio_ex.h"

#define UART_LOG_BAUDRATE 115200U

static uint8_t uart_log_ready = 0;

static void uart_log_write_char(char ch)
{
    uint32_t timeout = 200000U;

    if (uart_log_ready == 0U)
    {
        return;
    }

    while (((USART1->ISR & USART_ISR_TXE_TXFNF) == 0U) && (timeout > 0U))
    {
        --timeout;
    }

    if (timeout == 0U)
    {
        return;
    }

    USART1->TDR = (uint32_t) (uint8_t) ch;
}

void uart_log_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART1_FORCE_RESET();
    __HAL_RCC_USART1_RELEASE_RESET();

    gpio_init.Pin = UART_LOG_TX_Pin;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio_init.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(UART_LOG_TX_GPIO_Port, &gpio_init);

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;
    USART1->PRESC = 0U;
    USART1->BRR = (uint32_t) ((HAL_RCC_GetPCLK2Freq() + (UART_LOG_BAUDRATE / 2U)) / UART_LOG_BAUDRATE);
    USART1->ICR = 0xFFFFFFFFU;
    USART1->CR1 = USART_CR1_TE;
    USART1->CR1 |= USART_CR1_UE;

    uart_log_ready = 1U;
}

void uart_log_write(char const *msg)
{
    if (msg == NULL)
    {
        return;
    }

    while (*msg != '\0')
    {
        uart_log_write_char(*msg++);
    }
}

void uart_log_write_u32(uint32_t value)
{
    char buffer[11];
    uint32_t index = 10U;

    buffer[index] = '\0';

    do
    {
        --index;
        buffer[index] = (char) ('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);

    uart_log_write(&buffer[index]);
}