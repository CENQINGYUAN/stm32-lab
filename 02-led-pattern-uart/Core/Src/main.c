/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LED_NUM  4   // LED数量
// 4个LED对应的引脚（顺序：LED1→LED4）
static const uint16_t led_pins[LED_NUM] = {GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_3, GPIO_PIN_11};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
// ===== 系统状态 =====
uint8_t  current_mode = 1;     // 当前模式：1=流水 2=闪烁 3=呼吸
uint16_t speed_ms = 500;       // 速度（ms），默认500

// ===== 串口接收 =====
uint8_t  rx_byte;              // 每次中断收到的1个字节
char     rx_buf[64];           // 接收缓冲区
uint8_t  rx_idx = 0;           // 缓冲区当前位置

// ===== TIM3时基跑灯 =====
uint16_t tick_1ms = 0;         // 1ms计数器，到速度值就翻转
uint8_t  cur_led = 0;          // 模式1：当前点亮的是第几个LED
uint8_t  led_on = 1;           // 模式2：当前LED是亮还是灭
int16_t  breathe_val = 0;      // 模式3：呼吸亮度值 0~999
int8_t   breathe_dir = 1;       // 模式3：呼吸方向 1=渐亮 -1=渐暗
uint8_t  breathe_step = 0;      // 模式3：呼吸步长计数器
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */
void process_command(char *cmd);   // 指令解析函数
void led_all(uint8_t on);           // 全部LED亮/灭
void led_set(uint8_t index, uint8_t on); // 指定LED亮/灭
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// ===== 全部LED控制 =====
void led_all(uint8_t on)
{
    for (uint8_t i = 0; i < LED_NUM; i++)
    {
        HAL_GPIO_WritePin(GPIOB, led_pins[i], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

// ===== 指定LED控制 =====
void led_set(uint8_t index, uint8_t on)
{
    if (index < LED_NUM)
    {
        HAL_GPIO_WritePin(GPIOB, led_pins[index], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

// ===== 串口接收完成回调（每收到1字节触发） =====
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (rx_byte == '\n' || rx_byte == '\r')
        {
            if (rx_idx > 0)
            {
                rx_buf[rx_idx] = '\0';  // 加结束符
                process_command(rx_buf); // 解析指令
                rx_idx = 0;             // 清空缓冲区
            }
        }
        else
        {
            if (rx_idx < sizeof(rx_buf) - 1)
            {
                rx_buf[rx_idx++] = rx_byte;
            }
            else
            {
                rx_idx = 0;  // 溢出，清空
            }
        }
        // 继续接收下一个字节
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

// ===== 指令解析 =====
void process_command(char *cmd)
{
    if (strncmp(cmd, "mode:", 5) == 0)
    {
        uint8_t mode = atoi(cmd + 5);
        if (mode >= 1 && mode <= 3)
        {
            current_mode = mode;
            HAL_UART_Transmit(&huart1, (uint8_t*)"Mode changed\r\n", 14, 100);
        }
        else
        {
            HAL_UART_Transmit(&huart1, (uint8_t*)"Error: mode 1-3 only\r\n", 22, 100);
        }
    }
    else if (strncmp(cmd, "speed:", 6) == 0)
    {
        uint16_t speed = atoi(cmd + 6);
        if (speed >= 100 && speed <= 2000)
        {
            speed_ms = speed;
            HAL_UART_Transmit(&huart1, (uint8_t*)"Speed changed\r\n", 16, 100);
        }
        else
        {
            HAL_UART_Transmit(&huart1, (uint8_t*)"Error: speed 100-2000ms\r\n", 26, 100);
        }
    }
    else if (strncmp(cmd, "status", 6) == 0)
    {
        char buf[64];
        int len = sprintf(buf, "Mode:%d, Speed:%dms\r\n", current_mode, speed_ms);
        HAL_UART_Transmit(&huart1, (uint8_t*)buf, len, 100);
    }
    else
    {
        HAL_UART_Transmit(&huart1, (uint8_t*)"Unknown command\r\n", 18, 100);
    }
}

// ===== TIM3中断回调（每1ms触发） =====
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        tick_1ms++;
        
        if (current_mode == 1)  // 流水灯
        {
            if (tick_1ms >= speed_ms)
            {
                tick_1ms = 0;
                led_all(0);           // 全部灭
                led_set(cur_led, 1);  // 只亮当前
                cur_led = (cur_led + 1) % LED_NUM;
            }
        }
        else if (current_mode == 2)  // 闪烁灯
        {
            if (tick_1ms >= speed_ms)
            {
                tick_1ms = 0;
                led_all(led_on);      // 全部亮或灭
                led_on = !led_on;
            }
        }
        else if (current_mode == 3)  // 呼吸灯（简化版）
        {
            // 呼吸灯暂时用TIM3翻转模拟，后面再优化
            if (tick_1ms >= speed_ms / 10)
            {
                tick_1ms = 0;
                breathe_step++;
                if (breathe_step >= 10)
                {
                    breathe_step = 0;
                    breathe_dir = -breathe_dir;
                }
                // 简化：0步全灭，10步全亮
                led_all(breathe_dir > 0 ? (breathe_step >= 5) : (breathe_step < 5));
            }
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  // 启动TIM3中断
  HAL_TIM_Base_Start_IT(&htim3);
  // 启动串口接收中断
  HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
  // 开机打印欢迎信息
  HAL_UART_Transmit(&huart1, (uint8_t*)"===== LED Control =====\r\n", 26, 100);
  HAL_UART_Transmit(&huart1, (uint8_t*)"mode:1/2/3, speed:XXX, status\r\n", 34, 100);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 72-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_11, GPIO_PIN_RESET);

  /*Configure GPIO pins : PB0 PB1 PB3 PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
