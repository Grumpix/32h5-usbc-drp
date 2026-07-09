#include "app_usb_status.h"

#include "ucpd_diag.h"

#include "usb_device_tusb.h"
#include "usb_host_tusb.h"
#include "ftdi_host.h"

#include <stddef.h>


app_usb_role_t app_usb_get_role(void)
{
    return
        ucpd_diag_is_source()
        ? APP_USB_ROLE_HOST_SOURCE
        : APP_USB_ROLE_DEVICE_SINK;
}


bool app_usb_is_typec_attached(void)
{
    return
        ucpd_diag_is_attached() ? true : false;
}


bool app_usb_is_vbus_present(void)
{
    return
        ucpd_diag_vbus_present() ? true : false;
}


bool app_usb_is_usb_started(void)
{
    return
        ucpd_diag_usb_started() ? true : false;
}


bool app_usb_is_pc_connected(void)
{
    /*
     * PC/source pripojeni z pohledu Type-C:
     * - jsme DEVICE/SINK
     * - mame attach
     * - VBUS je pritomny
     *
     * To jeste neznamena, ze CDC terminal otevrel DTR.
     */
    if(app_usb_get_role() != APP_USB_ROLE_DEVICE_SINK)
    {
        return false;
    }

    if(!app_usb_is_typec_attached())
    {
        return false;
    }

    if(!app_usb_is_vbus_present())
    {
        return false;
    }

    return true;
}


bool app_usb_is_pc_cdc_ready(void)
{
    /*
     * CDC ready = TinyUSB device mounted + CDC line connected.
     *
     * Typicky az po tom, co host OS enumeruje device a terminal/driver otevře port.
     */
    if(app_usb_get_role() != APP_USB_ROLE_DEVICE_SINK)
    {
        return false;
    }

    return
        usb_device_tusb_is_cdc_connected();
}


bool app_usb_is_host_active(void)
{
    return
        (app_usb_get_role() == APP_USB_ROLE_HOST_SOURCE) &&
        app_usb_is_usb_started();
}


bool app_usb_is_host_device_attached(void)
{
    if(app_usb_get_role() != APP_USB_ROLE_HOST_SOURCE)
    {
        return false;
    }

    return
        usb_host_tusb_is_device_attached();
}


bool app_usb_is_msc_mounted(void)
{
    if(app_usb_get_role() != APP_USB_ROLE_HOST_SOURCE)
    {
        return false;
    }

    return
        usb_host_tusb_is_msc_mounted();
}


bool app_usb_is_ftdi_mounted(void)
{
    if(app_usb_get_role() != APP_USB_ROLE_HOST_SOURCE)
    {
        return false;
    }

    return
        ftdi_host_is_mounted();
}


bool app_usb_is_ftdi_ready(void)
{
    if(app_usb_get_role() != APP_USB_ROLE_HOST_SOURCE)
    {
        return false;
    }

    return
        ftdi_host_is_ready();
}


const char *app_usb_role_name(app_usb_role_t role)
{
    if(role == APP_USB_ROLE_HOST_SOURCE)
    {
        return "HOST/SOURCE";
    }

    return "DEVICE/SINK";
}


void app_usb_status_get(app_usb_status_t *status)
{
    if(status == NULL)
    {
        return;
    }

    status->role =
        app_usb_get_role();

    status->typec_attached =
        app_usb_is_typec_attached();

    status->typec_unattached =
        ucpd_diag_is_unattached() ? true : false;

    status->vbus_present =
        app_usb_is_vbus_present();

    status->usb_started =
        app_usb_is_usb_started();

    status->pc_connected =
        app_usb_is_pc_connected();

    status->pc_cdc_ready =
        app_usb_is_pc_cdc_ready();

    status->host_active =
        app_usb_is_host_active();

    status->host_device_attached =
        app_usb_is_host_device_attached();

    status->msc_mounted =
        app_usb_is_msc_mounted();

    status->ftdi_mounted =
        app_usb_is_ftdi_mounted();

    status->ftdi_ready =
        app_usb_is_ftdi_ready();
}