#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void app_cli_transport_init(void);
void app_cli_transport_task(void);

void app_cli_transport_cdc_rx(
    const uint8_t *data,
    uint32_t len);

#ifdef __cplusplus
}
#endif