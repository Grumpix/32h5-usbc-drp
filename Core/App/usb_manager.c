#include "usb_manager.h"
#include "main.h"
#include "tusb.h"
#include "uart_log.h"
#include "usb_host_tusb.h"
#include "usb_hw.h"

static volatile usb_state_t current_state = USB_STATE_OFF;
static volatile uint8_t host_stack_ready = 0;
static volatile uint8_t host_device_attached = 0;
static volatile uint8_t error_state = 0;

static void usb_manager_update_led(void);

typedef enum
{
    USB_LED_OFF = 0,
    USB_LED_DEVICE_SLOW,
    USB_LED_SWITCHING,
    USB_LED_HOST_INIT_FAST,
    USB_LED_HOST_READY,
    USB_LED_ERROR_DOUBLE
} usb_led_pattern_t;

static usb_led_pattern_t led_pattern = USB_LED_OFF;

static void usb_manager_wait_while_switching(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < duration_ms)
    {
        usb_manager_update_led();
        HAL_Delay(10);
    }
}

static void usb_manager_set_led_from_state(void)
{
    if (error_state != 0U)
    {
        led_pattern = USB_LED_ERROR_DOUBLE;
        return;
    }

    switch (current_state)
    {
        case USB_STATE_OFF:
            led_pattern = USB_LED_OFF;
            break;

        case USB_STATE_DEVICE:
            led_pattern = USB_LED_DEVICE_SLOW;
            break;

        case USB_STATE_SWITCHING:
            led_pattern = USB_LED_SWITCHING;
            break;

        case USB_STATE_HOST:
            led_pattern = host_stack_ready ? USB_LED_HOST_READY : USB_LED_HOST_INIT_FAST;
            break;
    }
}

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

        case USB_LED_SWITCHING:
            HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin,
                              ((tick / 75U) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
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

static void usb_manager_set_error(void)
{
    host_stack_ready = 0;
    host_device_attached = 0;
    error_state = 1;
    current_state = USB_STATE_OFF;
    usb_manager_set_led_from_state();
}

static void usb_stop_all(void)
{
    usb_state_t previous_state = current_state;
    bool stack_was_initialized = tusb_inited();

    if (previous_state == USB_STATE_DEVICE)
    {
        (void) tud_disconnect();
        usb_manager_wait_while_switching(20);
    }

    if (previous_state == USB_STATE_HOST)
    {
        usb_host_tusb_deinit();
    }

    if (stack_was_initialized)
    {
        (void) tusb_deinit(0);
        usb_hw_deinit();
    }

    host_stack_ready = 0;
    host_device_attached = 0;
    error_state = 0;
    current_state = USB_STATE_OFF;
    usb_manager_set_led_from_state();
}

static bool usb_device_init(void)
{
    tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL
    };

    usb_hw_enable_device();

    if (!tusb_init(0, &rh_init))
    {
        uart_log_write("DEVICE INIT FAILED\r\n");
        usb_hw_deinit();
        usb_manager_set_error();
        return false;
    }

    current_state = USB_STATE_DEVICE;
    host_stack_ready = 0;
    host_device_attached = 0;
    usb_manager_set_led_from_state();
    usb_hw_irq_enable();

    (void) tud_connect();
    uart_log_write("DEVICE READY\r\n");
    return true;
}

static bool usb_host_init(void)
{
    tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL
    };
    tuh_configure_param_t host_cfg = {
        .fsdev = {
            .max_nak = 8
        }
    };

    usb_host_tusb_log("HOST INIT START\r\n");
    usb_hw_enable_host();
    usb_manager_wait_while_switching(100);

    usb_host_tusb_init();
    (void) tuh_configure(0, TUH_CFGID_FSDEV, &host_cfg);

    if (!tusb_init(0, &rh_init))
    {
        usb_host_tusb_log("HOST INIT FAILED\r\n");
        usb_host_tusb_deinit();
        usb_hw_deinit();
        usb_manager_set_error();
        return false;
    }

    current_state = USB_STATE_HOST;
    host_stack_ready = 1;
    host_device_attached = 0;
    usb_manager_set_led_from_state();
    usb_hw_irq_enable();
    usb_host_tusb_log("HOST READY\r\n");
    return true;
}

static bool usb_switch_mode_internal(usb_state_t next_state)
{
    host_stack_ready = 0;
    host_device_attached = 0;
    error_state = 0;

    usb_stop_all();
    current_state = USB_STATE_SWITCHING;
    usb_manager_set_led_from_state();
    usb_manager_wait_while_switching(20);

    if (next_state == USB_STATE_DEVICE)
    {
        return usb_device_init();
    }

    if (next_state == USB_STATE_HOST)
    {
        return usb_host_init();
    }

    current_state = USB_STATE_OFF;
    usb_manager_set_led_from_state();
    return true;
}

void usb_manager_init(void)
{
    current_state = USB_STATE_OFF;
    host_stack_ready = 0;
    host_device_attached = 0;
    error_state = 0;
    usb_manager_set_led_from_state();
}

void usb_manager_stop(void)
{
    current_state = USB_STATE_SWITCHING;
    usb_manager_set_led_from_state();
    usb_stop_all();
}

bool usb_manager_switch_mode(usb_state_t state)
{
    if (state == USB_STATE_SWITCHING)
    {
        return false;
    }

    if (state == USB_STATE_OFF)
    {
        usb_manager_stop();
        return true;
    }

    if ((current_state == state) && (error_state == 0U))
    {
        return true;
    }

    return usb_switch_mode_internal(state);
}

bool usb_manager_start_device(void)
{
    return usb_manager_switch_mode(USB_STATE_DEVICE);
}

bool usb_manager_start_host(void)
{
    return usb_manager_switch_mode(USB_STATE_HOST);
}

bool usb_manager_request_mode(usb_state_t state)
{
    return usb_manager_switch_mode(state);
}

bool usb_manager_toggle_mode(void)
{
    usb_state_t next_state = (current_state == USB_STATE_HOST) ? USB_STATE_DEVICE : USB_STATE_HOST;

    return usb_manager_switch_mode(next_state);
}

void usb_manager_task(void)
{
    switch (current_state)
    {
        case USB_STATE_DEVICE:
            tud_task_ext(0, false);
            break;

        case USB_STATE_HOST:
            tuh_task_ext(0, false);
            break;

        case USB_STATE_OFF:
        case USB_STATE_SWITCHING:
            break;
    }

    usb_manager_update_led();
}

usb_state_t usb_manager_get_state(void)
{
    return current_state;
}

bool usb_manager_is_host_ready(void)
{
    return (host_stack_ready != 0U);
}

void usb_manager_host_attached(uint8_t daddr)
{
    (void) daddr;

    if (current_state == USB_STATE_HOST)
    {
        host_device_attached = 1;
        usb_manager_set_led_from_state();
    }
}

void usb_manager_host_removed(uint8_t daddr)
{
    (void) daddr;

    if (current_state == USB_STATE_HOST)
    {
        host_device_attached = 0;
        usb_manager_set_led_from_state();
    }
}