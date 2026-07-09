#include "usb_role_policy.h"

#include "main.h"
#include "uart.h"
#include "ucpd_diag.h"
#include "usb_mode_button.h"

#include <stdint.h>


/*
 * USB ROLE POLICY
 *
 * Cilem tohoto souboru je oddelit:
 *
 * - nizkou Type-C/UCPD logiku: ucpd_diag.c
 * - USB stack start/stop: usb_manager.c
 * - rozhodovani "co chceme delat": usb_role_policy.c
 *
 * Defaultne stale MANUAL rezim pres PA0.
 */


/* =========================
   MODE CONFIG
========================= */

#define USB_ROLE_POLICY_MODE_MANUAL             0U
#define USB_ROLE_POLICY_MODE_AUTO_DRP_OBSERVE   1U
#define USB_ROLE_POLICY_MODE_AUTO_DRP           2U


/*
 * BEZPECNY DEFAULT:
 *
 * Manual mode = zadna zmena chovani proti known-good stavu.
 *
 * Pro observe test:
 *   USB_ROLE_POLICY_MODE_AUTO_DRP_OBSERVE
 *
 * Pro prvni realny AUTO-DRP scan:
 *   USB_ROLE_POLICY_MODE_AUTO_DRP
 */
#define USB_ROLE_POLICY_MODE                    USB_ROLE_POLICY_MODE_AUTO_DRP


/* =========================
   LOG CONFIG
========================= */

#define USB_ROLE_POLICY_LOG_BOOT                1U
#define USB_ROLE_POLICY_LOG_MANUAL              1U
#define USB_ROLE_POLICY_LOG_AUTO                1U
#define USB_ROLE_POLICY_LOG_OBSERVE             1U
#define USB_ROLE_POLICY_LOG_SCAN                1U


/* =========================
   Timing
========================= */

#define USB_ROLE_POLICY_OBSERVE_PERIOD_MS       1000U
#define USB_ROLE_POLICY_SCAN_WINDOW_MS          700U
#define USB_ROLE_POLICY_PC_VBUS_HOLD_MS         2500U


typedef enum
{
    USB_ROLE_SCAN_SINK = 0,
    USB_ROLE_SCAN_SOURCE
} usb_role_scan_slot_t;


static uint32_t observe_last_log_ms =
    0U;

static uint32_t scan_last_switch_ms =
    0U;

static usb_role_scan_slot_t scan_slot =
    USB_ROLE_SCAN_SINK;

static uint8_t scan_paused =
    0U;


/* =========================
   Small logging helpers
========================= */

static void policy_log(uint8_t enabled, const char *s)
{
    if(enabled)
    {
        uart_write_str(s);
    }
}


static void policy_write_dec_u32(uint32_t value)
{
    char buf[11];
    uint32_t i = 0U;

    if(value == 0U)
    {
        uart_write_str("0");
        return;
    }

    while((value > 0U) && (i < sizeof(buf)))
    {
        buf[i++] =
            (char)('0' + (value % 10U));

        value /=
            10U;
    }

    while(i > 0U)
    {
        char c[2];

        i--;

        c[0] =
            buf[i];

        c[1] =
            '\0';

        uart_write_str(c);
    }
}


static void policy_log_observe_state(uint32_t now)
{
    if(USB_ROLE_POLICY_LOG_OBSERVE == 0U)
    {
        return;
    }

    if((uint32_t)(now - observe_last_log_ms) < USB_ROLE_POLICY_OBSERVE_PERIOD_MS)
    {
        return;
    }

    observe_last_log_ms =
        now;

    uart_write_str("[USB-POLICY] OBSERVE role=");
    uart_write_str(ucpd_diag_is_source() ? "HOST/SOURCE" : "DEVICE/SINK");

    uart_write_str(" unattached=");
    policy_write_dec_u32((uint32_t)ucpd_diag_is_unattached());

    uart_write_str(" attached=");
    policy_write_dec_u32((uint32_t)ucpd_diag_is_attached());

    uart_write_str(" usb=");
    policy_write_dec_u32((uint32_t)ucpd_diag_usb_started());

    uart_write_str(" vbus=");
    policy_write_dec_u32((uint32_t)ucpd_diag_vbus_present());

    uart_write_str("\r\n");
}


