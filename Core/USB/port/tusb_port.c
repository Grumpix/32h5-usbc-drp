#include "tusb.h"

/* link STM32 driver hooks */
void tusb_init(void)
{
    board_init();
    dcd_init(0);

    tusb_init(); // TinyUSB core init
}