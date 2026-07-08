#pragma once

#define CFG_TUSB_MCU              OPT_MCU_STM32H5
#define CFG_TUSB_OS               OPT_OS_NONE
#define CFG_TUSB_DEBUG            0
#define CFG_TUSB_INIT_MANUAL      1

#define CFG_TUSB_RHPORT0_MODE     (OPT_MODE_DEVICE | OPT_MODE_HOST)

/* DEVICE */
#define CFG_TUD_ENABLED           1
#define CFG_TUD_CDC               1
#define CFG_TUD_CDC_COUNT         1
#define CFG_TUD_CDC_RX_BUFSIZE    256
#define CFG_TUD_CDC_TX_BUFSIZE    256

#define CFG_TUD_NET               0
#define CFG_TUD_RNDIS             0
#define CFG_TUD_ECM_RNDIS         0

/* HOST */
#define CFG_TUH_ENABLED           1

#define CFG_TUH_MSC               1
#define CFG_TUH_MSC_MAXLUN        1

/* TinyUSB CDC/FTDI host driver vypnutý - na FT232R zamrzá po CONFIG DESC */
#define CFG_TUH_CDC               0
#define CFG_TUH_CDC_FTDI          0
#define CFG_TUH_CDC_CP210X        0
#define CFG_TUH_CDC_CH34X         0
#define CFG_TUH_CDC_CH9102F       0

#define CFG_TUH_HID               0
#define CFG_TUH_HUB               0

#define CFG_TUH_DEVICE_MAX        4
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN        __attribute__((aligned(4)))