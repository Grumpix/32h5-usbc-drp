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
 * 4 = ANAMODE + CCENABLE
 * 5 = ANAMODE + ANASUBMODE_0 + CCENABLE
 * 6 = ANAMODE + ANASUBMODE_1 + CCENABLE
 * 7 = ANAMODE + ANASUBMODE_0 + ANASUBMODE_1 + CCENABLE
 *
 * Pro sink/Rd hledej rezim, kde:
 * - C-C kabel pusti VBUS v obou orientacich
 * - aktivni CC neni 5V/open
 * - aktivni CC neni tvrde 0V
 */
#define UCPD_CR_TEST_MODE  4


/*
 * 1 = vypnout dead-battery Rd pres PWR->UCPDR.
 * To je spravne pro test aktivniho rizeni Rp/Rd pres UCPD.
 *
 * 0 = nechat dead-battery Rd aktivni.
 * Pouzij jen pro srovnavaci test.
 */
#define UCPD_DISABLE_DEAD_BATTERY  1


/*
 * 1 = prepsat CFG3 trim na 8/8/8/8
 * 0 = nechat factory/reset CFG3
 */
#define UCPD_OVERRIDE_CFG3_TRIM  0


#define UCPD_DIAG_EVENT_CC1  (1UL << 0)
#define UCPD_DIAG_EVENT_CC2  (1UL << 1)


static volatile uint32_t ucpd_diag_pending_events = 0U;

static uint8_t last_vbus = 0U;
static uint32_t vbus_last_change_ms = 0U;

static uint32_t gpio_last_check_ms = 0U;

static uint32_t last_gpiob_moder = 0U;
static uint32_t last_gpiob_otyper = 0U;
static uint32_t last_gpiob_ospeedr = 0U;
static uint32_t last_gpiob_pupdr = 0U;
static uint32_t last_gpiob_afrl = 0U;
static uint32_t last_gpiob_afrh = 0U;


static uint8_t ucpd_diag_read_vbus(void)
{
    return
        (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET)
        ? 1U
        : 0U;
}


static void dump_gpiob(void)
{
    uart_write_str("[GPIOB] MODER=");
    uart_write_hex(GPIOB->MODER);

    uart_write_str(" OTYPER=");
    uart_write_hex(GPIOB->OTYPER);

    uart_write_str(" OSPEEDR=");
    uart_write_hex(GPIOB->OSPEEDR);

    uart_write_str(" PUPDR=");
    uart_write_hex(GPIOB->PUPDR);

    uart_write_str(" IDR=");
    uart_write_hex(GPIOB->IDR);

    uart_write_str(" ODR=");
    uart_write_hex(GPIOB->ODR);

    uart_write_str(" AFRL=");
    uart_write_hex(GPIOB->AFR[0]);

    uart_write_str(" AFRH=");
    uart_write_hex(GPIOB->AFR[1]);

    uart_write_str("\r\n");
}


static void remember_gpiob_state(void)
{
    last_gpiob_moder =
        GPIOB->MODER;

    last_gpiob_otyper =
        GPIOB->OTYPER;

    last_gpiob_ospeedr =
        GPIOB->OSPEEDR;

    last_gpiob_pupdr =
        GPIOB->PUPDR;

    last_gpiob_afrl =
        GPIOB->AFR[0];

    last_gpiob_afrh =
        GPIOB->AFR[1];
}


static void check_gpiob_changed(void)
{
    if(
        (GPIOB->MODER   != last_gpiob_moder)   ||
        (GPIOB->OTYPER  != last_gpiob_otyper)  ||
        (GPIOB->OSPEEDR != last_gpiob_ospeedr) ||
        (GPIOB->PUPDR   != last_gpiob_pupdr)   ||
        (GPIOB->AFR[0]  != last_gpiob_afrl)    ||
        (GPIOB->AFR[1]  != last_gpiob_afrh)
    )
    {
        uart_write_str("[GPIOB] CHANGED\r\n");

        dump_gpiob();

        remember_gpiob_state();
    }
}


