#include "usb_debug.h"
#include "main.h"

static volatile usb_dbg_state_t dbg_state = USB_DBG_DISCONNECTED;

/* =========================
   INIT
========================= */

void usb_debug_init(void)
{
    dbg_state = USB_DBG_DISCONNECTED;
}

/* =========================
   SET STATE
========================= */

void usb_debug_set_state(usb_dbg_state_t state)
{
    dbg_state = state;
}

/* =========================
   GET STATE
========================= */

usb_dbg_state_t usb_debug_get_state(void)
{
    return dbg_state;
}

/* =========================
   LOG EVENT (stub)
========================= */

void usb_debug_log_event(const char *msg)
{
    (void)msg;
    /* future: UART / SWO */
}