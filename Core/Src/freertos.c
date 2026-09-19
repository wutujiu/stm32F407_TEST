/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
/* LOG_TAG / LOG_LVL 必须在包含 <elog.h> 之前定义，之后即可使用 log_x() 简写接口。
 * LOG_LVL 是"编译期"过滤级别：低于该级别的 log_x() 会被编译为空语句，不占 Flash。 */
#define LOG_TAG    "app"
#define LOG_LVL    ELOG_LVL_DEBUG
#include <elog.h>
#include "btn_app.h"
#include "led_ctrl.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
osThreadId defaultTaskHandle;
osThreadId btnTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void const * argument);
void BtnTask(void const * argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* GetIdleTaskMemory prototype (linked to static allocation support) */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/* USER CODE BEGIN GET_IDLE_TASK_MEMORY */
static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
{
  *ppxIdleTaskTCBBuffer = &xIdleTaskTCBBuffer;
  *ppxIdleTaskStackBuffer = &xIdleStack[0];
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
  /* place for user code */
}
/* USER CODE END GET_IDLE_TASK_MEMORY */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* definition and creation of defaultTask */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* definition and creation of btnTask
   * 优先级由 osPriorityIdle 提到 osPriorityNormal：CMSIS-RTOS v1 的
   * makeFreeRtosPriority() 把 osPriorityIdle(-3) 映射成 FreeRTOS 优先级 0，
   * 也就是和空闲任务同级、靠时间片轮转，按键轮询需要确定性。
   * 栈 256 字（1 KB）：调用链 BtnTask → button_ticks → 事件回调 → log_i →
   * elog_output → vsnprintf → HAL_UART_Transmit，与 .ioc 保持一致。 */
  osThreadDef(btnTask, BtnTask, osPriorityNormal, 0, 256);
  btnTaskHandle = osThreadCreate(osThread(btnTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* USER CODE BEGIN Header_BtnTask */
/**
* @brief Function implementing the btnTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_BtnTask */
void BtnTask(void const * argument)
{
  /* USER CODE BEGIN BtnTask */
  static uint32_t poll_cnt = 0U;

  /* 顺序有讲究：先让 LED 引擎就绪（led_ctrl_init 会熄灭上电默认点亮的 PC13），
   * 再初始化按键，这样自检日志打出来时 LED 已经进入默认模式。 */
  led_ctrl_init();
  btn_app_init();

  log_i("BtnTask start: LED mode %u (%s), key poll every %u ms",
        (unsigned)led_ctrl_get_mode(),
        led_ctrl_mode_name(led_ctrl_get_mode()),
        (unsigned)BTN_APP_POLL_MS);

  /* Infinite loop */
  for(;;)
  {
    /* 按键轮询：周期必须等于 MultiButton 的 TICKS_INTERVAL（5 ms），
     * 否则单击/双击/长按的时长判定会整体偏移（btn_app.c 有编译期检查）。 */
    btn_app_process();

    /* 每 2000 次（约 10 s）报一次任务栈余量，用于验证栈深度是否足够。
     * 需要 INCLUDE_uxTaskGetStackHighWaterMark = 1（见 FreeRTOSConfig.h）。
     * 余量稳定后本段可直接删除。 */
    if ((++poll_cnt % 2000U) == 0U)
    {
      log_d("BtnTask stack high water mark = %lu words",
            (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }

    /* 实际周期 = 5 ms + 按键事件日志的串口发送耗时（约 7 ms，仅按键时发生） */
    osDelay(BTN_APP_POLL_MS);
  }
  /* USER CODE END BtnTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
