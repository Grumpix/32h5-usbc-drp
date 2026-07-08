#include "usb_mode_button.h"

#include "main.h"
#include "uart.h"

#include "stm32h5xx_hal.h"
#include "stm32h5xx_ll_bus.h"
#include "stm32h5xx_ll_gpio.h"

#include <stdbool.h>
#include <stdint.h>


/*
 * HARDWARE TEST VERSION
 *
 * PA0 button -> toggle PB8 / VBUS P-MOS gate.
 *
 * Zapojeni P-MOS:
 *
 * Source = +5V
 * Drain  = VBUS
 * Gate   = PB8 + 100k pull-up na Source/+5V
 *
 * Bezpecne rizeni:
 *
 * VBUS OFF:
 *   PB8 = input / Hi-Z
 *   gate vytazena pres 100k na +5V
 *   Vgs = 0V
 *   P-MOS OFF
 *
 * VBUS ON:
 *   PB8 = output LOW
 *   gate = 0V
 *   Vgs = -5V
 *   P-MOS ON
 *
 * DULEZITE:
 * PB8 je 5V-tolerantni typ pinu, ale jako vystup nedava 5V.
 * Proto OFF nedelame output HIGH, ale Hi-Z.
 */


/*
 * Button pin.
 */
#define BUTTON_PORT             GPIOA
#define BUTTON_PIN              GPIO_PIN_0

#define BUTTON_LL_PORT          GPIOA
#define BUTTON_LL_PIN           LL_GPIO_PIN_0


/*
 * 1 = stisk tlacitka dava na PA0 log. 1
 * 0 = stisk tlacitka dava na PA0 log. 0
 */
#define BUTTON_ACTIVE_HIGH      1


#if BUTTON_ACTIVE_HIGH
#define BUTTON_PULL             LL_GPIO_PULL_DOWN
#else
#define BUTTON_PULL             LL_GPIO_PULL_UP
#endif


/*
 * VBUS P-MOS gate control pin.
 *
 * Zmeneno z PC13 na PB8.
 */
#define VBUS_FET_PORT           GPIOB
#define VBUS_FET_PIN            GPIO_PIN_8

#define VBUS_FET_LL_PORT        GPIOB
#define VBUS_FET_LL_PIN         LL_GPIO_PIN_8


/*
 * Nechavam podle tveho pozadavku.
 *
 * Poznamka:
 * U P-MOS gate ale realne:
 * - ON  = PB8 output LOW
 * - OFF = PB8 Hi-Z
 *
 * Tohle makro tady nechavame hlavne pro citelnost / kompatibilitu.
 */
#define VBUS_FET_ACTIVE_HIGH    1


/*
 * Debounce.
 */
#define BUTTON_DEBOUNCE_MS      50U


static uint8_t button_last_raw_pressed = 0U;
static uint8_t button_stable_pressed = 0U;
static uint32_t button_last_change_ms = 0U;

static uint8_t vbus_enabled = 0U;


