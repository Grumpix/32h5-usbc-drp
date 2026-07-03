#ifndef BOARD_APP_H_
#define BOARD_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"

/* ----------------------------------------------------------------------
 * Clock
 * ---------------------------------------------------------------------- */
void SystemClock_Config(void);

/* ----------------------------------------------------------------------
 * Board hooks
 * ---------------------------------------------------------------------- */

/**
 * Optional second-stage board init
 * (GPIO, UART, timers, etc.)
 */
void board_init2(void);
void Error_Handler(void);
/**
 * USB-C / VBUS control hook (future DRP / source-sink logic)
 */
void board_vbus_set(uint8_t rhport, bool state);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_APP_H_ */