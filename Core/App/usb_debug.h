#pragma once
#include <stdint.h>

typedef enum
{
    USB_DBG_DISCONNECTED = 0,
    USB_DBG_ATTACHED,
    USB_DBG_POWERED,
    USB_DBG_ENUMERATED,
    USB_DBG_ERROR
} usb_dbg_state_t;

void usb_debug_init(void);
void usb_debug_set_state(usb_dbg_state_t state);
usb_dbg_state_t usb_debug_get_state(void);

void usb_debug_log_event(const char *msg);