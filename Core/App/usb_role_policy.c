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
 * Pro observe test pozdeji prepnout na:
 * USB_ROLE_POLICY_MODE_AUTO_DRP_OBSERVE
 */
#define USB_ROLE_POLICY_MODE                    USB_ROLE_POLICY_MODE_MANUAL


/* =========================
   LOG CONFIG
========================= */

#define USB_ROLE_POLICY_LOG_BOOT                1U
#define USB_ROLE_POLICY_LOG_MANUAL              1U
#define USB_ROLE_POLICY_LOG_AUTO                1U
#define USB_ROLE_POLICY_LOG_OBSERVE             1U


#define USB_ROLE_POLICY_OBSERVE_PERIOD_MS       1000U


static uint32_t observe_last_log_ms =
    0U;


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


/* =========================
   Manual policy
========================= */

static void usb_role_policy_manual_init(void)
{
    /*
     * PA0 button zustava inicializovany tady, ne v main.c.
     * Chovani je porad stejne: press -> toggle DEVICE/SINK <-> HOST/SOURCE.
     */
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
    /*
     * Observe mode zatim nic neridi.
     *
     * Jen loguje stav, abychom meli jistotu, ze policy vrstva vidi:
     * - current role
     * - unattached/attached
     * - USB started
     * - VBUS present
     */
    observe_last_log_ms =
        0U;

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
   Auto-DRP skeleton
========================= */

static void usb_role_policy_auto_init(void)
{
    /*
     * Zatim jen skeleton.
     *
     * Auto-DRP budeme delat az dalsim patchem:
     * - periodicky zkouset Sink/Rd okno
     * - periodicky zkouset Source/Rp okno
     * - pri attach nechat ucpd_diag rozjet device/host
     * - zachovat ochrany proti VBUS kolizi
     */
    policy_log(
        USB_ROLE_POLICY_LOG_AUTO,
        "[USB-POLICY] AUTO-DRP skeleton enabled\r\n");
}


static void usb_role_policy_auto_task(void)
{
    /*
     * Zatim zamerne nic.
     */
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