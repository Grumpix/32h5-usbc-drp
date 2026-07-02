#include "ucpd_diag.h"

#include "main.h"
#include "uart.h"
#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_cortex.h"

#define UCPD_DIAG_EVENT_CC1  (1UL << 0)
#define UCPD_DIAG_EVENT_CC2  (1UL << 1)

static volatile uint32_t ucpd_diag_pending_events = 0U;
static uint8_t ucpd_diag_last_vbus_state = 0U;

static void ucpd_diag_write_vbus_state(uint8_t state)
{
    if (state != 0U)
    {
        uart_write_str("[VBUS] ON\r\n");
    }
    else
    {
        uart_write_str("[VBUS] OFF\r\n");
    }
}

static uint8_t ucpd_diag_read_vbus_state(void)
{
    return LL_GPIO_IsInputPinSet(GPIOC, LL_GPIO_PIN_4) ? 1U : 0U;
}

/* =========================
   init
   ========================= */

void ucpd_diag_init(void)
{
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_UCPD1);

    LL_GPIO_SetPinMode(GPIOC, LL_GPIO_PIN_4, LL_GPIO_MODE_INPUT);
    LL_GPIO_SetPinPull(GPIOC, LL_GPIO_PIN_4, LL_GPIO_PULL_NO);

    ucpd_diag_pending_events = 0U;
    ucpd_diag_last_vbus_state = ucpd_diag_read_vbus_state();

    LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
    LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

    LL_UCPD_EnableIT_TypeCEventCC1(UCPD1);
    LL_UCPD_EnableIT_TypeCEventCC2(UCPD1);

    NVIC_SetPriority(UCPD1_IRQn, 5);
    NVIC_EnableIRQ(UCPD1_IRQn);

    uart_write_str("[UCPD] DIAG INIT\r\n");
    ucpd_diag_write_vbus_state(ucpd_diag_last_vbus_state);
}

/* =========================
   IRQ handler logic
   ========================= */

void ucpd_diag_irq(void)
{
    if (LL_UCPD_IsActiveFlag_TypeCEventCC1(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
        ucpd_diag_pending_events |= UCPD_DIAG_EVENT_CC1;
    }

    if (LL_UCPD_IsActiveFlag_TypeCEventCC2(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);
        ucpd_diag_pending_events |= UCPD_DIAG_EVENT_CC2;
    }
}

void ucpd_diag_task(void)
{
    uint32_t pending_events;
    uint8_t vbus_state;

    __disable_irq();
    pending_events = ucpd_diag_pending_events;
    ucpd_diag_pending_events = 0U;
    __enable_irq();

    if ((pending_events & UCPD_DIAG_EVENT_CC1) != 0U)
    {
        uart_write_str("[UCPD] CC1 event\r\n");
    }

    if ((pending_events & UCPD_DIAG_EVENT_CC2) != 0U)
    {
        uart_write_str("[UCPD] CC2 event\r\n");
    }

    vbus_state = ucpd_diag_read_vbus_state();

    if (vbus_state != ucpd_diag_last_vbus_state)
    {
        ucpd_diag_last_vbus_state = vbus_state;
        ucpd_diag_write_vbus_state(vbus_state);
    }
}