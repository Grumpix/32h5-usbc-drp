#include "usb_mode_button.h"

#include "main.h"
#include "uart.h"

#include "stm32h5xx_hal.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

#include <stdbool.h>
#include <stdint.h>


/*
 * PA0 button for USB-C manual role toggle.
 *
 * DULEZITE:
 * Tento soubor uz NESMI ovladat PB8 / VBUS FET.
 *
 * PB8 / VBUS Source FET ridi pouze ucpd_diag.c podle aktualni Type-C role.
 */


#define BUTTON_PORT             GPIOA
#define BUTTON_PIN              GPIO_PIN_0

#define BUTTON_LL_PORT          GPIOA
#define BUTTON_LL_PIN           LL_GPIO_PIN_0


/*
 * NUCLEO user button / PA0:
 *
 * 1 = stisk tlacitka dava na PA0 log. 1
 * 0 = stisk tlacitka dava na PA0 log. 0
 */
#define BUTTON_ACTIVE_HIGH      1


#if BUTTON_ACTIVE_HIGH
#define BUTTON_PULL             LL_GPIO_PULL_DOWN
#else
#define BUTTON_PULL             LL_GPIO_PULL_UP
#endif


#define BUTTON_DEBOUNCE_MS      50U


static uint8_t button_last_raw_pressed = 0U;
static uint8_t button_stable_pressed = 0U;
static uint32_t button_last_change_ms = 0U;


static uint8_t button_read_pressed_raw(void)
{
    GPIO_PinState state =
        HAL_GPIO_ReadPin(
            BUTTON_PORT,
            BUTTON_PIN);

#if BUTTON_ACTIVE_HIGH
    return
        (state == GPIO_PIN_SET) ? 1U : 0U;
#else
    return
        (state == GPIO_PIN_RESET) ? 1U : 0U;
#endif
}


static void print_button_state(const char *prefix)
{
    uart_write_str(prefix);

    uart_write_str(" PA0_IDR=");
    uart_write_hex(GPIOA->IDR);

    uart_write_str(" PRESSED=");
    uart_write_hex(button_read_pressed_raw());

    uart_write_str(" GPIOA_MODER=");
    uart_write_hex(GPIOA->MODER);

    uart_write_str(" GPIOA_PUPDR=");
    uart_write_hex(GPIOA->PUPDR);

    uart_write_str("\r\n");
}


void usb_mode_button_init(void)
{
    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOA);

    LL_GPIO_SetPinMode(
        BUTTON_LL_PORT,
        BUTTON_LL_PIN,
        LL_GPIO_MODE_INPUT);

    LL_GPIO_SetPinPull(
        BUTTON_LL_PORT,
        BUTTON_LL_PIN,
        BUTTON_PULL);

    button_last_raw_pressed =
        button_read_pressed_raw();

    button_stable_pressed =
        button_last_raw_pressed;

    button_last_change_ms =
        HAL_GetTick();

    uart_write_str("[BUTTON] PA0 USB role toggle active\r\n");

#if BUTTON_ACTIVE_HIGH
    uart_write_str("[BUTTON] mode: active-high, pull-down\r\n");
#else
    uart_write_str("[BUTTON] mode: active-low, pull-up\r\n");
#endif

    print_button_state("[BUTTON] INIT");
}


bool usb_mode_button_pressed(void)
{
    uint32_t now =
        HAL_GetTick();

    uint8_t raw_pressed =
        button_read_pressed_raw();

    /*
     * Debounce raw changes.
     */
    if(raw_pressed != button_last_raw_pressed)
    {
        button_last_raw_pressed =
            raw_pressed;

        button_last_change_ms =
            now;
    }

    /*
     * Stable state change.
     */
    if((now - button_last_change_ms) >= BUTTON_DEBOUNCE_MS)
    {
        if(raw_pressed != button_stable_pressed)
        {
            button_stable_pressed =
                raw_pressed;

            if(button_stable_pressed)
            {
                /*
                 * Pouze STISK.
                 * Uvolneni tlacitka se ignoruje.
                 */
                uart_write_str("[BUTTON] PRESS -> USB ROLE TOGGLE\r\n");

                print_button_state("[BUTTON] PRESS");

                return true;
            }
            else
            {
                uart_write_str("[BUTTON] RELEASE\r\n");
            }
        }
    }

    return false;
}