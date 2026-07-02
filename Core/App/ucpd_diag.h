#pragma once

#include <stdint.h>

typedef enum
{
    UCPD_DISCONNECTED = 0,
    UCPD_CC1_ATTACHED,
    UCPD_CC2_ATTACHED,
    UCPD_ATTACHED
} ucpd_state_t;

void ucpd_diag_init(void);
void ucpd_diag_irq(void);
void ucpd_diag_task(void);

ucpd_state_t ucpd_diag_get_state(void);
uint8_t ucpd_diag_get_vbus(void);