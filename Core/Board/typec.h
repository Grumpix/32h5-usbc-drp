#pragma once

#include <stdbool.h>

typedef enum
{
    TYPEC_STATE_UNATTACHED = 0,
    TYPEC_STATE_ATTACHED_SINK,
    TYPEC_STATE_ATTACHED_SOURCE,
    TYPEC_STATE_INVALID
} typec_state_t;

typedef enum
{
    TYPEC_ROLE_NONE = 0,
    TYPEC_ROLE_SINK,
    TYPEC_ROLE_SOURCE
} typec_role_t;

typedef enum
{
    TYPEC_ORIENTATION_NONE = 0,
    TYPEC_ORIENTATION_CC1,
    TYPEC_ORIENTATION_CC2
} typec_cc_orientation_t;

typedef enum
{
    CC_NONE = 0,
    CC_RD,
    CC_RP
} cc_state_t;

void typec_init(void);
void typec_set_role(typec_role_t role);
typec_state_t typec_get_state(void);
cc_state_t typec_get_cc_state(void);
typec_role_t typec_get_role(void);
typec_cc_orientation_t typec_get_cc_orientation(void);
void typec_vbus_enable(bool enabled);
bool typec_vbus_is_enabled(void);
void typec_irq_handler(void);