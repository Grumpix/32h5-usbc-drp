#include "tusb.h"
#include "usb_manager.h"

/* =========================
   DEVICE CALLBACKS (CDC)
========================= */

void tud_mount_cb(void)
{
    /* device connected */
}

void tud_umount_cb(void)
{
    /* device disconnected */
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void) remote_wakeup_en;
}

void tud_resume_cb(void)
{
}

/* =========================
   CDC RX HANDLER
========================= */

void tud_cdc_rx_cb(uint8_t itf)
{
    (void) itf;

    uint8_t buf[64];
    uint32_t count = tud_cdc_read(buf, sizeof(buf));

    /* echo example */
    tud_cdc_write(buf, count);
    tud_cdc_write_flush();
}