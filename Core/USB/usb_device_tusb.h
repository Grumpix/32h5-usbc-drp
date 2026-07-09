#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_device_tusb_init(void);
void usb_device_tusb_task(void);
void usb_device_tusb_deinit(void);

bool usb_device_tusb_is_mounted(void);
bool usb_device_tusb_is_cdc_connected(void);

#ifdef __cplusplus
}
#endif