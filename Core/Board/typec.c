#include "typec.h"

#include "main.h"
#include "stm32h5xx_hal_gpio_ex.h"

static typec_role_t active_role = TYPEC_ROLE_NONE;
static typec_state_t current_state = TYPEC_STATE_UNATTACHED;
static typec_cc_orientation_t current_orientation = TYPEC_ORIENTATION_NONE;
static uint8_t vbus_enabled = 0U;

static bool typec_sink_is_rp(uint32_t cc_state)
{
    return (cc_state == LL_UCPD_SNK_CC1_VRP) ||
           (cc_state == LL_UCPD_SNK_CC1_VRP15A) ||
           (cc_state == LL_UCPD_SNK_CC1_VRP30A);
}

static bool typec_source_is_rd(uint32_t cc_state)
{
    return (cc_state == LL_UCPD_SRC_CC1_VRD);
}

static bool typec_source_is_open(uint32_t cc_state)
{
    return (cc_state == LL_UCPD_SRC_CC1_OPEN);
}

static bool typec_source_is_ra(uint32_t cc_state)
{
    return (cc_state == LL_UCPD_SRC_CC1_VRA);
}

static void typec_update_status(void)
{
    uint32_t sr = UCPD1->SR;
    uint32_t cc1 = sr & UCPD_SR_TYPEC_VSTATE_CC1;
    uint32_t cc2 = sr & UCPD_SR_TYPEC_VSTATE_CC2;

    current_orientation = TYPEC_ORIENTATION_NONE;

    if (active_role == TYPEC_ROLE_SOURCE)
    {
        if (typec_source_is_rd(cc1) && (typec_source_is_open(cc2) || typec_source_is_ra(cc2)))
        {
            current_state = TYPEC_STATE_ATTACHED_SOURCE;
            current_orientation = TYPEC_ORIENTATION_CC1;
            return;
        }

        if (typec_source_is_rd(cc2) && (typec_source_is_open(cc1) || typec_source_is_ra(cc1)))
        {
            current_state = TYPEC_STATE_ATTACHED_SOURCE;
            current_orientation = TYPEC_ORIENTATION_CC2;
            return;
        }

        if ((typec_source_is_open(cc1) || typec_source_is_ra(cc1)) &&
            (typec_source_is_open(cc2) || typec_source_is_ra(cc2)))
        {
            current_state = TYPEC_STATE_UNATTACHED;
            return;
        }

        current_state = TYPEC_STATE_INVALID;
        return;
    }

    if (active_role == TYPEC_ROLE_SINK)
    {
        if (typec_sink_is_rp(cc1) && (cc2 == LL_UCPD_SNK_CC2_VOPEN))
        {
            current_state = TYPEC_STATE_ATTACHED_SINK;
            current_orientation = TYPEC_ORIENTATION_CC1;
            return;
        }

        if (typec_sink_is_rp(cc2) && (cc1 == LL_UCPD_SNK_CC1_VOPEN))
        {
            current_state = TYPEC_STATE_ATTACHED_SINK;
            current_orientation = TYPEC_ORIENTATION_CC2;
            return;
        }

        if ((cc1 == LL_UCPD_SNK_CC1_VOPEN) && (cc2 == LL_UCPD_SNK_CC2_VOPEN))
        {
            current_state = TYPEC_STATE_UNATTACHED;
            return;
        }

        current_state = TYPEC_STATE_INVALID;
        return;
    }

    current_state = TYPEC_STATE_UNATTACHED;
}

static void typec_configure_role(typec_role_t role)
{
    LL_UCPD_Disable(UCPD1);
    UCPD1->IMR = 0U;
    UCPD1->ICR = 0xFFFFFFFFU;
    UCPD1->CR = 0U;

    if (role == TYPEC_ROLE_NONE)
    {
        active_role = TYPEC_ROLE_NONE;
        current_state = TYPEC_STATE_UNATTACHED;
        current_orientation = TYPEC_ORIENTATION_NONE;
        return;
    }

    LL_UCPD_SetPSCClk(UCPD1, LL_UCPD_PSC_DIV1);
    LL_UCPD_SetTransWin(UCPD1, 16U);
    LL_UCPD_SetIfrGap(UCPD1, 8U);
    LL_UCPD_SetHbitClockDiv(UCPD1, 23U);
    LL_UCPD_RxAnalogFilterEnable(UCPD1);
    LL_UCPD_RxFilterEnable(UCPD1);
    LL_UCPD_ForceClockEnable(UCPD1);
    LL_UCPD_TypeCDetectionCC1Enable(UCPD1);
    LL_UCPD_TypeCDetectionCC2Enable(UCPD1);
    LL_UCPD_SetccEnable(UCPD1, LL_UCPD_CCENABLE_CC1CC2);
    LL_UCPD_SetCCPin(UCPD1, LL_UCPD_CCPIN_CC1);
    LL_UCPD_SetRxMode(UCPD1, LL_UCPD_RXMODE_NORMAL);
    LL_UCPD_RxDisable(UCPD1);

    if (role == TYPEC_ROLE_SOURCE)
    {
        LL_UCPD_SetSRCRole(UCPD1);
        LL_UCPD_SetRpResistor(UCPD1, LL_UCPD_RESISTOR_DEFAULT);
    }
    else
    {
        LL_UCPD_SetSNKRole(UCPD1);
    }

    LL_UCPD_Enable(UCPD1);
    active_role = role;
    typec_update_status();
}

void typec_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_UCPD1_CLK_ENABLE();
    __HAL_RCC_UCPD1_FORCE_RESET();
    __HAL_RCC_UCPD1_RELEASE_RESET();

    gpio_init.Pin = GPIO_PIN_13 | GPIO_PIN_14;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = GPIO_AF6_UCPD1;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    active_role = TYPEC_ROLE_NONE;
    current_state = TYPEC_STATE_UNATTACHED;
    current_orientation = TYPEC_ORIENTATION_NONE;
    vbus_enabled = 0U;

    typec_configure_role(TYPEC_ROLE_NONE);
}

void typec_set_role(typec_role_t role)
{
    if (role == active_role)
    {
        return;
    }

    typec_configure_role(role);
}

typec_state_t typec_get_state(void)
{
    typec_update_status();
    return current_state;
}

typec_role_t typec_get_role(void)
{
    return active_role;
}

typec_cc_orientation_t typec_get_cc_orientation(void)
{
    return current_orientation;
}

void typec_vbus_enable(bool enabled)
{
    vbus_enabled = enabled ? 1U : 0U;
    HAL_GPIO_WritePin(VBUS_ON_GPIO_Port, VBUS_ON_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool typec_vbus_is_enabled(void)
{
    return (vbus_enabled != 0U);
}

void typec_irq_handler(void)
{
    UCPD1->ICR = UCPD_ICR_TYPECEVT1CF | UCPD_ICR_TYPECEVT2CF;
}