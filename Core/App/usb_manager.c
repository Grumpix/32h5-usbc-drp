#include "usb_manager.h"
#include "main.h"
#include "tusb.h"
#include "usb_hw.h"

static volatile usb_mode_t current_mode = USB_MODE_NONE;
static volatile uint8_t running = 0;

void usb_manager_init(void)
{
    current_mode = USB_MODE_NONE;
    running = 0;
}

void usb_manager_stop(void)
{
    if (running)
    {
        (void) tud_disconnect();
        usb_hw_deinit();
    }

    running = 0;
    current_mode = USB_MODE_NONE;

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
}

static void start_device(void)
{
    usb_hw_enable_device();

    (void) tusb_init(0);
    (void) tud_connect();

    current_mode = USB_MODE_DEVICE;
    running = 1;

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
}

void usb_manager_start_device(void)
{
    if (current_mode == USB_MODE_DEVICE) return;

    usb_manager_stop();
    HAL_Delay(2);

    start_device();
}

void usb_manager_task(void)
{
    if (!running) return;

    tud_task();
}

usb_mode_t usb_manager_get_mode(void)
{
    return current_mode;
}