#include "main.h"
#include "usb_manager.h"
#include "drp_fsm.h"
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

    HAL_Delay(5);

    
    drp_init();
    
    usb_mode_button_init();

    usb_manager_init();
    (void) usb_manager_start_device();

    while (1)
    {
        if (usb_mode_button_pressed())
        {
            if (usb_manager_toggle_mode())
            {
                drp_request_role((usb_manager_get_mode() == USB_MODE_HOST) ?
                                 DRP_ROLE_HOST : DRP_ROLE_DEVICE);
            }
        }

        drp_task();
        usb_manager_task();
    }
}


void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc_init = {0};
    RCC_ClkInitTypeDef clk_init = {0};
    RCC_PeriphCLKInitTypeDef usb_clk_init = {0};
    RCC_CRSInitTypeDef crs_init = {0};

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
    }

    osc_init.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_HSI48;
    osc_init.HSEState = RCC_HSE_ON;
    osc_init.HSI48State = RCC_HSI48_ON;
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

    __HAL_RCC_CRS_CLK_ENABLE();

    crs_init.Prescaler = RCC_CRS_SYNC_DIV1;
    crs_init.Source = RCC_CRS_SYNC_SOURCE_USB;
    crs_init.Polarity = RCC_CRS_SYNC_POLARITY_RISING;
    crs_init.ReloadValue = __HAL_RCC_CRS_RELOADVALUE_CALCULATE(48000000, 1000);
    crs_init.ErrorLimitValue = 34;
    crs_init.HSI48CalibrationValue = 32;
    HAL_RCCEx_CRSConfig(&crs_init);

    usb_clk_init.PeriphClockSelection = RCC_PERIPHCLK_USB;
    usb_clk_init.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;

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