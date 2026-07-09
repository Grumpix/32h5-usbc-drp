#include "ucpd_diag.h"

#include "main.h"
#include "uart.h"
#include "usb_manager.h"

#include "stm32h5xx_ll_ucpd.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

#include <stdint.h>


/*
 * MANUAL USB-C ROLE SWITCH SAFE TEST
 *
 * Default after boot:
 *   DEVICE / Sink / Rd / CDC
 *
 * PA0 button:
 *   toggles DEVICE/Sink <-> HOST/Source
 *
 * SAFETY RULES:
 *
 * - HOST/SOURCE may enable VBUS FET only after valid Type-C attach.
 * - HOST/SOURCE detach always forces VBUS FET OFF and stops host stack.
 * - DEVICE/SINK never enables VBUS FET.
 * - Switching to HOST/SOURCE is blocked if VBUS is already present.
 * - Leaving HOST/SOURCE first forces VBUS FET OFF before changing anything else.
 */


/* =========================
   LOG CONFIG
========================= */

/*
 * Known-good funkcni logika zustava stejna.
 * Tady jen ridime mnozstvi logu.
 */
#define UCPD_LOG_BOOT                 1U // 0U = vypnuto
#define UCPD_LOG_GPIO                 1U
#define UCPD_LOG_ROLE                 1U
#define UCPD_LOG_ATTACH               1U
#define UCPD_LOG_VBUS                 1U
#define UCPD_LOG_FET                  1U
#define UCPD_LOG_EVENTS               1U
#define UCPD_LOG_HW                   1U


static void ucpd_log(uint8_t enabled, const char *s)
{
    if(enabled)
    {
        uart_write_str(s);
    }
}


static void ucpd_log_hex(uint8_t enabled, const char *prefix, uint32_t value)
{
    if(enabled)
    {
        uart_write_str(prefix);
        uart_write_hex(value);
    }
}


/* =========================
   VBUS FET PB8
========================= */

#define VBUS_FET_PORT                 GPIOB
#define VBUS_FET_PIN                  GPIO_PIN_8

#define VBUS_FET_LL_PORT              GPIOB
#define VBUS_FET_LL_PIN               LL_GPIO_PIN_8


/* =========================
   CC pins
========================= */

#define UCPD_CC_PORT                  GPIOB
#define UCPD_CC1_PIN                  LL_GPIO_PIN_13
#define UCPD_CC2_PIN                  LL_GPIO_PIN_14


/* =========================
   Timing
========================= */

#define TYPEC_ATTACH_DEBOUNCE_MS      80U
#define TYPEC_DETACH_DEBOUNCE_MS      120U

#define USB_ROLE_START_DELAY_MS       200U
#define PERIODIC_DUMP_MS              2000U


#define UCPD_PERIODIC_DUMP_ENABLE     0U
#define UCPD_STATE_DUMP_ENABLE        0U


#define UCPD_DIAG_EVENT_CC1           (1UL << 0)
#define UCPD_DIAG_EVENT_CC2           (1UL << 1)


typedef enum
{
    TYPEC_ROLE_DEVICE_SINK = 0,
    TYPEC_ROLE_HOST_SOURCE
} typec_role_t;


typedef enum
{
    TYPEC_ORIENTATION_NONE = 0,
    TYPEC_ORIENTATION_CC1,
    TYPEC_ORIENTATION_CC2
} typec_orientation_t;


typedef enum
{
    ROLE_STATE_UNATTACHED = 0,
    ROLE_STATE_ATTACHED_WAIT_VBUS,
    ROLE_STATE_ATTACHED_WAIT_USB_START,
    ROLE_STATE_USB_ACTIVE
} role_state_t;


static volatile uint32_t ucpd_diag_pending_events = 0U;

static typec_role_t current_role =
    TYPEC_ROLE_DEVICE_SINK;

static role_state_t role_state =
    ROLE_STATE_UNATTACHED;

static typec_orientation_t active_orientation =
    TYPEC_ORIENTATION_NONE;

static uint8_t candidate_attached = 0U;

static typec_orientation_t candidate_orientation =
    TYPEC_ORIENTATION_NONE;

static uint32_t candidate_since_ms = 0U;

static uint8_t last_vbus = 0U;
static uint32_t vbus_last_change_ms = 0U;

static uint32_t usb_start_wait_since_ms = 0U;
static uint32_t periodic_dump_ms = 0U;

static uint8_t vbus_fet_enabled = 0U;
static uint8_t usb_started = 0U;


