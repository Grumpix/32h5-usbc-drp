#pragma once

#include <stdbool.h>
void usb_host_tusb_task(void);
void usb_host_tusb_init(void);
void usb_host_tusb_deinit(void);
bool usb_host_tusb_is_device_attached(void);