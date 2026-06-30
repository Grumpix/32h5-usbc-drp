#include "tusb.h"

/* =========================
   MSC callbacks
========================= */

void tuh_mount_cb(uint8_t dev_addr)
{
    (void) dev_addr;
}

void tuh_umount_cb(uint8_t dev_addr)
{
    (void) dev_addr;
}