/* =========================
   Helpers
========================= */

static const char *role_name(typec_role_t role)
{
    if(role == TYPEC_ROLE_HOST_SOURCE)
    {
        return "HOST/SOURCE";
    }

    return "DEVICE/SINK";
}


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
        (UCPD1->SR & UCPD_SR_TYPEC_VSTATE_CC1) >>
        UCPD_SR_TYPEC_VSTATE_CC1_Pos;
}


static uint32_t ucpd_get_cc2_vstate(void)
{
    return
        (UCPD1->SR & UCPD_SR_TYPEC_VSTATE_CC2) >>
        UCPD_SR_TYPEC_VSTATE_CC2_Pos;
}


static void vbus_fet_off_hiz(void)
{
    /*
     * OFF:
     * PB8 Hi-Z/input without internal pull.
     * External gate-source pull-up pulls gate to +5 V.
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
     * Prepare ODR LOW for future ON.
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
     * P-MOS gate goes to GND.
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

    /*
     * SAFETY:
     * VBUS FET may be ON only in HOST/SOURCE role.
     */
    if(current_role != TYPEC_ROLE_HOST_SOURCE)
    {
        enable =
            0U;
    }

    if(enable == vbus_fet_enabled)
    {
        return;
    }

    vbus_fet_enabled =
        enable;

    if(vbus_fet_enabled)
    {
        vbus_fet_on_drive_low();

        ucpd_log(
            UCPD_LOG_FET,
            "[VBUS-FET] ON: PB8 output LOW\r\n");
    }
    else
    {
        vbus_fet_off_hiz();

        ucpd_log(
            UCPD_LOG_FET,
            "[VBUS-FET] OFF: PB8 Hi-Z\r\n");
    }
}


static void force_vbus_fet_off_raw(void)
{
    /*
     * Emergency physical FET OFF independent of current_role.
     */

    vbus_fet_enabled =
        0U;

    vbus_fet_off_hiz();

    ucpd_log(
        UCPD_LOG_FET,
        "[VBUS-FET] FORCE OFF: PB8 Hi-Z\r\n");
}


static void dump_gpiob(void)
{
    if(UCPD_LOG_GPIO == 0U)
    {
        return;
    }

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


    if(UCPD_LOG_GPIO == 0U)
    {
        (void)pb8_mode;
        (void)pb13_mode;
        (void)pb14_mode;
        (void)pb8_pull;
        (void)pb13_pull;
        (void)pb14_pull;
        (void)pb13_af;
        (void)pb14_af;

        return;
    }


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
}


static void ucpd_dump_state(void)
{
#if UCPD_STATE_DUMP_ENABLE

    uart_write_str("[UCPD] ROLE=");
    uart_write_str(role_name(current_role));

    uart_write_str(" STATE=");
    uart_write_hex((uint32_t)role_state);

    uart_write_str(" CFG1=");
    uart_write_hex(UCPD1->CFG1);

    uart_write_str(" CFG2=");
    uart_write_hex(UCPD1->CFG2);

    uart_write_str(" CR=");
    uart_write_hex(UCPD1->CR);

    uart_write_str(" SR=");
    uart_write_hex(UCPD1->SR);

    uart_write_str(" CC1_V=");
    uart_write_hex(ucpd_get_cc1_vstate());

    uart_write_str(" CC2_V=");
    uart_write_hex(ucpd_get_cc2_vstate());

    uart_write_str(" VBUS=");
    uart_write_hex(ucpd_diag_read_vbus());

    uart_write_str(" FET=");
    uart_write_hex(vbus_fet_enabled);

    uart_write_str(" USB=");
    uart_write_hex(usb_started);

    uart_write_str("\r\n");

#else
    (void)role_state;
#endif
}


static void ucpd_disable_dead_battery(void)
{
    ucpd_log(
        UCPD_LOG_HW,
        "[UCPD] Disable dead-battery Rd\r\n");

    PWR->UCPDR |=
        PWR_UCPDR_UCPD_DBDIS;

    ucpd_log_hex(
        UCPD_LOG_HW,
        "[PWR] UCPDR=",
        PWR->UCPDR);

    ucpd_log(
        UCPD_LOG_HW,
        "\r\n");
}


