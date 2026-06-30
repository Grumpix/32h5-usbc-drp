#include "usb_manager.h"
#include "main.h"
#include "drp_fsm.h"
#include "tusb.h"

/* TinyUSB headers (later when added) */
// #include "tusb.h"

static volatile usb_mode_t current_mode = USB_MODE_NONE;
static volatile uint8_t running = 0;

/* =========================
   INIT
========================= */

void usb_manager_init(void)
{
    current_mode = USB_MODE_NONE;
    running = 0;
}

/* =========================
   STOP USB COMPLETELY
========================= */

void usb_manager_stop(void)
{
    if (!running)
        return;

    /* TODO: TinyUSB deinit */
    // tud_deinit();
    // tuh_deinit();

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);

    running = 0;
    current_mode = USB_MODE_NONE;
}

/* =========================
   DEVICE MODE (CDC ACM)
========================= */

void usb_manager_start_device(void)
{
    if (current_mode == USB_MODE_DEVICE)
        return;

    usb_manager_stop();

    tusb_init();   // <<< KLÍČOVÉ

    current_mode = USB_MODE_DEVICE;
    running = 1;

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}
/* =========================
   HOST MODE (MSC)
========================= */

void usb_manager_start_host(void)
{
    if (current_mode == USB_MODE_HOST)
        return;

    usb_manager_stop();

    tusb_init();   // same init, TinyUSB decides role

    current_mode = USB_MODE_HOST;
    running = 1;

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

/* =========================
   TASK LOOP
========================= */

void usb_manager_task(void)
{
    if (!running)
        return;

    tud_task();   // device
    tuh_task();   // host
}

/* =========================
   GET STATE
========================= */

usb_mode_t usb_manager_get_mode(void)
{
    return current_mode;
}