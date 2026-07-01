#include "drp_fsm.h"
#include "main.h"

static volatile drp_role_t current_role = DRP_ROLE_DEVICE;
static volatile drp_role_t requested_role = DRP_ROLE_DEVICE;

void drp_init(void)
{
    current_role = DRP_ROLE_DEVICE;
    requested_role = DRP_ROLE_DEVICE;
}

drp_role_t drp_get_role(void)
{
    return requested_role;
}

void drp_request_role(drp_role_t role)
{
    requested_role = role;
    current_role = role;
}

uint8_t drp_is_ready(void)
{
    return 1U;
}

void drp_irq_handler(void)
{
    /* DRP is stubbed in the CDC-only phase. */
}

void drp_task(void)
{
    /* Future hook point: role detection only, no USB control. */
}