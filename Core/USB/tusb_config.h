#pragma once

#define CFG_TUSB_MCU              OPT_MCU_STM32H5

/* =========================
   DEVICE
========================= */
#define CFG_TUD_ENABLED           1
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256

/* =========================
   HOST
========================= */
#define CFG_TUH_ENABLED           1
#define CFG_TUH_MSC               1

/* =========================
   USB speed
========================= */
#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_HOST)