static void policy_log_scan_state(uint32_t now)
{
    if(USB_ROLE_POLICY_LOG_SCAN == 0U)
    {
        (void)now;
        return;
    }

    uart_write_str("[USB-POLICY] SCAN slot=");
    uart_write_str((scan_slot == USB_ROLE_SCAN_SINK) ? "SINK/RD" : "SOURCE/RP");

    uart_write_str(" role=");
    uart_write_str(ucpd_diag_is_source() ? "HOST/SOURCE" : "DEVICE/SINK");

    uart_write_str(" unattached=");
    policy_write_dec_u32((uint32_t)ucpd_diag_is_unattached());

    uart_write_str(" attached=");
    policy_write_dec_u32((uint32_t)ucpd_diag_is_attached());

    uart_write_str(" usb=");
    policy_write_dec_u32((uint32_t)ucpd_diag_usb_started());

    uart_write_str(" vbus=");
    policy_write_dec_u32((uint32_t)ucpd_diag_vbus_present());

    uart_write_str("\r\n");
}


/* =========================
   Manual policy
========================= */

static void usb_role_policy_manual_init(void)
{
    ucpd_diag_set_auto_scan_mode(0U);

    usb_mode_button_init();

    policy_log(
        USB_ROLE_POLICY_LOG_MANUAL,
        "[USB-POLICY] MANUAL mode: PA0 toggles DEVICE/SINK <-> HOST/SOURCE\r\n");
}


static void usb_role_policy_manual_task(void)
{
    if(usb_mode_button_pressed())
    {
        policy_log(
            USB_ROLE_POLICY_LOG_MANUAL,
            "[USB-POLICY] MANUAL toggle request\r\n");

        ucpd_diag_toggle_role();
    }
}


/* =========================
   Auto-DRP observe policy
========================= */

static void usb_role_policy_auto_observe_init(void)
{
    observe_last_log_ms =
        0U;

    ucpd_diag_set_auto_scan_mode(0U);

    policy_log(
        USB_ROLE_POLICY_LOG_AUTO,
        "[USB-POLICY] AUTO-DRP OBSERVE mode enabled\r\n");
}


static void usb_role_policy_auto_observe_task(void)
{
    uint32_t now =
        HAL_GetTick();

    policy_log_observe_state(now);
}


/* =========================
   Auto-DRP scan policy
========================= */

static uint8_t usb_role_policy_scan_allowed(void)
{
    if(ucpd_diag_is_unattached() == 0U)
    {
        return 0U;
    }

    if(ucpd_diag_is_attached() != 0U)
    {
        return 0U;
    }

    if(ucpd_diag_usb_started() != 0U)
    {
        return 0U;
    }

    return 1U;
}


static void usb_role_policy_scan_apply_slot(void)
{
    if(scan_slot == USB_ROLE_SCAN_SINK)
    {
        policy_log(
            USB_ROLE_POLICY_LOG_SCAN,
            "[USB-POLICY] SCAN -> SINK/RD\r\n");

        ucpd_diag_set_scan_sink_role();
    }
    else
    {
        if(ucpd_diag_vbus_present())
        {
            policy_log(
                USB_ROLE_POLICY_LOG_SCAN,
                "[USB-POLICY] SCAN SOURCE/RP skipped: VBUS present\r\n");

            return;
        }

        policy_log(
            USB_ROLE_POLICY_LOG_SCAN,
            "[USB-POLICY] SCAN -> SOURCE/RP\r\n");

        ucpd_diag_set_scan_source_role();
    }
}


static void usb_role_policy_scan_next_slot(uint32_t now)
{
    if(scan_slot == USB_ROLE_SCAN_SINK)
    {
        scan_slot =
            USB_ROLE_SCAN_SOURCE;
    }
    else
    {
        scan_slot =
            USB_ROLE_SCAN_SINK;
    }

    scan_last_switch_ms =
        now;

    usb_role_policy_scan_apply_slot();

    policy_log_scan_state(now);
}


