#include "ucpd_diag.h"
#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_cortex.h"

/* =========================
   external UART logger
   ========================= */

extern void uart1_write_str(const char *s);

/* =========================
   init
   ========================= */

void ucpd_diag_init(void)
{
    /* enable clock */
    LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_UCPD1);

    /* enable interrupts (TypeC CC events) */
    LL_UCPD_EnableIT_TypeCEventCC1(UCPD1);
    LL_UCPD_EnableIT_TypeCEventCC2(UCPD1);

    /* enable NVIC */
    NVIC_SetPriority(UCPD1_IRQn, 5);
    NVIC_EnableIRQ(UCPD1_IRQn);

    uart1_write_str("UCPD DIAG INIT\r\n");
}

/* =========================
   IRQ handler logic
   ========================= */

void ucpd_diag_irq(void)
{
    /* CC1 event */
    if (LL_UCPD_IsActiveFlag_TypeCEventCC1(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
        uart1_write_str("CC1 event\r\n");
    }

    /* CC2 event */
    if (LL_UCPD_IsActiveFlag_TypeCEventCC2(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);
        uart1_write_str("CC2 event\r\n");
    }
}

/* =========================
   attach / detach
   ========================= */

void ucpd_diag_on_attach(void)
{
    uart1_write_str("[UCPD] ATTACH\r\n");
}

void ucpd_diag_on_detach(void)
{
    uart1_write_str("[UCPD] DETACH\r\n");
}

/* =========================
   role
   ========================= */

void ucpd_diag_role(uint8_t is_source)
{
    if (is_source)
        uart1_write_str("[UCPD] ROLE: SOURCE (HOST)\r\n");
    else
        uart1_write_str("[UCPD] ROLE: SINK (DEVICE)\r\n");
}

/* =========================
   VBUS state
   ========================= */

void ucpd_diag_vbus(uint8_t state)
{
    if (state)
        uart1_write_str("[VBUS] ON\r\n");
    else
        uart1_write_str("[VBUS] OFF\r\n");
}