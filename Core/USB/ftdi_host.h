#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ftdi_host_rx_callback_t)(
    const uint8_t *data,
    uint32_t len);

void ftdi_host_app_init(void);
void ftdi_host_task(void);

bool ftdi_host_is_mounted(void);
bool ftdi_host_is_ready(void);

bool ftdi_host_send(
    const uint8_t *data,
    uint32_t len);

void ftdi_host_set_rx_callback(
    ftdi_host_rx_callback_t callback);

#ifdef __cplusplus
}
#endif