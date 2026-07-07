#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ucpd_diag_init(void);
void ucpd_diag_task(void);
void ucpd_diag_irq(void);

uint8_t ucpd_diag_is_source(void);

#ifdef __cplusplus
}
#endif