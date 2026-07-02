#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void uart_init(void);
void uart_write_char(char ch);
void uart_write_str(const char *text);
void uart_write_hex(uint32_t value);

#ifdef __cplusplus
}
#endif