static void decode_pb13_pb14(void)
{
    uint32_t moder =
        GPIOB->MODER;

    uint32_t pupdr =
        GPIOB->PUPDR;

    uint32_t afrh =
        GPIOB->AFR[1];

    uint32_t pb13_mode =
        (moder >> (13U * 2U)) & 0x3U;

    uint32_t pb14_mode =
        (moder >> (14U * 2U)) & 0x3U;

    uint32_t pb13_pull =
        (pupdr >> (13U * 2U)) & 0x3U;

    uint32_t pb14_pull =
        (pupdr >> (14U * 2U)) & 0x3U;

    uint32_t pb13_af =
        (afrh >> ((13U - 8U) * 4U)) & 0xFU;

    uint32_t pb14_af =
        (afrh >> ((14U - 8U) * 4U)) & 0xFU;

    uart_write_str("[PB13] MODE=");
    uart_write_hex(pb13_mode);

    uart_write_str(" AF=");
    uart_write_hex(pb13_af);

    uart_write_str(" PULL=");
    uart_write_hex(pb13_pull);

    uart_write_str(" IDR=");
    uart_write_hex((GPIOB->IDR & GPIO_PIN_13) ? 1U : 0U);

    uart_write_str("\r\n");


    uart_write_str("[PB14] MODE=");
    uart_write_hex(pb14_mode);

    uart_write_str(" AF=");
    uart_write_hex(pb14_af);

    uart_write_str(" PULL=");
    uart_write_hex(pb14_pull);

    uart_write_str(" IDR=");
    uart_write_hex((GPIOB->IDR & GPIO_PIN_14) ? 1U : 0U);

    uart_write_str("\r\n");


    /*
     * For UCPD CC analog front-end:
     * MODE = 3 -> analog
     * PULL = 0 -> no GPIO pull
     */

    if(pb13_mode != 3U)
    {
        uart_write_str("[WARN] PB13 is NOT analog mode\r\n");
    }

    if(pb14_mode != 3U)
    {
        uart_write_str("[WARN] PB14 is NOT analog mode\r\n");
    }

    if(pb13_pull != 0U)
    {
        uart_write_str("[WARN] PB13 internal pull is NOT none\r\n");
    }

    if(pb14_pull != 0U)
    {
        uart_write_str("[WARN] PB14 internal pull is NOT none\r\n");
    }
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

    uart_write_str(" PWR_UCPDR=");
    uart_write_hex(PWR->UCPDR);

    uart_write_str("\r\n");
}


static void ucpd_disable_dead_battery_if_needed(void)
{
#if UCPD_DISABLE_DEAD_BATTERY
    uart_write_str("[UCPD] Disable dead-battery Rd\r\n");

    PWR->UCPDR |=
        PWR_UCPDR_UCPD_DBDIS;

    uart_write_str("[PWR] UCPDR=");
    uart_write_hex(PWR->UCPDR);
    uart_write_str("\r\n");
#else
    uart_write_str("[UCPD] Dead-battery Rd left enabled\r\n");

    uart_write_str("[PWR] UCPDR=");
    uart_write_hex(PWR->UCPDR);
    uart_write_str("\r\n");
#endif
}