static void ucpd_hw_common_init(void)
{
    UCPD1->CFG1 =
          (5U  << UCPD_CFG1_HBITCLKDIV_Pos)
        | (17U << UCPD_CFG1_IFRGAP_Pos)
        | (15U << UCPD_CFG1_TRANSWIN_Pos)
        | (0U  << UCPD_CFG1_PSC_UCPDCLK_Pos);

    UCPD1->CFG2 =
        UCPD_CFG2_FORCECLK;

    UCPD1->CFG1 |=
        UCPD_CFG1_UCPDEN;
}


static void ucpd_hw_set_sink_mode(void)
{
    uint32_t cr;

    UCPD1->CR =
        0U;

    cr =
          UCPD_CR_ANAMODE
        | UCPD_CR_CCENABLE_0
        | UCPD_CR_CCENABLE_1;

    UCPD1->CR =
        cr;

    for(volatile uint32_t i = 0U; i < 1000U; i++)
    {
        __NOP();
    }

    ucpd_log_hex(
        UCPD_LOG_HW,
        "[UCPD] SINK/RD MODE CR=",
        cr);

    ucpd_log(
        UCPD_LOG_HW,
        "\r\n");
}


static void ucpd_hw_set_source_mode(void)
{
    uint32_t cr;

    UCPD1->CR =
        0U;

    cr =
          UCPD_CR_ANASUBMODE_0
        | UCPD_CR_CCENABLE_0
        | UCPD_CR_CCENABLE_1;

    UCPD1->CR =
        cr;

    for(volatile uint32_t i = 0U; i < 1000U; i++)
    {
        __NOP();
    }

    ucpd_log_hex(
        UCPD_LOG_HW,
        "[UCPD] SOURCE/RP MODE CR=",
        cr);

    ucpd_log(
        UCPD_LOG_HW,
        "\r\n");
}


static void reset_role_runtime_state(void)
{
    candidate_attached =
        0U;

    candidate_orientation =
        TYPEC_ORIENTATION_NONE;

    candidate_since_ms =
        HAL_GetTick();

    active_orientation =
        TYPEC_ORIENTATION_NONE;

    role_state =
        ROLE_STATE_UNATTACHED;

    usb_start_wait_since_ms =
        0U;

    usb_started =
        0U;

    last_vbus =
        ucpd_diag_read_vbus();

    vbus_last_change_ms =
        HAL_GetTick();

    periodic_dump_ms =
        HAL_GetTick();

    ucpd_diag_pending_events =
        0U;
}


static void apply_role(typec_role_t role)
{
    ucpd_log(
        UCPD_LOG_ROLE,
        "[TYPEC] APPLY ROLE ");

    ucpd_log(
        UCPD_LOG_ROLE,
        role_name(role));

    ucpd_log(
        UCPD_LOG_ROLE,
        "\r\n");

    /*
     * SAFETY FIRST:
     *
     * If leaving HOST/SOURCE, physically turn VBUS FET off first.
     */
    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Leaving HOST/SOURCE -> force VBUS FET OFF first\r\n");

        force_vbus_fet_off_raw();

        HAL_Delay(150U);

        if(ucpd_diag_read_vbus())
        {
            ucpd_log(
                UCPD_LOG_ROLE,
                "[TYPEC] WARN: VBUS still PRESENT after FET OFF\r\n");

            ucpd_log(
                UCPD_LOG_ROLE,
                "[TYPEC] Role switch aborted for safety\r\n");

            return;
        }
    }

    /*
     * Real role change:
     * usb_manager_stop() is correct here.
     */
    usb_manager_stop();

    current_role =
        role;

    force_vbus_fet_off_raw();

    reset_role_runtime_state();

    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        ucpd_hw_set_source_mode();

        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Now HOST/SOURCE. Connect USB device / FTDI.\r\n");
    }
    else
    {
        ucpd_hw_set_sink_mode();

        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Now DEVICE/SINK. Connect to PC.\r\n");
    }

    LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
    LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

    ucpd_dump_state();
}


static typec_orientation_t detect_orientation_source_mode(void)
{
    uint32_t cc1 =
        ucpd_get_cc1_vstate();

    uint32_t cc2 =
        ucpd_get_cc2_vstate();

    /*
     * Source/Rp mode observed:
     * open -> 2
     * Rd attached -> 1
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


static typec_orientation_t detect_orientation_sink_mode(void)
{
    uint32_t cc1 =
        ucpd_get_cc1_vstate();

    uint32_t cc2 =
        ucpd_get_cc2_vstate();

    /*
     * Sink/Rd mode:
     * open -> 0
     * Rp attached -> 1/2/3
     */

    if((cc1 != 0U) && (cc2 == 0U))
    {
        return TYPEC_ORIENTATION_CC1;
    }

    if((cc2 != 0U) && (cc1 == 0U))
    {
        return TYPEC_ORIENTATION_CC2;
    }

    return TYPEC_ORIENTATION_NONE;
}