static uint8_t button_read_pressed_raw(void)
{
    GPIO_PinState state =
        HAL_GPIO_ReadPin(
            BUTTON_PORT,
            BUTTON_PIN);

#if BUTTON_ACTIVE_HIGH
    return (state == GPIO_PIN_SET) ? 1U : 0U;
#else
    return (state == GPIO_PIN_RESET) ? 1U : 0U;
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


static uint32_t get_pb8_mode(void)
{
    return
        (GPIOB->MODER >> (8U * 2U)) & 0x3U;
}


static void print_vbus_fet_state(const char *prefix)
{
    uart_write_str(prefix);

    uart_write_str(" VBUS_EN=");
    uart_write_hex(vbus_enabled);

    uart_write_str(" PB8_MODE=");
    uart_write_hex(get_pb8_mode());

    uart_write_str(" PB8_IDR=");
    uart_write_hex((GPIOB->IDR & GPIO_PIN_8) ? 1U : 0U);

    uart_write_str(" PB8_ODR=");
    uart_write_hex((GPIOB->ODR & GPIO_PIN_8) ? 1U : 0U);

    uart_write_str(" GPIOB_MODER=");
    uart_write_hex(GPIOB->MODER);

    uart_write_str(" GPIOB_PUPDR=");
    uart_write_hex(GPIOB->PUPDR);

    uart_write_str(" GPIOB_ODR=");
    uart_write_hex(GPIOB->ODR);

    uart_write_str("\r\n");
}


static void vbus_fet_set_off_hiz(void)
{
    /*
     * OFF:
     * PB8 Hi-Z/input bez pullu.
     * Externi 100k gate-source vytahne gate na +5V.
     */

    LL_GPIO_SetPinPull(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinMode(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_MODE_INPUT);

    /*
     * ODR si pripravime na 0, aby pri prepnuti do outputu
     * sel pin rovnou do LOW a gate se nestihla cuknout nahoru/dolu divne.
     */
    HAL_GPIO_WritePin(
        VBUS_FET_PORT,
        VBUS_FET_PIN,
        GPIO_PIN_RESET);
}


static void vbus_fet_set_on_drive_low(void)
{
    /*
     * ON:
     * PB8 output LOW.
     * Gate P-MOSu jde na GND.
     */

    HAL_GPIO_WritePin(
        VBUS_FET_PORT,
        VBUS_FET_PIN,
        GPIO_PIN_RESET);

    LL_GPIO_SetPinPull(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_PULL_NO);

    LL_GPIO_SetPinOutputType(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_OUTPUT_PUSHPULL);

    LL_GPIO_SetPinSpeed(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_SPEED_FREQ_LOW);

    LL_GPIO_SetPinMode(
        VBUS_FET_LL_PORT,
        VBUS_FET_LL_PIN,
        LL_GPIO_MODE_OUTPUT);
}


static void vbus_fet_apply(uint8_t enable)
{
    vbus_enabled =
        enable ? 1U : 0U;

    if(vbus_enabled)
    {
        vbus_fet_set_on_drive_low();

        uart_write_str("[VBUS-FET] ON: PB8 output LOW\r\n");
    }
    else
    {
        vbus_fet_set_off_hiz();

        uart_write_str("[VBUS-FET] OFF: PB8 Hi-Z, gate pulled to +5V\r\n");
    }

    print_vbus_fet_state("[VBUS-FET]");
}


static void vbus_fet_toggle(void)
{
    vbus_fet_apply(
        vbus_enabled ? 0U : 1U);
}


void usb_mode_button_init(void)
{
    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOA);

    LL_AHB2_GRP1_EnableClock(
        LL_AHB2_GRP1_PERIPH_GPIOB);


    /*
     * PA0 button input.
     */

    LL_GPIO_SetPinMode(
        BUTTON_LL_PORT,
        BUTTON_LL_PIN,
        LL_GPIO_MODE_INPUT);

    LL_GPIO_SetPinPull(
        BUTTON_LL_PORT,
        BUTTON_LL_PIN,
        BUTTON_PULL);


    /*
     * PB8 VBUS P-MOS gate control.
     *
     * Default VBUS OFF = Hi-Z.
     * External 100k pull-up gate-source keeps MOSFET OFF.
     */

    vbus_fet_apply(0U);


    button_last_raw_pressed =
        button_read_pressed_raw();

    button_stable_pressed =
        button_last_raw_pressed;

    button_last_change_ms =
        HAL_GetTick();


    uart_write_str("[BUTTON] PA0 press-toggle PB8 VBUS FET test active\r\n");

#if BUTTON_ACTIVE_HIGH
    uart_write_str("[BUTTON] mode: active-high, pull-down\r\n");
#else
    uart_write_str("[BUTTON] mode: active-low, pull-up\r\n");
#endif

#if VBUS_FET_ACTIVE_HIGH
    uart_write_str("[VBUS-FET] macro VBUS_FET_ACTIVE_HIGH=1\r\n");
#else
    uart_write_str("[VBUS-FET] macro VBUS_FET_ACTIVE_HIGH=0\r\n");
#endif

    print_button_state("[BUTTON] INIT");
    print_vbus_fet_state("[VBUS-FET] INIT");
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

                uart_write_str("[BUTTON] PRESS -> TOGGLE VBUS\r\n");

                print_button_state("[BUTTON] PRESS");

                vbus_fet_toggle();
            }
            else
            {
                uart_write_str("[BUTTON] RELEASE\r\n");
            }
        }
    }


    /*
     * Vracime false, aby main.c nespustil puvodni USB mode toggle logiku.
     */
    return false;
}