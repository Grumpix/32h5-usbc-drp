#include "ucpd_diag.h"

#include "main.h"
#include "uart.h"
#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

#include <stdbool.h>


/* =========================
   EVENTS
   ========================= */

#define UCPD_DIAG_EVENT_CC1  (1UL << 0)
#define UCPD_DIAG_EVENT_CC2  (1UL << 1)


static volatile uint32_t ucpd_diag_pending_events = 0U;


/* =========================
   STATE
   ========================= */

static uint8_t ucpd_is_source = 0U;

static uint8_t last_vbus = 0U;
static uint8_t stable_role = 0xFF;
static uint32_t vbus_last_change_ms = 0U;


/* =========================
   PUBLIC GETTER
   ========================= */

uint8_t ucpd_diag_is_source(void)
{
    return ucpd_is_source;
}


/* =========================
   VBUS READ
   ========================= */

static uint8_t ucpd_diag_read_vbus(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET)
           ? 1U
           : 0U;
}


/* =========================
   INIT
   ========================= */

void ucpd_diag_init(void)
{
    /* clocks */

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);

    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_UCPD1);


    /*
     * UCPD pins
     *
     * CC1 = PB13
     * CC2 = PB14
     *
     * AF depends on STM32H533 mapping
     */

    LL_GPIO_SetPinMode(GPIOB,
                       LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
                       LL_GPIO_MODE_ALTERNATE);

    LL_GPIO_SetPinPull(GPIOB,
                       LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
                       LL_GPIO_PULL_NO);


    /*
     * VBUS sense
     *
     * Dev board:
     * PC4
     */

    LL_GPIO_SetPinMode(GPIOC,
                       LL_GPIO_PIN_4,
                       LL_GPIO_MODE_INPUT);

    LL_GPIO_SetPinPull(GPIOC,
                       LL_GPIO_PIN_4,
                       LL_GPIO_PULL_NO);


    /*
     * UCPD configuration
     *
     * Start as Sink
     */

    LL_UCPD_SetSNKRole(UCPD1);


    /*
     * Enable CC detection
     */

    LL_UCPD_SetccEnable(
        UCPD1,
        LL_UCPD_CCENABLE_CC1CC2
    );


    /*
     * Select CC1 as default monitor
     */

    LL_UCPD_SetCCPin(
        UCPD1,
        LL_UCPD_CCPIN_CC1
    );


    /*
     * Enable receiver
     */

    LL_UCPD_RxEnable(UCPD1);


    /*
     * Interrupts
     */

    ucpd_diag_pending_events = 0U;

    last_vbus = ucpd_diag_read_vbus();
    stable_role = 0xFF;
    vbus_last_change_ms = HAL_GetTick();


    LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
    LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

    LL_UCPD_EnableIT_TypeCEventCC1(UCPD1);
    LL_UCPD_EnableIT_TypeCEventCC2(UCPD1);


    NVIC_SetPriority(UCPD1_IRQn, 5);
    NVIC_EnableIRQ(UCPD1_IRQn);


    uart_write_str("[UCPD] DIAG INIT\r\n");
}


/* =========================
   IRQ
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


/* =========================
   TASK
   ========================= */

void ucpd_diag_task(void)
{
    uint32_t events;
    uint8_t vbus;
    uint32_t now = HAL_GetTick();


    __disable_irq();

    events = ucpd_diag_pending_events;
    ucpd_diag_pending_events = 0U;

    __enable_irq();



    if (events & UCPD_DIAG_EVENT_CC1)
    {
        uart_write_str("[UCPD] CC1 event\r\n");
    }


    if (events & UCPD_DIAG_EVENT_CC2)
    {
        uart_write_str("[UCPD] CC2 event\r\n");
    }



    /*
     * VBUS debounce
     */

    vbus = ucpd_diag_read_vbus();


    if (vbus != last_vbus)
    {
        if ((now - vbus_last_change_ms) > 50U)
        {
            vbus_last_change_ms = now;
            last_vbus = vbus;

            uart_write_str(
                vbus ? "[VBUS] ON\r\n"
                     : "[VBUS] OFF\r\n"
            );
        }
    }



    /*
     * Temporary role indication
     *
     * Real role will later come from CC state.
     */

    if (last_vbus)
    {
        if (stable_role != 0U)
        {
            stable_role = 0U;
            ucpd_is_source = 0U;

            uart_write_str("[UCPD] ROLE: DEVICE\r\n");
        }
    }
    else
    {
        if (stable_role != 1U)
        {
            stable_role = 1U;
            ucpd_is_source = 1U;

            uart_write_str("[UCPD] ROLE: HOST\r\n");
        }
    }
}