static typec_orientation_t detect_orientation_for_current_role(void)
{
    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        return
            detect_orientation_source_mode();
    }

    return
        detect_orientation_sink_mode();
}


static void usb_start_once_for_current_role(void)
{
    if(usb_started)
    {
        return;
    }

    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[USB-HOST] START from Type-C source state\r\n");

        usb_manager_start_host();

        usb_started =
            1U;

        ucpd_log(
            UCPD_LOG_ROLE,
            "[USB-HOST] START DONE from Type-C source state\r\n");
    }
    else
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[USB-DEVICE] START from Type-C sink state\r\n");

        usb_manager_start_device();

        usb_started =
            1U;

        ucpd_log(
            UCPD_LOG_ROLE,
            "[USB-DEVICE] START DONE from Type-C sink state\r\n");
    }
}


/* =========================
   Public API
========================= */

uint8_t ucpd_diag_is_source(void)
{
    return
        (current_role == TYPEC_ROLE_HOST_SOURCE) ? 1U : 0U;
}

uint8_t ucpd_diag_is_attached(void)
{
    return
        (active_orientation != TYPEC_ORIENTATION_NONE) ? 1U : 0U;
}


uint8_t ucpd_diag_usb_started(void)
{
    return
        usb_started ? 1U : 0U;
}


uint8_t ucpd_diag_vbus_present(void)
{
    return
        ucpd_diag_read_vbus();
}


uint8_t ucpd_diag_is_unattached(void)
{
    return
        (role_state == ROLE_STATE_UNATTACHED) ? 1U : 0U;
}

void ucpd_diag_request_device_role(void)
{
    if(current_role == TYPEC_ROLE_DEVICE_SINK)
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Already DEVICE/SINK\r\n");

        return;
    }

    apply_role(TYPEC_ROLE_DEVICE_SINK);
}


void ucpd_diag_request_host_role(void)
{
    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Already HOST/SOURCE\r\n");

        return;
    }

    /*
     * SAFETY:
     *
     * If VBUS is already present, we are probably still connected to a PC/source.
     * Do not become HOST/SOURCE against another VBUS source.
     */
    if(ucpd_diag_read_vbus())
    {
        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] BLOCK HOST/SOURCE: VBUS already PRESENT\r\n");

        ucpd_log(
            UCPD_LOG_ROLE,
            "[TYPEC] Disconnect cable first, then press PA0 again\r\n");

        return;
    }

    apply_role(TYPEC_ROLE_HOST_SOURCE);
}


void ucpd_diag_toggle_role(void)
{
    if(current_role == TYPEC_ROLE_HOST_SOURCE)
    {
        ucpd_diag_request_device_role();
    }
    else
    {
        ucpd_diag_request_host_role();
    }
}


void ucpd_diag_init(void)
{
    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOC);

    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOB);

    HAL_PWREx_EnableVddUSB();

    ucpd_disable_dead_battery();

    vbus_fet_enabled =
        0U;

    force_vbus_fet_off_raw();

    LL_APB1_GRP2_EnableClock(
        LL_APB1_GRP2_PERIPH_UCPD1);

    LL_APB1_GRP2_ForceReset(
        LL_APB1_GRP2_PERIPH_UCPD1);

    for(volatile uint32_t i = 0U; i < 100U; i++)
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
     * PB13 = UCPD1_CC1
     * PB14 = UCPD1_CC2
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

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_13,
        LL_GPIO_AF_6);

    LL_GPIO_SetAFPin_8_15(
        GPIOB,
        LL_GPIO_PIN_14,
        LL_GPIO_AF_6);

    LL_GPIO_SetPinPull(
        UCPD_CC_PORT,
        UCPD_CC2_PIN,
        LL_GPIO_PULL_NO);

    ucpd_log(
        UCPD_LOG_GPIO,
        "[GPIOB] AFTER PIN CONFIG\r\n");

    dump_gpiob();
    decode_pb13_pb14_pb8();

    ucpd_hw_common_init();

    current_role =
        TYPEC_ROLE_DEVICE_SINK;

    reset_role_runtime_state();

    ucpd_hw_set_sink_mode();

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

    ucpd_log(
        UCPD_LOG_BOOT,
        "===== UCPD READY MANUAL ROLE SWITCH SAFE TEST =====\r\n");

    ucpd_log(
        UCPD_LOG_BOOT,
        "[TYPEC] Default role: DEVICE/SINK CDC\r\n");

    ucpd_log(
        UCPD_LOG_BOOT,
        "[TYPEC] Press PA0 to toggle DEVICE/SINK <-> HOST/SOURCE\r\n");

    ucpd_log(
        UCPD_LOG_BOOT,
        "[TYPEC] HOST/SOURCE blocked if VBUS already present\r\n");

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


