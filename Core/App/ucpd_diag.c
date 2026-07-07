#include "ucpd_diag.h"

#include "main.h"
#include "uart.h"

#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"


/*
 * UCPD_CR_TEST_MODE:
 *
 * 0 = CCENABLE only
 * 1 = ANASUBMODE_0 + CCENABLE
 * 2 = ANASUBMODE_1 + CCENABLE
 * 3 = ANASUBMODE_0 + ANASUBMODE_1 + CCENABLE
 *
 * DULEZITE:
 * - bez ANAMODE
 * - bez PHYRXEN
 * - bez RDCH
 * - bez LL_UCPD_RxEnable()
 */
#define UCPD_CR_TEST_MODE  3


#define UCPD_DIAG_EVENT_CC1  (1UL << 0)
#define UCPD_DIAG_EVENT_CC2  (1UL << 1)


static volatile uint32_t ucpd_diag_pending_events = 0U;

static uint8_t last_vbus = 0U;
static uint32_t vbus_last_change_ms = 0U;


static uint8_t ucpd_diag_read_vbus(void)
{
    return
        (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET)
        ? 1U
        : 0U;
}


static void ucpd_dump_state(void)
{
    uart_write_str("[UCPD] CFG1=");
    uart_write_hex(UCPD1->CFG1);

    uart_write_str(" CFG2=");
    uart_write_hex(UCPD1->CFG2);

    uart_write_str(" CFG3=");
    uart_write_hex(UCPD1->CFG3);

    uart_write_str(" CR=");
    uart_write_hex(UCPD1->CR);

    uart_write_str(" SR=");
    uart_write_hex(UCPD1->SR);

    uart_write_str(" CC1=");
    uart_write_hex(LL_UCPD_GetTypeCVstateCC1(UCPD1));

    uart_write_str(" CC2=");
    uart_write_hex(LL_UCPD_GetTypeCVstateCC2(UCPD1));

    uart_write_str("\r\n");
}


