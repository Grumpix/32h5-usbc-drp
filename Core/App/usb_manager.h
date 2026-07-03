#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    USB_MODE_NONE,
    USB_MODE_DEVICE,
    USB_MODE_HOST
} usb_mode_t;

void usb_manager_init(void);
void usb_manager_stop(void);
bool usb_manager_start_device(void);
bool usb_manager_start_host(void);
bool usb_manager_switch_mode(usb_mode_t mode);
bool usb_manager_toggle_mode(void);
void usb_manager_task(void);
usb_mode_t usb_manager_get_mode(void);
bool usb_manager_is_host_ready(void);