#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_device_tusb_init(void);
void usb_device_tusb_task(void);
void usb_device_tusb_deinit(void);

bool usb_device_tusb_is_mounted(void);
bool usb_device_tusb_is_cdc_connected(void);

bool usb_device_tusb_cdc_send(
    const uint8_t *data,
    uint32_t len);

bool usb_device_tusb_cdc_send_str(
    const char *text);

#ifdef __cplusplus
}
#endif