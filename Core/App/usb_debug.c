#include "usb_debug.h"
#include "main.h"

static volatile usb_dbg_state_t dbg_state = USB_DBG_DISCONNECTED;

/* simple LED debug (no UART dependency yet) */

void usb_debug_init(void)
{
    dbg_state = USB_DBG_DISCONNECTED;
    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

void usb_debug_set_state(usb_dbg_state_t state)
{
    dbg_state = state;

    switch (state)
    {
        case USB_DBG_DISCONNECTED:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            break;

        case USB_DBG_ATTACHED:
            HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
            break;

        case USB_DBG_POWERED:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;

        case USB_DBG_ENUMERATED:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;

        case USB_DBG_ERROR:
            /* fast blink (simple error signal) */
            for (int i = 0; i < 3; i++)
            {
                HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
                HAL_Delay(100);
            }
            break;
    }
}

usb_dbg_state_t usb_debug_get_state(void)
{
    return dbg_state;
}

void usb_debug_log_event(const char *msg)
{
    (void)msg;
    /* later: UART or SWO trace */
}