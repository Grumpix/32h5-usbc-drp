#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ucpd_diag_init(void);
void ucpd_diag_task(void);
void ucpd_diag_irq(void);

uint8_t ucpd_diag_is_source(void);

/*
 * Read-only state getters for role policy.
 *
 * Tyhle funkce nic nemení. Jen vraci stav z ucpd_diag.c.
 */
uint8_t ucpd_diag_is_attached(void);
uint8_t ucpd_diag_usb_started(void);
uint8_t ucpd_diag_vbus_present(void);
uint8_t ucpd_diag_is_unattached(void);
void ucpd_diag_set_auto_scan_mode(uint8_t enable);
void ucpd_diag_request_device_role(void);
void ucpd_diag_request_host_role(void);
void ucpd_diag_toggle_role(void);

/*
 * Scan-role API for AUTO-DRP policy.
 *
 * Pouzivat jen z usb_role_policy.c.
 * Interně používá stejnou bezpečnou apply_role() logiku.
 */
void ucpd_diag_set_scan_sink_role(void);
void ucpd_diag_set_scan_source_role(void);

#ifdef __cplusplus
}
#endif