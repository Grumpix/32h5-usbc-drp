#pragma once
#include <stdint.h>

typedef enum {
    USB_MODE_NONE = 0,
    USB_MODE_DEVICE
} usb_mode_t;

void usb_manager_init(void);
void usb_manager_stop(void);
void usb_manager_start_device(void);
void usb_manager_task(void);
usb_mode_t usb_manager_get_mode(void);