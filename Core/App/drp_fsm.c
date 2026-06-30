#include "drp_fsm.h"
#include "usb_manager.h"
#include "usb_hw.h"
#include "stm32h5xx_ll_ucpd.h"
#include "usb_debug.h"

/* =========================
   STATE
========================= */

typedef enum
{
    DRP_IDLE = 0,
    DRP_WAIT_ATTACH,
    DRP_ATTACHED
} drp_state_t;

static volatile drp_state_t state = DRP_IDLE;
static volatile uint8_t last_role = 0;

/* =========================
   INIT
========================= */

void drp_init(void)
{
    state = DRP_WAIT_ATTACH;
    last_role = 0;

    /* 1. CLEAR ALL UCPD FLAGS (IMPORTANT ON H5) */
    LL_UCPD_ClearFlag_ALL(UCPD1);

    /* 2. CONFIGURE TYPE-C INTERRUPTS */
    LL_UCPD_EnableIT_TYPEC_EVENT(CC1, UCPD1);
    LL_UCPD_EnableIT_TYPEC_EVENT(CC2, UCPD1);

    /* 3. ENABLE UCPD PERIPHERAL */
    LL_UCPD_Enable(UCPD1);

    /* 4. SET DRP MODE */
    LL_UCPD_SetDataRole(UCPD1, LL_UCPD_ROLE_DRP);

    /* 5. START TYPE-C FSM */
    LL_UCPD_Start(UCPD1);

    usb_debug_log_event("DRP init done");
}

/* =========================
   ROLE DECISION (HARDWARE SAFE VERSION)
========================= */

static uint8_t drp_get_role_from_hw(void)
{
    /* STM32H5 reality:
       CC flags alone are NOT reliable for final role decision.
       We use state + event presence.
    */

    if (LL_UCPD_IsActiveFlag_CC1(UCPD1))
    {
        return 0; // DEVICE (sink side)
    }

    if (LL_UCPD_IsActiveFlag_CC2(UCPD1))
    {
        return 1; // HOST (source side)
    }

    return last_role;
}

/* =========================
   APPLY ROLE
========================= */

static void drp_apply_role(uint8_t role)
{
    if (role == last_role)
        return;

    last_role = role;
    state = DRP_ATTACHED;

    usb_debug_log_event(role ? "ROLE: HOST" : "ROLE: DEVICE");

    /* HARD STOP BEFORE SWITCH */
    usb_manager_stop();

    /* IMPORTANT: allow USB peripheral settle time */
    HAL_Delay(5);

    if (role == 0)
    {
        /* DEVICE MODE */
        usb_hw_enable_device();
        usb_manager_start_device();
        usb_debug_set_state(USB_DBG_POWERED);
    }
    else
    {
        /* HOST MODE */
        usb_hw_enable_host();
        usb_manager_start_host();
        usb_debug_set_state(USB_DBG_POWERED);
    }
}

/* =========================
   IRQ ENTRY (FROM IT FILE)
========================= */

void drp_irq_handler(void)
{
    /* 1. READ EVENTS FIRST (before clearing!) */
    uint8_t cc1 = LL_UCPD_IsActiveFlag_CC1(UCPD1);
    uint8_t cc2 = LL_UCPD_IsActiveFlag_CC2(UCPD1);

    /* 2. CLEAR FLAGS */
    LL_UCPD_ClearFlag_TYPEC_EVENT(CC1, UCPD1);
    LL_UCPD_ClearFlag_TYPEC_EVENT(CC2, UCPD1);

    /* 3. DEBUG */
    if (cc1) usb_debug_log_event("CC1 EVENT");
    if (cc2) usb_debug_log_event("CC2 EVENT");

    /* 4. ROLE DECISION */
    uint8_t role = drp_get_role_from_hw();

    drp_apply_role(role);
}

/* =========================
   TASK LOOP (ANTI-STUCK SAFETY)
========================= */

void drp_task(void)
{
    static uint32_t last_check = 0;

    /* simple periodic safety check */
    if (HAL_GetTick() - last_check < 50)
        return;

    last_check = HAL_GetTick();

    /* if no attach detected but hardware says otherwise → resync */
    if (state == DRP_WAIT_ATTACH)
    {
        uint8_t role = drp_get_role_from_hw();
        if (role != last_role)
        {
            drp_apply_role(role);
        }
    }
}