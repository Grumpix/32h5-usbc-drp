#include "uart.h"
#include "stm32h5xx.h"

/* =========================
   USART1 on PA9 (TX only)
   ========================= */

void uart1_init(void)
{
    /* Enable clocks */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 -> AF7 (USART1_TX) */
    GPIOA->MODER &= ~(3U << (9 * 2));
    GPIOA->MODER |=  (2U << (9 * 2));

    GPIOA->AFR[1] &= ~(0xFU << ((9 - 8) * 4));
    GPIOA->AFR[1] |=  (7U << ((9 - 8) * 4));

    GPIOA->OSPEEDR |= (3U << (9 * 2));

    /* Disable USART before config */
    USART1->CR1 = 0;

    /* Baudrate (simple oversimplified version) */
    USART1->BRR = SystemCoreClock / 115200;

    /* Enable TX only */
    USART1->CR1 |= USART_CR1_TE;

    /* Enable USART */
    USART1->CR1 |= USART_CR1_UE;

    /* Wait for ready */
    while (!(USART1->ISR & USART_ISR_TEACK)) {}
}

/* =========================
   TX functions
   ========================= */

void uart1_write_char(char c)
{
    while (!(USART1->ISR & USART_ISR_TXE_TXFNF)) {}
    USART1->TDR = (uint8_t)c;
}

void uart1_write_str(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
            uart1_write_char('\r');

        uart1_write_char(*s++);
    }
}

/* =========================
   simple HEX debug helper
   ========================= */

static const char hex[] = "0123456789ABCDEF";

void uart1_write_hex(uint32_t v)
{
    uart1_write_str("0x");

    for (int i = 7; i >= 0; i--)
    {
        uart1_write_char(hex[(v >> (i * 4)) & 0xF]);
    }
}