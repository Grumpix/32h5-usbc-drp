#include "usb_manager.h"
#include "main.h"
#include "tusb.h"
#include "uart.h"
#include "usb_host_tusb.h"
#include "usb_hw.h"
#include <stdbool.h>

/* =========================
   state
   ========================= */

static volatile usb_mode_t current_mode = USB_MODE_NONE;
static volatile uint8_t running = 0;
static volatile uint8_t host_ready = 0;
static volatile uint8_t error_state = 0;
static uint32_t host_init_started_ms = 0;

typedef enum
{
    USB_LED_OFF = 0,
    USB_LED_DEVICE_SLOW,
    USB_LED_HOST_INIT_FAST,
    USB_LED_HOST_READY,
    USB_LED_ERROR_DOUBLE
} usb_led_pattern_t;

static usb_led_pattern_t led_pattern = USB_LED_OFF;

static void usb_manager_log_mode(usb_mode_t mode)
{
    if (mode == USB_MODE_HOST)
    {
        uart_write_str("[USB] HOST mode\r\n");
    }
    else if (mode == USB_MODE_DEVICE)
    {
        uart_write_str("[USB] DEVICE mode\r\n");
    }
}

/* =========================
   LED handling
   ========================= */

static void usb_manager_update_led(void)
{
    uint32_t tick = HAL_GetTick();

    switch (led_pattern)
    {
        case USB_LED_OFF:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
            break;

        case USB_LED_DEVICE_SLOW:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                              ((tick / 500U) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        case USB_LED_HOST_INIT_FAST:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                              ((tick / 100U) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;

        case USB_LED_HOST_READY:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
            break;

        case USB_LED_ERROR_DOUBLE:
        {
            uint32_t phase = tick % 1000U;
            GPIO_PinState level = GPIO_PIN_RESET;

            if ((phase < 80U) || ((phase >= 180U) && (phase < 260U)))
            {
                level = GPIO_PIN_SET;
            }

            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, level);
            break;
        }
    }
}

/* =========================
   error handling
   ========================= */

static void usb_manager_set_error(void)
{
    running = 0;
    host_ready = 0;
    error_state = 1;
    current_mode = USB_MODE_NONE;
    led_pattern = USB_LED_ERROR_DOUBLE;
    uart_write_str("[USB] ERROR\r\n");
}

/* =========================
   start mode
   ========================= */

static bool usb_manager_start_mode(usb_mode_t mode)
{
    tusb_rhport_init_t rh_init = {
        .role = (mode == USB_MODE_HOST) ? TUSB_ROLE_HOST : TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    error_state = 0;

    if (mode == USB_MODE_DEVICE)
    {
        usb_hw_enable_device();

        if (!tusb_rhport_init(0, &rh_init))
        {
            usb_manager_set_error();
            return false;
        }

        (void) tud_connect();

        current_mode = USB_MODE_DEVICE;
        running = 1;
        host_ready = 0;
        led_pattern = USB_LED_DEVICE_SLOW;
        usb_manager_log_mode(current_mode);
        return true;
    }

    /* HOST */
    usb_hw_enable_host();
    usb_host_tusb_init();

    if (!tusb_rhport_init(0, &rh_init))
    {
        usb_host_tusb_deinit();
        usb_manager_set_error();
        return false;
    }

    current_mode = USB_MODE_HOST;
    running = 1;
    host_ready = 0;
    host_init_started_ms = HAL_GetTick();
    led_pattern = USB_LED_HOST_INIT_FAST;
    usb_manager_log_mode(current_mode);

    return true;
}

/* =========================
   public API
   ========================= */

void usb_manager_init(void)
{
    current_mode = USB_MODE_NONE;
    running = 0;
    host_ready = 0;
    error_state = 0;
    led_pattern = USB_LED_OFF;
}

void usb_manager_stop(void)
{
    if (running)
    {
        if (current_mode == USB_MODE_DEVICE)
        {
            (void) tud_disconnect();
        }

        if (current_mode == USB_MODE_HOST)
        {
            usb_host_tusb_deinit();
        }

        (void) tusb_deinit(0);
        usb_hw_reset_peripheral();
        usb_hw_deinit();
    }

    running = 0;
    host_ready = 0;
    error_state = 0;
    current_mode = USB_MODE_NONE;
    led_pattern = USB_LED_OFF;
}

bool usb_manager_switch_mode(usb_mode_t mode)
{
    if (mode == USB_MODE_NONE)
    {
        usb_manager_stop();
        return true;
    }

    if ((running != 0U) && (current_mode == mode) && (error_state == 0U))
    {
        return true;
    }

    usb_manager_stop();
    HAL_Delay(2);

    return usb_manager_start_mode(mode);
}

bool usb_manager_start_device(void)
{
    return usb_manager_switch_mode(USB_MODE_DEVICE);
}

bool usb_manager_start_host(void)
{
    return usb_manager_switch_mode(USB_MODE_HOST);
}

bool usb_manager_toggle_mode(void)
{
    usb_mode_t next_mode =
        (current_mode == USB_MODE_HOST) ? USB_MODE_DEVICE : USB_MODE_HOST;

    return usb_manager_switch_mode(next_mode);
}

/* =========================
   main task
   ========================= */

void usb_manager_task(void)
{
    if (running != 0U)
    {
        if (current_mode == USB_MODE_DEVICE)
        {
            tud_task_ext(0, false);
        }
        else if (current_mode == USB_MODE_HOST)
        {
            tuh_task_ext(0, false);

            if ((host_ready == 0U) && tuh_rhport_is_active(0) &&
                ((HAL_GetTick() - host_init_started_ms) >= 200U))
            {
                host_ready = 1;
                led_pattern = USB_LED_HOST_READY;
            }
        }
    }

    usb_manager_update_led();
}

usb_mode_t usb_manager_get_mode(void)
{
    return current_mode;
}

bool usb_manager_is_host_ready(void)
{
    return (host_ready != 0U);
}