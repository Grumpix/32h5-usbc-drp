#include "main.h"
#include "usb_manager.h"
#include "drp_fsm.h"
#include "uart_log.h"
#include "usb_mode_button.h"

void SystemClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_ICACHE_Init(void);

int main(void)
{
    MPU_Config();

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_ICACHE_Init();
    uart_log_init();
    uart_log_write("BOOT\r\n");

    HAL_Delay(5);

    usb_manager_init();
    drp_init();
    usb_mode_button_init();

    while (1)
    {
        if (usb_mode_button_pressed())
        {
            if (usb_manager_toggle_mode())
            {
                uart_log_write((usb_manager_get_state() == USB_STATE_HOST) ?
                               "MODE HOST\r\n" : "MODE DEVICE\r\n");
            }
        }

        usb_manager_task();
        drp_task();
    }
}


void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc_init = {0};
    RCC_ClkInitTypeDef clk_init = {0};
    RCC_PeriphCLKInitTypeDef usb_clk_init = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc_init.HSEState = RCC_HSE_ON;
    osc_init.PLL.PLLState = RCC_PLL_ON;
    osc_init.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
    osc_init.PLL.PLLM = 12;
    osc_init.PLL.PLLN = 250;
    osc_init.PLL.PLLP = 2;
    osc_init.PLL.PLLQ = 2;
    osc_init.PLL.PLLR = 2;
    osc_init.PLL.PLLRGE = RCC_PLL1_VCIRANGE_1;
    osc_init.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
    osc_init.PLL.PLLFRACN = 0;

    if (HAL_RCC_OscConfig(&osc_init) != HAL_OK)
    {
        Error_Handler();
    }

    clk_init.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                         RCC_CLOCKTYPE_PCLK3;
    clk_init.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk_init.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk_init.APB1CLKDivider = RCC_HCLK_DIV1;
    clk_init.APB2CLKDivider = RCC_HCLK_DIV1;
    clk_init.APB3CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clk_init, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }

    usb_clk_init.PeriphClockSelection = RCC_PERIPHCLK_USB;
    usb_clk_init.PLL3.PLL3Source = RCC_PLL3_SOURCE_HSE;
    usb_clk_init.PLL3.PLL3M = 6;
    usb_clk_init.PLL3.PLL3N = 48;
    usb_clk_init.PLL3.PLL3P = 2;
    usb_clk_init.PLL3.PLL3Q = 4;
    usb_clk_init.PLL3.PLL3R = 2;
    usb_clk_init.PLL3.PLL3RGE = RCC_PLL3_VCIRANGE_1;
    usb_clk_init.PLL3.PLL3VCOSEL = RCC_PLL3_VCORANGE_WIDE;
    usb_clk_init.PLL3.PLL3FRACN = 0;
    usb_clk_init.PLL3.PLL3ClockOut = RCC_PLL3_DIVQ;
    usb_clk_init.UsbClockSelection = RCC_USBCLKSOURCE_PLL3Q;

    if (HAL_RCCEx_PeriphCLKConfig(&usb_clk_init) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MPU_Config(void)
{
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(VBUS_ON_GPIO_Port, VBUS_ON_Pin, GPIO_PIN_RESET);

    gpio_init.Pin = LED_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_Port, &gpio_init);

    gpio_init.Pin = VBUS_ON_Pin;
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(VBUS_ON_GPIO_Port, &gpio_init);
}

static void MX_ICACHE_Init(void)
{
    if (HAL_ICACHE_ConfigAssociativityMode(ICACHE_2WAYS) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ICACHE_Enable() != HAL_OK)
    {
        Error_Handler();
    }
}

uint32_t tusb_time_millis_api(void)
{
    return HAL_GetTick();
}

void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
    }
}