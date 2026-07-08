#include "ucpd_diag.h"

#include "main.h"
#include "uart.h"
#include "usb_manager.h"

#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

#include <stdint.h>


/*
 * USB-C SOURCE / HOST BRING-UP TEST
 *
 * Funkcni source/Rp rezim:
 *
 * CR = 0x00000C80
 *    = UCPD_CR_ANASUBMODE_0
 *    | UCPD_CR_CCENABLE_0
 *    | UCPD_CR_CCENABLE_1
 *
 * Chovani:
 *
 * - STM32 vystavi Rp jako USB-C Source
 * - ceka na Rd od pripojeneho Sink/Device
 * - detekuje orientaci CC1/CC2
 * - zapne VBUS pres PB8 P-MOS gate
 * - ceka na VBUS PRESENT z PC4
 * - po 200 ms spusti TinyUSB Host pres usb_manager_start_host()
 *
 * Detach:
 *
 * - vypne VBUS
 * - host stack zatim explicitne nezastavujeme, protoze ted testujeme bring-up
 *   a nechceme volat API, ktere v projektu nemusi existovat.
 */


/*
 * VBUS P-MOS gate control.
 *
 * Zapojeni:
 *
 * Source P-MOS = +5 V
 * Drain P-MOS  = USB-C VBUS
 * Gate P-MOS   = PB8 + 100k pull-up na Source/+5 V
 *
 * OFF:
 *   PB8 = Hi-Z/input
 *   gate vytazena na +5 V
 *
 * ON:
 *   PB8 = output LOW
 *   gate stazena na GND
 */

#define VBUS_FET_PORT                 GPIOB
#define VBUS_FET_PIN                  GPIO_PIN_8

#define VBUS_FET_LL_PORT              GPIOB
#define VBUS_FET_LL_PIN               LL_GPIO_PIN_8


/*
 * CC pins.
 */

#define UCPD_CC_PORT                  GPIOB
#define UCPD_CC1_PIN                  LL_GPIO_PIN_13
#define UCPD_CC2_PIN                  LL_GPIO_PIN_14


/*
 * Timings.
 */

#define TYPEC_ATTACH_DEBOUNCE_MS      80U
#define TYPEC_DETACH_DEBOUNCE_MS      120U
#define USB_HOST_START_DELAY_MS       200U
#define PERIODIC_DUMP_MS              2000U


/*
 * Periodicky vypis:
 *
 * [UCPD] STATE=...
 *
 * 0 = vypnuto
 * 1 = zapnuto
 */
#define UCPD_PERIODIC_DUMP_ENABLE     0U


/*
 * Globalni vypinac pro vsechny [UCPD] STATE=... dumpy.
 *
 * 0 = zadne dlouhe register/state dumpy
 * 1 = dumpy zapnute
 */
#define UCPD_STATE_DUMP_ENABLE        0U


#define UCPD_DIAG_EVENT_CC1           (1UL << 0)
#define UCPD_DIAG_EVENT_CC2           (1UL << 1)


typedef enum
{
    TYPEC_ORIENTATION_NONE = 0,
    TYPEC_ORIENTATION_CC1,
    TYPEC_ORIENTATION_CC2
} typec_orientation_t;


typedef enum
{
    HOST_TEST_STATE_UNATTACHED = 0,
    HOST_TEST_STATE_ATTACHED_WAIT_VBUS,
    HOST_TEST_STATE_ATTACHED_WAIT_USB_START,
    HOST_TEST_STATE_USB_HOST_ACTIVE
} host_test_state_t;


static volatile uint32_t ucpd_diag_pending_events = 0U;

static host_test_state_t host_state =
    HOST_TEST_STATE_UNATTACHED;

static typec_orientation_t source_orientation =
    TYPEC_ORIENTATION_NONE;

static uint8_t candidate_attached = 0U;
static typec_orientation_t candidate_orientation =
    TYPEC_ORIENTATION_NONE;

static uint32_t candidate_since_ms = 0U;

static uint8_t last_vbus = 0U;
static uint32_t vbus_last_change_ms = 0U;

static uint32_t host_start_wait_since_ms = 0U;
static uint32_t periodic_dump_ms = 0U;

static uint8_t vbus_fet_enabled = 0U;
static uint8_t usb_host_started = 0U;


static uint8_t ucpd_diag_read_vbus(void)
{
    return
        (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_4) == GPIO_PIN_SET)
        ? 1U
        : 0U;
}


static uint32_t ucpd_get_cc1_vstate(void)
{
    return
        (UCPD1->SR & UCPD_SR_TYPEC_VSTATE_CC1) >> UCPD_SR_TYPEC_VSTATE_CC1_Pos;
}