static void role_state_machine_task(uint32_t now)
{
    typec_orientation_t detected =
        detect_orientation_for_current_role();

    uint8_t detected_attached =
        (detected != TYPEC_ORIENTATION_NONE) ? 1U : 0U;

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

    switch(role_state)
    {
        case ROLE_STATE_UNATTACHED:
        {
            if(candidate_attached)
            {
                if((now - candidate_since_ms) >= TYPEC_ATTACH_DEBOUNCE_MS)
                {
                    active_orientation =
                        candidate_orientation;

                    if(current_role == TYPEC_ROLE_HOST_SOURCE)
                    {
                        if(active_orientation == TYPEC_ORIENTATION_CC1)
                        {
                            ucpd_log(
                                UCPD_LOG_ATTACH,
                                "[TYPEC-SRC] SINK ATTACHED on CC1\r\n");
                        }
                        else
                        {
                            ucpd_log(
                                UCPD_LOG_ATTACH,
                                "[TYPEC-SRC] SINK ATTACHED on CC2\r\n");
                        }

                        /*
                         * HOST/SOURCE:
                         * Only after valid attach may we enable VBUS.
                         */
                        vbus_fet_apply(1U);
                    }
                    else
                    {
                        if(active_orientation == TYPEC_ORIENTATION_CC1)
                        {
                            ucpd_log(
                                UCPD_LOG_ATTACH,
                                "[TYPEC-SNK] SOURCE ATTACHED on CC1\r\n");
                        }
                        else
                        {
                            ucpd_log(
                                UCPD_LOG_ATTACH,
                                "[TYPEC-SNK] SOURCE ATTACHED on CC2\r\n");
                        }

                        /*
                         * DEVICE/SINK:
                         * Never enable VBUS.
                         */
                        vbus_fet_apply(0U);
                    }

                    role_state =
                        ROLE_STATE_ATTACHED_WAIT_VBUS;

                    ucpd_dump_state();
                }
            }

            break;
        }


        case ROLE_STATE_ATTACHED_WAIT_VBUS:
        {
            if(candidate_attached == 0U)
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    ucpd_log(
                        UCPD_LOG_ATTACH,
                        "[TYPEC] DETACH before VBUS\r\n");

                    if(current_role == TYPEC_ROLE_HOST_SOURCE)
                    {
                        vbus_fet_apply(0U);
                    }

                    active_orientation =
                        TYPEC_ORIENTATION_NONE;

                    role_state =
                        ROLE_STATE_UNATTACHED;

                    ucpd_dump_state();
                }

                break;
            }

            if(ucpd_diag_read_vbus())
            {
                if(current_role == TYPEC_ROLE_HOST_SOURCE)
                {
                    ucpd_log(
                        UCPD_LOG_VBUS,
                        "[TYPEC-SRC] VBUS PRESENT, wait before host start\r\n");
                }
                else
                {
                    ucpd_log(
                        UCPD_LOG_VBUS,
                        "[TYPEC-SNK] VBUS PRESENT from PC, wait before device start\r\n");
                }

                usb_start_wait_since_ms =
                    now;

                role_state =
                    ROLE_STATE_ATTACHED_WAIT_USB_START;
            }

            break;
        }


        case ROLE_STATE_ATTACHED_WAIT_USB_START:
        {
            if(candidate_attached == 0U)
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    ucpd_log(
                        UCPD_LOG_ATTACH,
                        "[TYPEC] DETACH before USB start\r\n");

                    if(current_role == TYPEC_ROLE_HOST_SOURCE)
                    {
                        vbus_fet_apply(0U);
                    }

                    active_orientation =
                        TYPEC_ORIENTATION_NONE;

                    role_state =
                        ROLE_STATE_UNATTACHED;

                    ucpd_dump_state();
                }

                break;
            }

            if(ucpd_diag_read_vbus() == 0U)
            {
                ucpd_log(
                    UCPD_LOG_VBUS,
                    "[TYPEC] VBUS LOST before USB start\r\n");

                if(current_role == TYPEC_ROLE_HOST_SOURCE)
                {
                    vbus_fet_apply(0U);
                }

                role_state =
                    ROLE_STATE_ATTACHED_WAIT_VBUS;

                break;
            }

            if((now - usb_start_wait_since_ms) >= USB_ROLE_START_DELAY_MS)
            {
                usb_start_once_for_current_role();

                role_state =
                    ROLE_STATE_USB_ACTIVE;

                ucpd_dump_state();
            }

            break;
        }


        case ROLE_STATE_USB_ACTIVE:
        {
            if((candidate_attached == 0U) || (ucpd_diag_read_vbus() == 0U))
            {
                if((now - candidate_since_ms) >= TYPEC_DETACH_DEBOUNCE_MS)
                {
                    ucpd_log(
                        UCPD_LOG_ATTACH,
                        "[TYPEC] DETACH while USB active\r\n");

                    if(current_role == TYPEC_ROLE_HOST_SOURCE)
                    {
                        /*
                         * HOST/SOURCE detach:
                         *
                         * Fyzicky odpojene USB-C sink zarizeni.
                         *
                         * Bezpecne:
                         * - vypnout VBUS FET
                         * - zastavit TinyUSB host stack
                         * - vratit state machine do UNATTACHED
                         * - znovu re-armnout UCPD Source/Rp mode
                         */
                        ucpd_log(
                            UCPD_LOG_ATTACH,
                            "[TYPEC-SRC] detach -> VBUS OFF + HOST STOP + SOURCE REARM\r\n");

                        vbus_fet_apply(0U);

                        usb_manager_stop();

                        usb_started =
                            0U;

                        active_orientation =
                            TYPEC_ORIENTATION_NONE;

                        candidate_attached =
                            0U;

                        candidate_orientation =
                            TYPEC_ORIENTATION_NONE;

                        candidate_since_ms =
                            now;

                        usb_start_wait_since_ms =
                            0U;

                        role_state =
                            ROLE_STATE_UNATTACHED;

                        /*
                         * Re-arm UCPD Source/Rp detection after detach.
                         */
                        ucpd_hw_set_source_mode();

                        LL_UCPD_ClearFlag_TypeCEventCC1(UCPD1);
                        LL_UCPD_ClearFlag_TypeCEventCC2(UCPD1);

                        ucpd_diag_pending_events =
                            0U;

                        last_vbus =
                            ucpd_diag_read_vbus();

                        vbus_last_change_ms =
                            now;

                        ucpd_dump_state();
                    }
                    else
                    {
                        /*
                         * DEVICE/SINK:
                         *
                         * Device nikdy nezapina VBUS.
                         * TinyUSB device stack nechavame bezet kvuli CDC replug.
                         */
                        ucpd_log(
                            UCPD_LOG_ATTACH,
                            "[TYPEC-SNK] detach -> keep DEVICE stack active\r\n");

                        usb_started =
                            1U;

                        active_orientation =
                            TYPEC_ORIENTATION_NONE;

                        role_state =
                            ROLE_STATE_UNATTACHED;

                        ucpd_dump_state();
                    }
                }
            }

            break;
        }


        default:
        {
            ucpd_log(
                UCPD_LOG_ROLE,
                "[TYPEC] Invalid state, reset to unattached\r\n");

            force_vbus_fet_off_raw();

            usb_manager_stop();

            usb_started =
                0U;

            active_orientation =
                TYPEC_ORIENTATION_NONE;

            role_state =
                ROLE_STATE_UNATTACHED;

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

    role_state_machine_task(now);

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

    if(vbus != last_vbus)
    {
        if((now - vbus_last_change_ms) > 50U)
        {
            last_vbus =
                vbus;

            vbus_last_change_ms =
                now;

            ucpd_log(
                UCPD_LOG_VBUS,
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

        ucpd_diag_pending_events =
            0U;

        __enable_irq();

        if(ev & UCPD_DIAG_EVENT_CC1)
        {
            ucpd_log(
                UCPD_LOG_EVENTS,
                "[UCPD] CC1 EVENT\r\n");
        }

        if(ev & UCPD_DIAG_EVENT_CC2)
        {
            ucpd_log(
                UCPD_LOG_EVENTS,
                "[UCPD] CC2 EVENT\r\n");
        }

        ucpd_dump_state();
    }
}