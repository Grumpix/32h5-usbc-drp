#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    USB_STATE_OFF = 0,
    USB_STATE_DEVICE,
    USB_STATE_HOST,
    USB_STATE_SWITCHING
} usb_state_t;

void usb_manager_init(void);
void usb_manager_stop(void);
bool usb_manager_start_device(void);
bool usb_manager_start_host(void);
bool usb_manager_request_mode(usb_state_t state);
bool usb_manager_switch_mode(usb_state_t state);
bool usb_manager_toggle_mode(void);
void usb_manager_task(void);
usb_state_t usb_manager_get_state(void);
bool usb_manager_is_host_ready(void);
void usb_manager_host_attached(uint8_t daddr);
void usb_manager_host_removed(uint8_t daddr);