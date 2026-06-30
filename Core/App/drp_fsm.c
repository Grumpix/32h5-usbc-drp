#include "drp_fsm.h"
#include "usb_manager.h"
#include "usb_hw.h"
#include "stm32h5xx_ll_ucpd.h"
#include "usb_debug.h"
#include "main.h"
#include "stm32h5xx_hal.h"
#include "stm32h5xx_ll_rcc.h"
typedef enum
{
    DRP_IDLE = 0,
    DRP_WAIT_STABLE,
    DRP_ATTACHED
} drp_state_t;

static volatile drp_state_t state = DRP_IDLE;

static volatile drp_role_t current_role = DRP_ROLE_UNKNOWN;
static volatile drp_role_t pending_role = DRP_ROLE_UNKNOWN;

static volatile uint8_t event_flag = 0;
static uint32_t debounce_start = 0;

void drp_init(void)
{
    state = DRP_WAIT_STABLE;
    current_role = DRP_ROLE_UNKNOWN;
    pending_role = DRP_ROLE_UNKNOWN;
    event_flag = 0;

    LL_UCPD_ClearFlag_ALL(UCPD1);

    LL_UCPD_EnableIT_TYPEC_EVENT(CC1, UCPD1);
    LL_UCPD_EnableIT_TYPEC_EVENT(CC2, UCPD1);

    LL_UCPD_SetDataRole(UCPD1, LL_UCPD_ROLE_DRP);
    LL_UCPD_Enable(UCPD1);
    LL_UCPD_Start(UCPD1);
}

drp_role_t drp_get_role(void)
{
    return current_role;
}

uint8_t drp_is_ready(void)
{
    return current_role != DRP_ROLE_UNKNOWN;
}

static drp_role_t drp_get_role_from_hw(void)
{
    if (LL_UCPD_IsActiveFlag_CC1(UCPD1))
        return DRP_ROLE_DEVICE;

    if (LL_UCPD_IsActiveFlag_CC2(UCPD1))
        return DRP_ROLE_HOST;

    return current_role;
}

static void drp_apply_role(drp_role_t role)
{
    if (role == current_role)
        return;

    current_role = role;
    state = DRP_ATTACHED;

    usb_manager_stop();

    for (volatile int i = 0; i < 200000; i++) __NOP();

    if (role == DRP_ROLE_DEVICE)
    {
        usb_hw_enable_device();
        usb_manager_start_device();
    }
    else if (role == DRP_ROLE_HOST)
    {
        usb_hw_enable_host();
        usb_manager_start_host();
    }
}

void drp_irq_handler(void)
{
    LL_UCPD_ClearFlag_TYPEC_EVENT(CC1, UCPD1);
    LL_UCPD_ClearFlag_TYPEC_EVENT(CC2, UCPD1);
    event_flag = 1;
}

void drp_task(void)
{
    if (!event_flag)
        return;

    event_flag = 0;

    drp_role_t role = drp_get_role_from_hw();

    if (role != pending_role)
    {
        pending_role = role;
        debounce_start = HAL_GetTick();
        state = DRP_WAIT_STABLE;
        return;
    }

    if (HAL_GetTick() - debounce_start < 100)
        return;

    drp_apply_role(role);
}