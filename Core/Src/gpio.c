/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
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
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */
/**
  * @brief  PA15 按键引脚修正：补内部上拉
  * @note   放在这里而不是 MX_GPIO_Init() 内部，是因为本文件里没有任何位于函数体
  *         内部的 USER CODE 保留区（`USER CODE BEGIN/END 2` 在 MX_GPIO_Init() 的
  *         右花括号之后，属文件作用域），写函数体里重新生成代码时会被覆盖。
  *         由 main.c 在 MX_GPIO_Init() 之后调用。
  *
  *         CubeMX 生成的 PA15 是 GPIO_NOPULL（引脚悬空，读数不确定），这里补内部
  *         上拉让空闲电平确定为高。按键接 GND 时按下为低 → 对应 btn_app.c 的
  *         BTN_ACTIVE_LEVEL = 0。板上若已有外部上拉，内部上拉与之并存无害；
  *         若实测接法是"按键接 3.3V"，把 Pull 改成 GPIO_PULLDOWN 并同步改
  *         BTN_ACTIVE_LEVEL 为 1。
  *
  *         另：PA15 复位后默认是 JTDI，但 STM32F4 没有 F1 那套 AFIO/SWJ_CFG 重映射
  *         寄存器（Drivers 下不存在 __HAL_RCC_AFIO_CLK_ENABLE / AFIO_MAPR_SWJ_CFG），
  *         把 MODER 改成输入模式即可正常当 GPIO 用；调试走 SWD（PA13/PA14），不受影响。
  */
void GPIO_PA15_ButtonInit(void)
{
  GPIO_InitTypeDef gpio_init = {0};

  gpio_init.Pin  = BTN_PIN;
  gpio_init.Mode = GPIO_MODE_INPUT;
  gpio_init.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_GPIO_Port, &gpio_init);
}
/* USER CODE END 2 */
