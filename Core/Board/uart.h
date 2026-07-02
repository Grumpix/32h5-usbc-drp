#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void uart1_init(void);

void uart1_write_char(char c);
void uart1_write_str(const char *s);
void uart1_write_hex(uint32_t v);

#ifdef __cplusplus
}
#endif