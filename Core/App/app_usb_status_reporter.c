#include "app_usb_status_reporter.h"

#include "app_usb_status.h"
#include "uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>


/*
 * USB STATUS REPORTER
 *
 * Read-only debug reporter nad app_usb_status API.
 *
 * - nic neridi
 * - nesaha na VBUS
 * - nestartuje ani nezastavuje USB stack
 * - vypisuje pouze pri zmene stavu
 */


#define APP_USB_STATUS_REPORTER_LOG_ENABLE      1U


static app_usb_status_t last_status;
static bool last_status_valid = false;


static void reporter_log(const char *s)
{
#if APP_USB_STATUS_REPORTER_LOG_ENABLE
    uart_write_str(s);
#else
    (void)s;
#endif
}


static void reporter_write_bool(bool value)
{
    reporter_log(value ? "1" : "0");
}


static bool status_equal(
    const app_usb_status_t *a,
    const app_usb_status_t *b)
{
    return
        (a->role == b->role) &&
        (a->typec_attached == b->typec_attached) &&
        (a->typec_unattached == b->typec_unattached) &&
        (a->vbus_present == b->vbus_present) &&
        (a->usb_started == b->usb_started) &&
        (a->pc_connected == b->pc_connected) &&
        (a->pc_cdc_ready == b->pc_cdc_ready) &&
        (a->host_active == b->host_active) &&
        (a->host_device_attached == b->host_device_attached) &&
        (a->msc_mounted == b->msc_mounted) &&
        (a->ftdi_mounted == b->ftdi_mounted) &&
        (a->ftdi_ready == b->ftdi_ready);
}


static void report_status(const app_usb_status_t *s)
{
    reporter_log("[APP-USB] role=");
    reporter_log(app_usb_role_name(s->role));

    reporter_log(" attached=");
    reporter_write_bool(s->typec_attached);

    reporter_log(" unattached=");
    reporter_write_bool(s->typec_unattached);

    reporter_log(" vbus=");
    reporter_write_bool(s->vbus_present);

    reporter_log(" usb=");
    reporter_write_bool(s->usb_started);

    reporter_log(" pc=");
    reporter_write_bool(s->pc_connected);

    reporter_log(" cdc=");
    reporter_write_bool(s->pc_cdc_ready);

    reporter_log(" host=");
    reporter_write_bool(s->host_active);

    reporter_log(" hdev=");
    reporter_write_bool(s->host_device_attached);

    reporter_log(" msc=");
    reporter_write_bool(s->msc_mounted);

    reporter_log(" ftdi=");
    reporter_write_bool(s->ftdi_mounted);

    reporter_log(" ftdi_ready=");
    reporter_write_bool(s->ftdi_ready);

    reporter_log("\r\n");
}


void app_usb_status_reporter_init(void)
{
    memset(&last_status, 0, sizeof(last_status));

    last_status_valid =
        false;

    reporter_log("[APP-USB] status reporter init\r\n");
}


void app_usb_status_reporter_task(void)
{
    app_usb_status_t status;

    app_usb_status_get(&status);

    if(last_status_valid)
    {
        if(status_equal(&status, &last_status))
        {
            return;
        }
    }

    report_status(&status);

    last_status =
        status;

    last_status_valid =
        true;
}