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
   UCPD LOW LEVEL INIT
   ========================= */

static void ucpd_hw_init(void)
{
    LL_UCPD_InitTypeDef UCPD_InitStruct;

    LL_UCPD_StructInit(&UCPD_InitStruct);


    UCPD_InitStruct.psc_ucpdclk = LL_UCPD_PSC_DIV1;
    UCPD_InitStruct.transwin = 15;
    UCPD_InitStruct.IfrGap = 17;
    UCPD_InitStruct.HbitClockDiv = 5;


    LL_UCPD_Init(UCPD1, &UCPD_InitStruct);


    uart_write_str("[UCPD] AFTER LL_INIT CR=");
    uart_write_hex(UCPD1->CR);

    uart_write_str(" CFG1=");
    uart_write_hex(UCPD1->CFG1);

    uart_write_str("\r\n");
}



/* =========================
   GETTER
   ========================= */

uint8_t ucpd_diag_is_source(void)
{
    return ucpd_is_source;
}



/* =========================
   VBUS
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

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOC);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOB);


    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_UCPD1);



    /*
     * VBUS sense
     */

    LL_GPIO_SetPinMode(GPIOC,
                       LL_GPIO_PIN_4,
                       LL_GPIO_MODE_INPUT);

    LL_GPIO_SetPinPull(GPIOC,
                       LL_GPIO_PIN_4,
                       LL_GPIO_PULL_NO);



    /*
     * CC pins
     */

    LL_GPIO_SetPinMode(GPIOB,
                       LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
                       LL_GPIO_MODE_ALTERNATE);

    LL_GPIO_SetPinPull(GPIOB,
                       LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
                       LL_GPIO_PULL_NO);



    /*
     * UCPD init
     */

    LL_UCPD_Disable(UCPD1);


    ucpd_hw_init();



    /*
     * Manual CR setup
     *
     * LL macros on this H5 driver
     * write incorrect value.
     */


    /*
     * Sink role
     *
     * ANAMODE = 1 means sink on STM32H5
     */

    SET_BIT(UCPD1->CR, UCPD_CR_ANAMODE);


    uart_write_str("[UCPD] AFTER SNK CR=");
    uart_write_hex(UCPD1->CR);
    uart_write_str("\r\n");



    /*
     * Enable CC1 + CC2
     */

    SET_BIT(UCPD1->CR, UCPD_CR_CCENABLE);


    uart_write_str("[UCPD] AFTER CCENABLE CR=");
    uart_write_hex(UCPD1->CR);
    uart_write_str("\r\n");



    /*
     * Type-C detection
     */

    LL_UCPD_TypeCDetectionCC1Enable(UCPD1);
    LL_UCPD_TypeCDetectionCC2Enable(UCPD1);



    /*
     * RX
     */

    LL_UCPD_RxEnable(UCPD1);



    /*
     * Enable peripheral
     */

    LL_UCPD_Enable(UCPD1);



    /*
     * Clear events
     */

    LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
    LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);



    /*
     * Interrupts
     */

    LL_UCPD_EnableIT_TypeCEventCC1(UCPD1);
    LL_UCPD_EnableIT_TypeCEventCC2(UCPD1);



    ucpd_diag_pending_events = 0U;

    last_vbus = ucpd_diag_read_vbus();

    stable_role = 0xFF;

    vbus_last_change_ms = HAL_GetTick();



    NVIC_SetPriority(UCPD1_IRQn, 5);
    NVIC_EnableIRQ(UCPD1_IRQn);



    uart_write_str("[UCPD] CR=");
    uart_write_hex(UCPD1->CR);

    uart_write_str(" CFG1=");
    uart_write_hex(UCPD1->CFG1);

    uart_write_str(" SR=");
    uart_write_hex(UCPD1->SR);

    uart_write_str(" IMR=");
    uart_write_hex(UCPD1->IMR);

    uart_write_str("\r\n");


    uart_write_str("[UCPD] CC1=");
    uart_write_hex(LL_UCPD_GetTypeCVstateCC1(UCPD1));

    uart_write_str(" CC2=");
    uart_write_hex(LL_UCPD_GetTypeCVstateCC2(UCPD1));

    uart_write_str("\r\n");


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



    vbus = ucpd_diag_read_vbus();



    if (vbus != last_vbus)
    {
        if ((now - vbus_last_change_ms) > 50U)
        {
            vbus_last_change_ms = now;

            last_vbus = vbus;


            uart_write_str(
                vbus ?
                "[VBUS] ON\r\n" :
                "[VBUS] OFF\r\n"
            );
        }
    }



    /*
     * Diagnostic only
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