static void ucpd_print_test_mode(uint32_t cr)
{
    uart_write_str("[UCPD] CR TEST MODE ");
    uart_write_hex(UCPD_CR_TEST_MODE);
    uart_write_str(" CR=");
    uart_write_hex(cr);
    uart_write_str(" : ");

#if UCPD_CR_TEST_MODE == 0
    uart_write_str("CCENABLE only\r\n");
#elif UCPD_CR_TEST_MODE == 1
    uart_write_str("ANASUBMODE_0 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 2
    uart_write_str("ANASUBMODE_1 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 3
    uart_write_str("ANASUBMODE_0 + ANASUBMODE_1 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 4
    uart_write_str("ANAMODE + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 5
    uart_write_str("ANAMODE + ANASUBMODE_0 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 6
    uart_write_str("ANAMODE + ANASUBMODE_1 + CCENABLE\r\n");
#elif UCPD_CR_TEST_MODE == 7
    uart_write_str("ANAMODE + ANASUBMODE_0 + ANASUBMODE_1 + CCENABLE\r\n");
#else
    uart_write_str("UNKNOWN\r\n");
#endif
}


static uint32_t ucpd_make_test_cr(void)
{
    uint32_t cr =
          UCPD_CR_CCENABLE_0
        | UCPD_CR_CCENABLE_1;

#if UCPD_CR_TEST_MODE == 0

    /*
     * CCENABLE only.
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

#elif UCPD_CR_TEST_MODE == 4

    cr |=
        UCPD_CR_ANAMODE;

#elif UCPD_CR_TEST_MODE == 5

    cr |=
          UCPD_CR_ANAMODE
        | UCPD_CR_ANASUBMODE_0;

#elif UCPD_CR_TEST_MODE == 6

    cr |=
          UCPD_CR_ANAMODE
        | UCPD_CR_ANASUBMODE_1;

#elif UCPD_CR_TEST_MODE == 7

    cr |=
          UCPD_CR_ANAMODE
        | UCPD_CR_ANASUBMODE_0
        | UCPD_CR_ANASUBMODE_1;

#else
#error "Invalid UCPD_CR_TEST_MODE"
#endif

    return cr;
}


static void ucpd_hw_init(void)
{
    uint32_t cr;


    /*
     * Timing config.
     */

    UCPD1->CFG1 =
          (5U  << UCPD_CFG1_HBITCLKDIV_Pos)
        | (17U << UCPD_CFG1_IFRGAP_Pos)
        | (15U << UCPD_CFG1_TRANSWIN_Pos)
        | (0U  << UCPD_CFG1_PSC_UCPDCLK_Pos);


    /*
     * FORCECLK for stable diagnostics.
     */

    UCPD1->CFG2 =
        UCPD_CFG2_FORCECLK;


#if UCPD_OVERRIDE_CFG3_TRIM
    UCPD1->CFG3 =
          (8U << UCPD_CFG3_TRIM_CC1_RD_Pos)
        | (8U << UCPD_CFG3_TRIM_CC2_RD_Pos)
        | (8U << UCPD_CFG3_TRIM_CC1_RP_Pos)
        | (8U << UCPD_CFG3_TRIM_CC2_RP_Pos);
#endif


    /*
     * Enable UCPD peripheral.
     */

    UCPD1->CFG1 |=
        UCPD_CFG1_UCPDEN;


    /*
     * Clean control register.
     */

    UCPD1->CR = 0U;


    /*
     * Configure tested analog mode.
     *
     * Zamerne zatim NE:
     * - PHYRXEN
     * - RDCH
     * - LL_UCPD_RxEnable()
     */

    cr =
        ucpd_make_test_cr();

    UCPD1->CR =
        cr;

    /*
     * Small settle delay before reading CC status.
     */

    for(volatile uint32_t i = 0; i < 1000U; i++)
    {
        __NOP();
    }

    ucpd_print_test_mode(cr);
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


    /*
     * Dead-battery setting must be done before active UCPD CC control test.
     */

    ucpd_disable_dead_battery_if_needed();


    LL_APB1_GRP2_EnableClock(
        LL_APB1_GRP2_PERIPH_UCPD1);


    LL_APB1_GRP2_ForceReset(
        LL_APB1_GRP2_PERIPH_UCPD1);

    for(volatile uint32_t i = 0; i < 100U; i++)
    {
        __NOP();
    }

    LL_APB1_GRP2_ReleaseReset(
        LL_APB1_GRP2_PERIPH_UCPD1);


    /*
     * VBUS sense PC4.
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
     *
     * PB13 = UCPD1_CC1
     * PB14 = UCPD1_CC2
     *
     * For UCPD CC analog front-end, keep both pins in analog mode.
     */

    LL_GPIO_SetPinMode(
        GPIOB,
        LL_GPIO_PIN_13,
        LL_GPIO_MODE_ANALOG);

    LL_GPIO_SetPinMode(
        GPIOB,
        LL_GPIO_PIN_14,
        LL_GPIO_MODE_ANALOG);


    LL_GPIO_SetPinPull(
        GPIOB,
        LL_GPIO_PIN_13,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinPull(
        GPIOB,
        LL_GPIO_PIN_14,
        LL_GPIO_PULL_NO);


    /*
     * AFRH AF6 is kept only for visibility/consistency.
     * In analog mode, AF mux is not used.
     */

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_13,
        LL_GPIO_AF_6);

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_14,
        LL_GPIO_AF_6);


    uart_write_str("[GPIOB] AFTER PIN CONFIG\r\n");
    dump_gpiob();
    decode_pb13_pb14();


    ucpd_hw_init();


    uart_write_str("[GPIOB] AFTER UCPD INIT\r\n");
    dump_gpiob();
    decode_pb13_pb14();


    /*
     * Type-C event detector only.
     * PD RX stays disabled for this analog test.
     */

    LL_UCPD_TypeCDetectionCC1Enable(UCPD1);
    LL_UCPD_TypeCDetectionCC2Enable(UCPD1);

    /*
     * Intentionally disabled:
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

    gpio_last_check_ms =
        HAL_GetTick();

    remember_gpiob_state();


    uart_write_str("===== UCPD READY ANALOG MODE TEST =====\r\n");
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


    if((now - gpio_last_check_ms) > 1000U)
    {
        gpio_last_check_ms =
            now;

        check_gpiob_changed();
    }


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

            uart_write_str("[GPIOB] ON VBUS CHANGE\r\n");
            dump_gpiob();
            decode_pb13_pb14();
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

        uart_write_str("[GPIOB] ON CC EVENT\r\n");
        dump_gpiob();
        decode_pb13_pb14();
    }
}