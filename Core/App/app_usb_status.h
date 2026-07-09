#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    APP_USB_ROLE_DEVICE_SINK = 0,
    APP_USB_ROLE_HOST_SOURCE
} app_usb_role_t;


typedef struct
{
    app_usb_role_t role;

    bool typec_attached;
    bool typec_unattached;
    bool vbus_present;
    bool usb_started;

    bool pc_connected;
    bool pc_cdc_ready;

    bool host_active;
    bool host_device_attached;
    bool msc_mounted;

    bool ftdi_mounted;
    bool ftdi_ready;
} app_usb_status_t;


void app_usb_status_get(app_usb_status_t *status);

app_usb_role_t app_usb_get_role(void);

bool app_usb_is_typec_attached(void);
bool app_usb_is_vbus_present(void);
bool app_usb_is_usb_started(void);

bool app_usb_is_pc_connected(void);
bool app_usb_is_pc_cdc_ready(void);

bool app_usb_is_host_active(void);
bool app_usb_is_host_device_attached(void);
bool app_usb_is_msc_mounted(void);

bool app_usb_is_ftdi_mounted(void);
bool app_usb_is_ftdi_ready(void);

const char *app_usb_role_name(app_usb_role_t role);

#ifdef __cplusplus
}
#endif