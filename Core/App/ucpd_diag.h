#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ucpd_diag_init(void);
void ucpd_diag_task(void);
void ucpd_diag_irq(void);

uint8_t ucpd_diag_is_source(void);

void ucpd_diag_request_device_role(void);
void ucpd_diag_request_host_role(void);
void ucpd_diag_toggle_role(void);

#ifdef __cplusplus
}
#endif