static void ucpd_print_test_mode(void)
{
    uart_write_str("[UCPD] CR TEST MODE ");

#if UCPD_CR_TEST_MODE == 0
    uart_write_str("0: CCENABLE only\r\n");
#elif UCPD_CR_TEST_MODE == 1
    uart_write_str("1: ANASUBMODE_0 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 2
    uart_write_str("2: ANASUBMODE_1 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 3
    uart_write_str("3: ANASUBMODE_0 + ANASUBMODE_1 + CCENABLE\r\n");
#else
    uart_write_str("UNKNOWN\r\n");
#endif
}


static void ucpd_hw_init(void)
{
    uint32_t cr = 0U;


    /*
     * Timing config
     */

    UCPD1->CFG1 =
          (5U  << UCPD_CFG1_HBITCLKDIV_Pos)
        | (17U << UCPD_CFG1_IFRGAP_Pos)
        | (15U << UCPD_CFG1_TRANSWIN_Pos)
        | (0U  << UCPD_CFG1_PSC_UCPDCLK_Pos);


    /*
     * FORCECLK nechame zapnuty kvuli stabilnimu debug mereni.
     */

    UCPD1->CFG2 =
        UCPD_CFG2_FORCECLK;


    /*
     * Pro tento test zatim zachovame trim hodnoty jako predtim.
     * Pokud se bude chovat divne, dalsi krok bude CFG3 vubec neprepisovat.
     */

    //PD1->CFG3 =
      //  (8U << UCPD_CFG3_TRIM_CC1_RD_Pos)
      //| (8U << UCPD_CFG3_TRIM_CC2_RD_Pos)
      //| (8U << UCPD_CFG3_TRIM_CC1_RP_Pos)
      //| (8U << UCPD_CFG3_TRIM_CC2_RP_Pos);


    /*
     * Enable UCPD peripheral.
     * Pozor: UCPDEN je v CFG1, ne v CR.
     */

    UCPD1->CFG1 |=
        UCPD_CFG1_UCPDEN;


    /*
     * Vymaz CR pred testem.
     */

    UCPD1->CR = 0U;


    /*
     * Zaklad testu:
     * CCENABLE_0 + CCENABLE_1.
     *
     * Zamerne NE:
     * - ANAMODE
     * - PHYRXEN
     * - RDCH
     */

    cr =
          UCPD_CR_CCENABLE_0
        | UCPD_CR_CCENABLE_1;


#if UCPD_CR_TEST_MODE == 0

    /*
     * CCENABLE only
     */

#elif UCPD_CR_TEST_MODE == 1

    cr |=
        UCPD_CR_ANASUBMODE_0;

#elif UCPD_CR_TEST_MODE == 2

    cr |=
        UCPD_CR_ANASUBMODE_1;

#elif UCPD_CR_TEST_MODE == 3

    cr |=
          UCPD_CR_ANASUBMODE_0
        | UCPD_CR_ANASUBMODE_1;

#else
#error "Invalid UCPD_CR_TEST_MODE"
#endif


    UCPD1->CR =
        cr;


    ucpd_print_test_mode();
    ucpd_dump_state();
}


uint8_t ucpd_diag_is_source(void)
{
    return 0U;
}


void ucpd_diag_init(void)
{
    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOC);

    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOB);


    HAL_PWREx_EnableVddUSB();


    LL_APB1_GRP2_EnableClock(
        LL_APB1_GRP2_PERIPH_UCPD1);


    LL_APB1_GRP2_ForceReset(
        LL_APB1_GRP2_PERIPH_UCPD1);

    for(volatile uint32_t i = 0; i < 100; i++)
    {
        __NOP();
    }

    LL_APB1_GRP2_ReleaseReset(
        LL_APB1_GRP2_PERIPH_UCPD1);


    /*
     * VBUS sense PC4
     */

    LL_GPIO_SetPinMode(
        GPIOC,
        LL_GPIO_PIN_4,
        LL_GPIO_MODE_INPUT);

    LL_GPIO_SetPinPull(
        GPIOC,
        LL_GPIO_PIN_4,
        LL_GPIO_PULL_NO);


    /*
     * CC pins:
     * PB13 = UCPD1_CC1
     * PB14 = UCPD1_CC2
     */

    LL_GPIO_SetPinMode(
        GPIOB,
        LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
        LL_GPIO_MODE_ALTERNATE);

    LL_GPIO_SetPinPull(
        GPIOB,
        LL_GPIO_PIN_13 | LL_GPIO_PIN_14,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_13,
        LL_GPIO_AF_6);

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_14,
        LL_GPIO_AF_6);


    ucpd_hw_init();


    /*
     * Type-C event detector.
     * PD RX zatim nezapiname.
     */

    LL_UCPD_TypeCDetectionCC1Enable(UCPD1);
    LL_UCPD_TypeCDetectionCC2Enable(UCPD1);

    /*
     * DULEZITE:
     * Tohle je zamerne vypnute:
     *
     * LL_UCPD_RxEnable(UCPD1);
     */


    LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
    LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

    LL_UCPD_EnableIT_TypeCEventCC1(UCPD1);
    LL_UCPD_EnableIT_TypeCEventCC2(UCPD1);


    NVIC_SetPriority(
        UCPD1_IRQn,
        5);

    NVIC_EnableIRQ(
        UCPD1_IRQn);


    last_vbus =
        ucpd_diag_read_vbus();

    vbus_last_change_ms =
        HAL_GetTick();


    uart_write_str("===== UCPD READY CR TEST =====\r\n");
    ucpd_dump_state();
}


void ucpd_diag_irq(void)
{
    if(LL_UCPD_IsActiveFlag_TypeCEventCC1(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);

        ucpd_diag_pending_events |=
            UCPD_DIAG_EVENT_CC1;
    }

    if(LL_UCPD_IsActiveFlag_TypeCEventCC2(UCPD1))
    {
        LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

        ucpd_diag_pending_events |=
            UCPD_DIAG_EVENT_CC2;
    }
}


void ucpd_diag_task(void)
{
    uint32_t now =
        HAL_GetTick();

    uint8_t vbus =
        ucpd_diag_read_vbus();


    if(vbus != last_vbus)
    {
        if((now - vbus_last_change_ms) > 50U)
        {
            last_vbus =
                vbus;

            vbus_last_change_ms =
                now;

            uart_write_str(
                vbus ?
                "[VBUS] PRESENT\r\n" :
                "[VBUS] LOST\r\n");

            ucpd_dump_state();
        }
    }


    if(ucpd_diag_pending_events)
    {
        uint32_t ev;

        __disable_irq();

        ev =
            ucpd_diag_pending_events;

        ucpd_diag_pending_events = 0U;

        __enable_irq();


        if(ev & UCPD_DIAG_EVENT_CC1)
        {
            uart_write_str("[UCPD] CC1 EVENT\r\n");
        }

        if(ev & UCPD_DIAG_EVENT_CC2)
        {
            uart_write_str("[UCPD] CC2 EVENT\r\n");
        }

        ucpd_dump_state();
    }
}