static void usb_role_policy_auto_init(void)
{
    scan_slot =
        USB_ROLE_SCAN_SINK;

    scan_last_switch_ms =
        HAL_GetTick();

    scan_paused =
        0U;

    ucpd_diag_set_auto_scan_mode(1U);

    policy_log(
        USB_ROLE_POLICY_LOG_AUTO,
        "[USB-POLICY] AUTO-DRP scan enabled\r\n");

    policy_log(
        USB_ROLE_POLICY_LOG_AUTO,
        "[USB-POLICY] AUTO-DRP starts in SINK/RD slot\r\n");

    ucpd_diag_set_scan_sink_role();

    policy_log_scan_state(scan_last_switch_ms);
}


static uint8_t usb_role_policy_pc_vbus_hold_task(uint32_t now)
{
    /*
     * PC/source friendliness:
     *
     * If VBUS is present while we are idle/unattached, strongly prefer SINK/RD.
     * This gives the PC a stable window to see the device and enumerate CDC.
     */
    if(ucpd_diag_vbus_present() == 0U)
    {
        return 0U;
    }

    if(ucpd_diag_is_attached() != 0U)
    {
        return 0U;
    }

    if(ucpd_diag_usb_started() != 0U)
    {
        return 0U;
    }

    if(ucpd_diag_is_unattached() == 0U)
    {
        return 0U;
    }

    if(scan_slot != USB_ROLE_SCAN_SINK)
    {
        scan_slot =
            USB_ROLE_SCAN_SINK;

        policy_log(
            USB_ROLE_POLICY_LOG_SCAN,
            "[USB-POLICY] PC VBUS present -> force SINK/RD hold\r\n");

        ucpd_diag_set_scan_sink_role();

        policy_log_scan_state(now);
    }

    /*
     * Keep extending the scan timer while PC VBUS is present.
     * This prevents automatic switch to SOURCE/RP during PC attach.
     */
    scan_last_switch_ms =
        now;

    return 1U;
}


static void usb_role_policy_auto_task(void)
{
    uint32_t now =
        HAL_GetTick();

    if(usb_role_policy_pc_vbus_hold_task(now))
    {
        return;
    }

    if(usb_role_policy_scan_allowed() == 0U)
    {
        if(scan_paused == 0U)
        {
            scan_paused =
                1U;

            policy_log(
                USB_ROLE_POLICY_LOG_SCAN,
                "[USB-POLICY] SCAN paused: attach/usb active/not-unattached\r\n");

            policy_log_scan_state(now);
        }

        scan_last_switch_ms =
            now;

        return;
    }

    if(scan_paused != 0U)
    {
        scan_paused =
            0U;

        scan_last_switch_ms =
            now;

        policy_log(
            USB_ROLE_POLICY_LOG_SCAN,
            "[USB-POLICY] SCAN resumed\r\n");

        policy_log_scan_state(now);
    }

    if((uint32_t)(now - scan_last_switch_ms) >= USB_ROLE_POLICY_SCAN_WINDOW_MS)
    {
        usb_role_policy_scan_next_slot(now);
    }
}


/* =========================
   Public API
========================= */

void usb_role_policy_init(void)
{
#if USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_MANUAL

    policy_log(
        USB_ROLE_POLICY_LOG_BOOT,
        "[USB-POLICY] init: MANUAL\r\n");

    usb_role_policy_manual_init();

#elif USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_AUTO_DRP_OBSERVE

    policy_log(
        USB_ROLE_POLICY_LOG_BOOT,
        "[USB-POLICY] init: AUTO-DRP OBSERVE\r\n");

    usb_role_policy_auto_observe_init();

#elif USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_AUTO_DRP

    policy_log(
        USB_ROLE_POLICY_LOG_BOOT,
        "[USB-POLICY] init: AUTO-DRP\r\n");

    usb_role_policy_auto_init();

#else
#error "Invalid USB_ROLE_POLICY_MODE"
#endif
}


void usb_role_policy_task(void)
{
#if USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_MANUAL

    usb_role_policy_manual_task();

#elif USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_AUTO_DRP_OBSERVE

    usb_role_policy_auto_observe_task();

#elif USB_ROLE_POLICY_MODE == USB_ROLE_POLICY_MODE_AUTO_DRP

    usb_role_policy_auto_task();

#else
#error "Invalid USB_ROLE_POLICY_MODE"
#endif
}