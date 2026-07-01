#pragma once
#include <stdbool.h>
#include <stdint.h>

void usb_hw_init(void);
void usb_hw_deinit(void);

void usb_hw_reset_peripheral(void);
void usb_hw_enable_device(void);
void usb_hw_enable_host(void);
void usb_hw_irq_enable(void);
void usb_hw_irq_disable(void);
