#pragma once

#include <stdint.h>

void uart_log_init(void);
void uart_log_write(char const *msg);
void uart_log_write_u32(uint32_t value);