#include "drp_fsm.h"

#include "main.h"
#include "typec.h"
#include "uart_log.h"
#include "usb_manager.h"

typedef enum
{
    DRP_STATE_UNATTACHED = 0,
    DRP_STATE_DRP_TOGGLE,
    DRP_STATE_ATTACH_DETECTED,
    DRP_STATE_ATTACHED_HOST,
    DRP_STATE_ATTACHED_DEVICE,
    DRP_STATE_ERROR
} drp_state_t;

#define DRP_TOGGLE_MS      75U
#define DRP_CC_DEBOUNCE_MS 120U
#define DRP_VBUS_STABLE_MS 100U

static volatile drp_role_t current_role = DRP_ROLE_UNKNOWN;
static volatile drp_role_t requested_role = DRP_ROLE_UNKNOWN;
static volatile uint8_t drp_ready = 0U;

static drp_state_t current_state = DRP_STATE_UNATTACHED;
static typec_state_t stable_typec_state = TYPEC_STATE_UNATTACHED;
static typec_state_t candidate_typec_state = TYPEC_STATE_UNATTACHED;
static typec_cc_orientation_t stable_orientation = TYPEC_ORIENTATION_NONE;
static typec_cc_orientation_t candidate_orientation = TYPEC_ORIENTATION_NONE;
static typec_role_t toggle_role = TYPEC_ROLE_SINK;
static drp_role_t latched_role = DRP_ROLE_UNKNOWN;
static uint8_t usb_mode_requested = 0U;
static uint32_t candidate_since_ms = 0U;
static uint32_t state_since_ms = 0U;

