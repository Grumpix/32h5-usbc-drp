#pragma once
#include <stdint.h>
#include "drp_fsm.h"


void usb_manager_init(void);
void usb_manager_task(void);

void usb_manager_start_device(void);
void usb_manager_start_host(void);
void usb_manager_stop(void);

usb_mode_t usb_manager_get_mode(void);