#pragma once

#include <stdbool.h>

void usb_mode_button_init(void);
bool usb_mode_button_pressed(void);
bool usb_mode_button_available(void);