static void drp_set_led(void)
{
    uint32_t tick = HAL_GetTick();
    GPIO_PinState level = GPIO_PIN_RESET;

    switch (current_state)
    {
        case DRP_STATE_ATTACHED_HOST:
            level = GPIO_PIN_SET;
            break;

        case DRP_STATE_ATTACH_DETECTED:
            level = ((tick / 100U) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
            break;

        case DRP_STATE_ATTACHED_DEVICE:
            level = ((tick / 500U) & 0x1U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
            break;

        case DRP_STATE_ERROR:
        {
            uint32_t phase = tick % 1000U;

            if ((phase < 80U) || ((phase >= 180U) && (phase < 260U)))
            {
                level = GPIO_PIN_SET;
            }
            break;
        }

        case DRP_STATE_UNATTACHED:
        case DRP_STATE_DRP_TOGGLE:
        default:
            level = ((tick % 1000U) < 60U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
            break;
    }

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, level);
}

static void drp_log_mode(typec_role_t role)
{
    if (role == TYPEC_ROLE_SOURCE)
    {
        uart_log_write("DRP -> SOURCE\r\n");
    }
    else
    {
        uart_log_write("DRP -> SINK\r\n");
    }
}

static void drp_reset_debounce(typec_state_t state, typec_cc_orientation_t orientation, uint32_t now)
{
    stable_typec_state = state;
    candidate_typec_state = state;
    stable_orientation = orientation;
    candidate_orientation = orientation;
    candidate_since_ms = now;
}

static void drp_start_toggle(typec_role_t role, uint32_t now)
{
    typec_set_role(role);
    drp_log_mode(role);
    drp_reset_debounce(TYPEC_STATE_UNATTACHED, TYPEC_ORIENTATION_NONE, now);

    toggle_role = role;
    latched_role = DRP_ROLE_UNKNOWN;
    usb_mode_requested = 0U;
    current_state = DRP_STATE_DRP_TOGGLE;
    state_since_ms = now;
}

static void drp_set_unattached(uint32_t now)
{
    current_role = DRP_ROLE_UNKNOWN;
    requested_role = DRP_ROLE_UNKNOWN;
    latched_role = DRP_ROLE_UNKNOWN;
    usb_mode_requested = 0U;
    current_state = DRP_STATE_UNATTACHED;
    drp_reset_debounce(TYPEC_STATE_UNATTACHED, TYPEC_ORIENTATION_NONE, now);
    state_since_ms = now;
}

static bool drp_candidate_matches_toggle(void)
{
    return ((toggle_role == TYPEC_ROLE_SOURCE) && (candidate_typec_state == TYPEC_STATE_ATTACHED_SOURCE)) ||
           ((toggle_role == TYPEC_ROLE_SINK) && (candidate_typec_state == TYPEC_STATE_ATTACHED_SINK));
}

static bool drp_stable_matches_latched(void)
{
    return ((latched_role == DRP_ROLE_HOST) && (stable_typec_state == TYPEC_STATE_ATTACHED_SOURCE)) ||
           ((latched_role == DRP_ROLE_DEVICE) && (stable_typec_state == TYPEC_STATE_ATTACHED_SINK));
}

static void drp_begin_attach_detected(uint32_t now)
{
    latched_role = (toggle_role == TYPEC_ROLE_SOURCE) ? DRP_ROLE_HOST : DRP_ROLE_DEVICE;
    current_state = DRP_STATE_ATTACH_DETECTED;
    state_since_ms = now;
}

static void drp_enter_error(char const *message)
{
    (void) usb_manager_request_mode(USB_STATE_OFF);

    if (typec_vbus_is_enabled())
    {
        typec_vbus_enable(false);
        uart_log_write("VBUS OFF\r\n");
    }

    current_role = DRP_ROLE_UNKNOWN;
    requested_role = DRP_ROLE_UNKNOWN;
    current_state = DRP_STATE_ERROR;
    uart_log_write(message);
}

static void drp_handle_detach(uint32_t now)
{
    (void) usb_manager_request_mode(USB_STATE_OFF);

    if (typec_vbus_is_enabled())
    {
        typec_vbus_enable(false);
        uart_log_write("VBUS OFF\r\n");
    }

    uart_log_write("DETACHED\r\n");
    drp_set_unattached(now);
}

static void drp_select_device(uint32_t now)
{
    current_role = DRP_ROLE_DEVICE;
    requested_role = DRP_ROLE_DEVICE;
    latched_role = DRP_ROLE_DEVICE;
    usb_mode_requested = 0U;
    uart_log_write("DEVICE SELECTED\r\n");

    if (typec_vbus_is_enabled())
    {
        typec_vbus_enable(false);
        uart_log_write("VBUS OFF\r\n");
    }

    current_state = DRP_STATE_ATTACHED_DEVICE;
    state_since_ms = now;
}

static void drp_select_host(uint32_t now)
{
    current_role = DRP_ROLE_HOST;
    requested_role = DRP_ROLE_HOST;
    latched_role = DRP_ROLE_HOST;
    usb_mode_requested = 0U;
    uart_log_write("HOST SELECTED\r\n");
    typec_vbus_enable(true);
    uart_log_write("VBUS ON\r\n");
    current_state = DRP_STATE_ATTACHED_HOST;
    state_since_ms = now;
}

static void drp_update_typec_sample(uint32_t now)
{
    typec_state_t sampled_state = typec_get_state();
    typec_cc_orientation_t sampled_orientation = typec_get_cc_orientation();

    if ((sampled_state != candidate_typec_state) || (sampled_orientation != candidate_orientation))
    {
        candidate_typec_state = sampled_state;
        candidate_orientation = sampled_orientation;
        candidate_since_ms = now;
        return;
    }

    if ((sampled_state != stable_typec_state) || (sampled_orientation != stable_orientation))
    {
        if ((now - candidate_since_ms) >= DRP_CC_DEBOUNCE_MS)
        {
            stable_typec_state = sampled_state;
            stable_orientation = sampled_orientation;
        }
    }
}

void drp_init(void)
{
    uint32_t now = HAL_GetTick();

    typec_init();
    typec_vbus_enable(false);

    current_role = DRP_ROLE_UNKNOWN;
    requested_role = DRP_ROLE_UNKNOWN;
    current_state = DRP_STATE_UNATTACHED;
    drp_reset_debounce(TYPEC_STATE_UNATTACHED, TYPEC_ORIENTATION_NONE, now);
    state_since_ms = now;
    drp_ready = 1U;
    uart_log_write("DRP START\r\n");
}

drp_role_t drp_get_role(void)
{
    return current_role;
}

void drp_request_role(drp_role_t role)
{
    requested_role = role;
}

uint8_t drp_is_ready(void)
{
    return drp_ready;
}

void drp_irq_handler(void)
{
    typec_irq_handler();
}

void drp_task(void)
{
    uint32_t now;

    if (drp_ready == 0U)
    {
        return;
    }

    now = HAL_GetTick();
    drp_update_typec_sample(now);

    switch (current_state)
    {
        case DRP_STATE_UNATTACHED:
            drp_start_toggle(TYPEC_ROLE_SINK, now);
            break;

        case DRP_STATE_DRP_TOGGLE:
            if (candidate_typec_state == TYPEC_STATE_INVALID)
            {
                drp_enter_error("CC INVALID\r\n");
            }
            else if (drp_candidate_matches_toggle())
            {
                drp_begin_attach_detected(now);
            }
            else if ((candidate_typec_state == TYPEC_STATE_UNATTACHED) &&
                     ((now - state_since_ms) >= DRP_TOGGLE_MS))
            {
                drp_start_toggle((toggle_role == TYPEC_ROLE_SINK) ? TYPEC_ROLE_SOURCE : TYPEC_ROLE_SINK, now);
            }
            break;

        case DRP_STATE_ATTACH_DETECTED:
            if (candidate_typec_state == TYPEC_STATE_INVALID)
            {
                drp_enter_error("CC INVALID\r\n");
            }
            else if (candidate_typec_state == TYPEC_STATE_UNATTACHED)
            {
                drp_set_unattached(now);
            }
            else if (drp_stable_matches_latched())
            {
                if (latched_role == DRP_ROLE_HOST)
                {
                    drp_select_host(now);
                }
                else if (latched_role == DRP_ROLE_DEVICE)
                {
                    drp_select_device(now);
                }
            }
            break;

        case DRP_STATE_ATTACHED_HOST:
            if (stable_typec_state != TYPEC_STATE_ATTACHED_SOURCE)
            {
                drp_handle_detach(now);
            }
            else if ((usb_mode_requested == 0U) && ((now - state_since_ms) >= DRP_VBUS_STABLE_MS))
            {
                if (usb_manager_request_mode(USB_STATE_HOST))
                {
                    usb_mode_requested = 1U;
                    uart_log_write("HOST READY\r\n");
                }
                else
                {
                    drp_enter_error("HOST INIT FAILED\r\n");
                }
            }
            break;

        case DRP_STATE_ATTACHED_DEVICE:
            if (stable_typec_state != TYPEC_STATE_ATTACHED_SINK)
            {
                drp_handle_detach(now);
            }
            else if (usb_mode_requested == 0U)
            {
                if (usb_manager_request_mode(USB_STATE_DEVICE))
                {
                    usb_mode_requested = 1U;
                    uart_log_write("DEVICE READY\r\n");
                }
                else
                {
                    drp_enter_error("DEVICE INIT FAILED\r\n");
                }
            }
            break;

        case DRP_STATE_ERROR:
            if (stable_typec_state == TYPEC_STATE_UNATTACHED)
            {
                drp_handle_detach(now);
            }
            break;
    }

    drp_set_led();
}