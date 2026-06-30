#pragma once
#include <stdint.h>

typedef enum {
    DRP_ROLE_UNKNOWN = 0,
    DRP_ROLE_DEVICE,
    DRP_ROLE_HOST
} drp_role_t;

void drp_init(void);
void drp_task(void);
drp_role_t drp_get_role(void);