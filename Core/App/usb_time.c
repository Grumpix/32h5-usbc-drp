#include "stm32h5xx_hal.h"

uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}