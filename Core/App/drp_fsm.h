#pragma once
#include <stdint.h>

typedef enum {
    DRP_ROLE_UNKNOWN = 0,
    DRP_ROLE_DEVICE
} drp_role_t;

void drp_init(void);
void drp_task(void);
drp_role_t drp_get_role(void);
uint8_t drp_is_ready(void);