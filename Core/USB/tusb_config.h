#pragma once

#define CFG_TUSB_MCU              OPT_MCU_STM32H5
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_DEBUG            0

/*
 * Manual init používáme, protože rhport startujeme ručně přes usb_manager.
 */
#define CFG_TUSB_INIT_MANUAL      1

/* =========================
   ROOT PORT
========================= */

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_HOST)

/* =========================
   DEVICE
========================= */

#define CFG_TUD_ENABLED           1

#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_COUNT         1

#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256

#define CFG_TUD_NET               0
#define CFG_TUD_RNDIS             0
#define CFG_TUD_ECM_RNDIS         0

/* =========================
   HOST
========================= */

#define CFG_TUH_ENABLED           1

/*
 * USB flash disk / Mass Storage Host.
 */
#define CFG_TUH_MSC               1

/*
 * HID zatím může zůstat vypnutý.
 * Zapneme později pro klávesnici/myš, pokud bude potřeba.
 */
#define CFG_TUH_HID               0

#define CFG_TUH_HUB               0
#define CFG_TUH_DEVICE_MAX        4

/*
 * Počet současných MSC zařízení.
 * Pro první test stačí 1.
 */
#define CFG_TUH_MSC_MAXLUN        1

/*
 * Enumeration buffer.
 * Některé TinyUSB konfigurace ho vyžadují pro host enumeraci.
 */
#define CFG_TUH_ENUMERATION_BUFSIZE 256

/* =========================
   MEMORY
========================= */

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))