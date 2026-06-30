#include "usb_debug.h"
#include "main.h"

static volatile usb_dbg_state_t dbg_state = USB_DBG_DISCONNECTED;

/* =========================
   INIT
========================= */

void usb_debug_init(void)
{
    dbg_state = USB_DBG_DISCONNECTED;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

/* =========================
   SET STATE
========================= */

void usb_debug_set_state(usb_dbg_state_t state)
{
    dbg_state = state;

    switch (state)
    {
        case USB_DBG_DISCONNECTED:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            break;

        case USB_DBG_ATTACHED:
            /* steady low indication (not toggle spam) */
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            break;

        case USB_DBG_POWERED:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;

        case USB_DBG_ENUMERATED:
            /* short blink, non-blocking style */
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            break;

        case USB_DBG_ERROR:
            /* IMPORTANT: no blocking loop */
            /* just force visible state */
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;
    }
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