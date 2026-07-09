#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_mode_button_init(void);
bool usb_mode_button_pressed(void);

#ifdef __cplusplus
}
#endif