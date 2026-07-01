#include "usb_mode_button.h"

#include "main.h"

#if defined(USB_MODE_BUTTON_GPIO_Port) && defined(USB_MODE_BUTTON_Pin)
#define USB_MODE_BUTTON_CONFIGURED 1
#else
#define USB_MODE_BUTTON_CONFIGURED 0
#endif

#if USB_MODE_BUTTON_CONFIGURED
static void usb_mode_button_enable_clock(void)
{
    if (USB_MODE_BUTTON_GPIO_Port == GPIOA)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOB)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOC)
    {
        __HAL_RCC_GPIOC_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOD)
    {
        __HAL_RCC_GPIOD_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOE)
    {
        __HAL_RCC_GPIOE_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOF)
    {
        __HAL_RCC_GPIOF_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOG)
    {
        __HAL_RCC_GPIOG_CLK_ENABLE();
    }
    else if (USB_MODE_BUTTON_GPIO_Port == GPIOH)
    {
        __HAL_RCC_GPIOH_CLK_ENABLE();
    }
}
#endif

void usb_mode_button_init(void)
{
#if USB_MODE_BUTTON_CONFIGURED
    GPIO_InitTypeDef gpio_init = {0};

    usb_mode_button_enable_clock();

    gpio_init.Pin = USB_MODE_BUTTON_Pin;
    gpio_init.Mode = GPIO_MODE_INPUT;
    gpio_init.Pull = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(USB_MODE_BUTTON_GPIO_Port, &gpio_init);
#endif
}

bool usb_mode_button_pressed(void)
{
#if USB_MODE_BUTTON_CONFIGURED
    static GPIO_PinState stable_state = GPIO_PIN_SET;
    static GPIO_PinState last_sample = GPIO_PIN_SET;
    static uint32_t change_started_ms = 0;
    GPIO_PinState raw_state = HAL_GPIO_ReadPin(USB_MODE_BUTTON_GPIO_Port, USB_MODE_BUTTON_Pin);
    uint32_t now = HAL_GetTick();

    if (raw_state != last_sample)
    {
        last_sample = raw_state;
        change_started_ms = now;
    }

    if ((now - change_started_ms) < 75U)
    {
        return false;
    }

    if (raw_state != stable_state)
    {
        stable_state = raw_state;

        if (stable_state == GPIO_PIN_RESET)
        {
            return true;
        }
    }
#endif

    return false;
}

bool usb_mode_button_available(void)
{
#if USB_MODE_BUTTON_CONFIGURED
    return true;
#else
    return false;
#endif
}