static uint32_t ucpd_get_cc2_vstate(void)
{
    return
        (UCPD1->SR & UCPD_SR_TYPEC_VSTATE_CC2) >> UCPD_SR_TYPEC_VSTATE_CC2_Pos;
}


static void vbus_fet_off_hiz(void)
{
    /*
     * OFF:
     * PB8 Hi-Z/input bez internich pullu.
     * Externi gate-source pull-up vytahne gate na +5 V.
     */

    LL_GPIO_SetPinPull(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinMode(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_MODE_INPUT);

    /*
     * Priprav ODR=0 pro budouci prepnuti do output LOW.
     */

    HAL_GPIO_WritePin(
        VBUS_FET_PORT,
        VBUS_FET_PIN,
        GPIO_PIN_RESET);
}


static void vbus_fet_on_drive_low(void)
{
    /*
     * ON:
     * PB8 output LOW.
     */

    HAL_GPIO_WritePin(
        VBUS_FET_PORT,
        VBUS_FET_PIN,
        GPIO_PIN_RESET);

    LL_GPIO_SetPinPull(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinOutputType(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_OUTPUT_PUSHPULL);

    LL_GPIO_SetPinSpeed(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_SPEED_FREQ_LOW);

    LL_GPIO_SetPinMode(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_MODE_OUTPUT);
}


static void vbus_fet_apply(uint8_t enable)
{
    enable =
        enable ? 1U : 0U;

    if(enable == vbus_fet_enabled)
    {
        return;
    }

    vbus_fet_enabled =
        enable;

    if(vbus_fet_enabled)
    {
        vbus_fet_on_drive_low();

        uart_write_str("[VBUS-FET] ON: PB8 output LOW\r\n");
    }
    else
    {
        vbus_fet_off_hiz();

        uart_write_str("[VBUS-FET] OFF: PB8 Hi-Z\r\n");
    }
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


static void decode_pb13_pb14_pb8(void)
{
    uint32_t moder =
        GPIOB->MODER;

    uint32_t pupdr =
        GPIOB->PUPDR;

    uint32_t afrh =
        GPIOB->AFR[1];

    uint32_t pb8_mode =
        (moder >> (8U * 2U)) & 0x3U;

    uint32_t pb13_mode =
        (moder >> (13U * 2U)) & 0x3U;

    uint32_t pb14_mode =
        (moder >> (14U * 2U)) & 0x3U;

    uint32_t pb8_pull =
        (pupdr >> (8U * 2U)) & 0x3U;

    uint32_t pb13_pull =
        (pupdr >> (13U * 2U)) & 0x3U;

    uint32_t pb14_pull =
        (pupdr >> (14U * 2U)) & 0x3U;

    uint32_t pb13_af =
        (afrh >> ((13U - 8U) * 4U)) & 0xFU;

    uint32_t pb14_af =
        (afrh >> ((14U - 8U) * 4U)) & 0xFU;


    uart_write_str("[PB8] MODE=");
    uart_write_hex(pb8_mode);

    uart_write_str(" PULL=");
    uart_write_hex(pb8_pull);

    uart_write_str(" IDR=");
    uart_write_hex((GPIOB->IDR & GPIO_PIN_8) ? 1U : 0U);

    uart_write_str(" ODR=");
    uart_write_hex((GPIOB->ODR & GPIO_PIN_8) ? 1U : 0U);

    uart_write_str("\r\n");


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
#if UCPD_STATE_DUMP_ENABLE

    uart_write_str("[UCPD] STATE=");
    uart_write_hex((uint32_t)host_state);

    uart_write_str(" CFG1=");
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

    uart_write_str(" CC1_V=");
    uart_write_hex(ucpd_get_cc1_vstate());

    uart_write_str(" CC2_V=");
    uart_write_hex(ucpd_get_cc2_vstate());

    uart_write_str(" VBUS=");
    uart_write_hex(ucpd_diag_read_vbus());

    uart_write_str(" FET=");
    uart_write_hex(vbus_fet_enabled);

    uart_write_str(" HOST=");
    uart_write_hex(usb_host_started);

    uart_write_str(" PWR_UCPDR=");
    uart_write_hex(PWR->UCPDR);

    uart_write_str("\r\n");

#else

    /*
     * State dump vypnuty.
     * Funkci nechavame volatelnou, aby nebylo nutne mazat vsechny debug cally.
     */

#endif
}


static void ucpd_disable_dead_battery(void)
{
    uart_write_str("[UCPD] Disable dead-battery Rd\r\n");

    PWR->UCPDR |=
        PWR_UCPDR_UCPD_DBDIS;

    uart_write_str("[PWR] UCPDR=");
    uart_write_hex(PWR->UCPDR);
    uart_write_str("\r\n");
}


static void ucpd_hw_init_source_mode(void)
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


    /*
     * Keep factory/reset CFG3 trim.
     */


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
     * SOURCE/Rp mode:
     *
     * CR = 0x00000C80
     */

    cr =
          UCPD_CR_ANASUBMODE_0
        | UCPD_CR_CCENABLE_0
        | UCPD_CR_CCENABLE_1;

    UCPD1->CR =
        cr;


    for(volatile uint32_t i = 0; i < 1000U; i++)
    {
        __NOP();
    }


    uart_write_str("[UCPD] SOURCE/RP MODE CR=");
    uart_write_hex(cr);
    uart_write_str(" : ANASUBMODE_0 + CCENABLE\r\n");

    ucpd_dump_state();
}


static typec_orientation_t source_detect_orientation_from_sr(void)
{
    uint32_t cc1 =
        ucpd_get_cc1_vstate();

    uint32_t cc2 =
        ucpd_get_cc2_vstate();


    /*
     * Observed in source/Rp mode:
     *
     * open/unattached -> vstate 2
     * sink/Rd attached -> vstate 1
     */

    if((cc1 == 1U) && (cc2 != 1U))
    {
        return TYPEC_ORIENTATION_CC1;
    }

    if((cc2 == 1U) && (cc1 != 1U))
    {
        return TYPEC_ORIENTATION_CC2;
    }

    return TYPEC_ORIENTATION_NONE;
}


static void usb_host_start_once(void)
{
    if(usb_host_started)
    {
        return;
    }

    uart_write_str("[USB-HOST] START\r\n");

    usb_manager_start_host();

    usb_host_started =
        1U;

    uart_write_str("[USB-HOST] START DONE\r\n");
}


uint8_t ucpd_diag_is_source(void)
{
    return
        (host_state != HOST_TEST_STATE_UNATTACHED) ? 1U : 0U;
}


void ucpd_diag_init(void)
{
    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOC);

    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOB);


    HAL_PWREx_EnableVddUSB();

    ucpd_disable_dead_battery();


    /*
     * Default source VBUS OFF.
     */

    vbus_fet_enabled = 0U;
    vbus_fet_off_hiz();


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
        UCPD_CC_PORT,
        UCPD_CC1_PIN,
        LL_GPIO_MODE_ANALOG);

    LL_GPIO_SetPinMode(
        UCPD_CC_PORT,
        UCPD_CC2_PIN,
        LL_GPIO_MODE_ANALOG);


    LL_GPIO_SetPinPull(
        UCPD_CC_PORT,
        UCPD_CC1_PIN,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinPull(
        UCPD_CC_PORT,
        UCPD_CC2_PIN,
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
    decode_pb13_pb14_pb8();


    ucpd_hw_init_source_mode();


    uart_write_str("[GPIOB] AFTER UCPD SOURCE INIT\r\n");
    dump_gpiob();
    decode_pb13_pb14_pb8();


    /*
     * Type-C event detector only.
     * PD RX stays disabled for this source/Rp attach test.
     */

    LL_UCPD_TypeCDetectionCC1Enable(UCPD1);
    LL_UCPD_TypeCDetectionCC2Enable(UCPD1);


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

    periodic_dump_ms =
        HAL_GetTick();

    candidate_attached = 0U;
    candidate_orientation = TYPEC_ORIENTATION_NONE;
    candidate_since_ms = HAL_GetTick();

    host_state =
        HOST_TEST_STATE_UNATTACHED;

    source_orientation =
        TYPEC_ORIENTATION_NONE;

    host_start_wait_since_ms =
        0U;

    usb_host_started =
        0U;


    uart_write_str("===== UCPD READY HOST BRING-UP TEST =====\r\n");
    uart_write_str("[TEST] Connect USB-C sink/device. Host starts after VBUS PRESENT.\r\n");

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


static void host_state_machine_task(uint32_t now)
{
    typec_orientation_t detected =
        source_detect_orientation_from_sr();

    uint8_t detected_attached =
        (detected != TYPEC_ORIENTATION_NONE) ? 1U : 0U;


    /*
     * Debounce candidate change.
     */

    if(
        (detected_attached != candidate_attached) ||
        (detected != candidate_orientation)
    )
    {
        candidate_attached =
            detected_attached;

        candidate_orientation =
            detected;

        candidate_since_ms =
            now;
    }


    switch(host_state)
    {
        case HOST_TEST_STATE_UNATTACHED:
        {
            if(candidate_attached)
            {
                if((now - candidate_since_ms) >= TYPEC_ATTACH_DEBOUNCE_MS)
                {
                    source_orientation =
                        candidate_orientation;

                    if(source_orientation == TYPEC_ORIENTATION_CC1)
                    {
                        uart_write_str("[TYPEC-SRC] SINK ATTACHED on CC1\r\n");
                    }
                    else if(source_orientation == TYPEC_ORIENTATION_CC2)
                    {
                        uart_write_str("[TYPEC-SRC] SINK ATTACHED on CC2\r\n");
                    }
                    else
                    {
                        uart_write_str("[TYPEC-SRC] SINK ATTACHED unknown orientation\r\n");
                    }

                    ucpd_dump_state();

                    /*
                     * Source provides VBUS only after valid attach.
                     */

                    vbus_fet_apply(1U);

                    host_state =
                        HOST_TEST_STATE_ATTACHED_WAIT_VBUS;
                }
            }

            break;
        }


        case HOST_TEST_STATE_ATTACHED_WAIT_VBUS:
        {
            if(candidate_attached == 0U)
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    uart_write_str("[TYPEC-SRC] DETACH before VBUS\r\n");

                    vbus_fet_apply(0U);

                    source_orientation =
                        TYPEC_ORIENTATION_NONE;

                    host_state =
                        HOST_TEST_STATE_UNATTACHED;

                    ucpd_dump_state();
                }

                break;
            }

            if(ucpd_diag_read_vbus())
            {
                uart_write_str("[TYPEC-SRC] VBUS PRESENT, wait before host start\r\n");

                host_start_wait_since_ms =
                    now;

                host_state =
                    HOST_TEST_STATE_ATTACHED_WAIT_USB_START;
            }

            break;
        }


        case HOST_TEST_STATE_ATTACHED_WAIT_USB_START:
        {
            if(candidate_attached == 0U)
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    uart_write_str("[TYPEC-SRC] DETACH before USB host start\r\n");

                    vbus_fet_apply(0U);

                    source_orientation =
                        TYPEC_ORIENTATION_NONE;

                    host_state =
                        HOST_TEST_STATE_UNATTACHED;

                    ucpd_dump_state();
                }

                break;
            }

            if((now - host_start_wait_since_ms) >= USB_HOST_START_DELAY_MS)
            {
                usb_host_start_once();

                host_state =
                    HOST_TEST_STATE_USB_HOST_ACTIVE;

                ucpd_dump_state();
            }

            break;
        }


        case HOST_TEST_STATE_USB_HOST_ACTIVE:
        {
            if(candidate_attached == 0U)
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    uart_write_str("[TYPEC-SRC] DETACH while USB host active\r\n");

                    /*
                     * For this bring-up test:
                     * - vypneme VBUS
                     * - zustaneme bez explicitniho host deinitu
                     *
                     * Pozdeji doplnime korektni usb_manager_stop_host(),
                     * pokud v projektu pridame takove API.
                     */

                    vbus_fet_apply(0U);

                    source_orientation =
                        TYPEC_ORIENTATION_NONE;

                    host_state =
                        HOST_TEST_STATE_UNATTACHED;

                    ucpd_dump_state();
                }
            }

            break;
        }


        default:
        {
            uart_write_str("[TYPEC-SRC] Invalid state, reset to unattached\r\n");

            vbus_fet_apply(0U);

            host_state =
                HOST_TEST_STATE_UNATTACHED;

            source_orientation =
                TYPEC_ORIENTATION_NONE;

            break;
        }
    }
}


void ucpd_diag_task(void)
{
    uint32_t now =
        HAL_GetTick();

    uint8_t vbus =
        ucpd_diag_read_vbus();


    /*
     * Host bring-up state machine.
     */

    host_state_machine_task(now);


    /*
     * Periodic state dump.
     *
     * Vypnuto pres UCPD_PERIODIC_DUMP_ENABLE,
     * aby UART nespamoval:
     *
     * [UCPD] STATE=...
     */

#if UCPD_PERIODIC_DUMP_ENABLE
    if((now - periodic_dump_ms) > PERIODIC_DUMP_MS)
    {
        periodic_dump_ms =
            now;

        ucpd_dump_state();
    }
#else
    (void)periodic_dump_ms;
#endif


    /*
     * VBUS monitor.
     */

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


    /*
     * CC event logging.
     */

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