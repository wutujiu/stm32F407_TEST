/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: Portable interface for each platform.
 * Created on: 2015-04-28
 *
 * ---------------------------------------------------------------------------
 * 本项目移植实现：STM32F407ZGT6 + FreeRTOS(CMSIS-RTOS v1) + USART1
 *
 *   - 输出方式：同步阻塞发送（HAL_UART_Transmit），未启用异步/缓冲模式
 *   - 输出锁  ：FreeRTOS 互斥量（带优先级继承），而非关中断的临界区
 *   - 时间戳  ：HAL_GetTick()（TIM1 1 ms 时基）
 *   - 线程名  ：FreeRTOS 任务名（pcTaskGetName）
 *
 * 限制：日志接口只能在【任务上下文】调用。在中断里调用会触发 FreeRTOS
 *       configASSERT（关中断死循环）。若将来需要中断中打日志，请改用异步
 *       模式并在 elog_async_output_notice() 中使用 FromISR 版本 API。
 * ---------------------------------------------------------------------------
 */

/* 移植层自身日志的 tag（elog.c 内部也已定义同名 tag，此处仅用于本文件） */
#define LOG_TAG      "elog"

#include <elog.h>
#include <stdio.h>
#include "usart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/*
 * USART1 发送超时（ms）。
 * ELOG_LINE_BUF_SIZE = 256 时最长一帧 256 字节，115200 8N1 约需 23 ms，留足余量。
 * 超时后该行日志被丢弃，不影响调用任务（HAL_UART_Transmit 返回 HAL_TIMEOUT）。
 */
#define ELOG_PORT_TX_TIMEOUT_MS     100U

/* 输出锁：保护串口与 elog 内部行缓冲（elog.c 的 log_buf） */
static SemaphoreHandle_t elog_output_mutex = NULL;

/**
 * EasyLogger port initialize
 *
 * @return result
 */
ElogErrCode elog_port_init(void) {
    ElogErrCode result = ELOG_NO_ERR;

    /* 在调度器启动前创建互斥量是允许的（非阻塞操作） */
    elog_output_mutex = xSemaphoreCreateMutex();
    /* 说明：ElogErrCode 枚举只有 ELOG_NO_ERR 一个值，无法上报失败；
     * 分配失败时下面的 lock/unlock 会退化为"无锁"，日志仍可输出。
     * 此时 FreeRTOS 堆刚初始化、余量充足，实际不会发生。 */

    return result;
}

/**
 * EasyLogger port deinitialize
 *
 */
void elog_port_deinit(void) {

    if (elog_output_mutex != NULL) {
        vSemaphoreDelete(elog_output_mutex);
        elog_output_mutex = NULL;
    }

}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char *log, size_t size) {

    if ((log == NULL) || (size == 0U)) {
        return;
    }
    /* 日志落地的唯一出口：阻塞发送。
     * size 受 ELOG_LINE_BUF_SIZE(256) 约束，转 uint16_t 安全。 */
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)log, (uint16_t)size, ELOG_PORT_TX_TIMEOUT_MS);

}

/**
 * output lock
 */
void elog_port_output_lock(void) {

    /* 调度器尚未启动时（elog_start() 的启动横幅在 osKernelStart() 之前打印）
     * 不操作 FreeRTOS 对象：此时只有主线程在运行，不存在竞争。
     * 注意必须与 unlock 的判据完全一致，避免"未取却给"的失配。 */
    if ((elog_output_mutex != NULL) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreTake(elog_output_mutex, portMAX_DELAY);
    }

}

/**
 * output unlock
 */
void elog_port_output_unlock(void) {

    if ((elog_output_mutex != NULL) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreGive(elog_output_mutex);
    }

}

/**
 * get current time interface
 *
 * @return current time
 */
const char *elog_port_get_time(void) {

    /* HAL_GetTick() 毫秒计数 → "时:分:秒.毫秒"，如 "000:00:12.345"
     * （开机约 49.7 天后回绕，属正常现象） */
    static char time_str[16];
    uint32_t ms = HAL_GetTick();

    (void)snprintf(time_str, sizeof(time_str), "%03lu:%02lu:%02lu.%03lu",
                   (unsigned long)(ms / 3600000UL),
                   (unsigned long)((ms / 60000UL) % 60UL),
                   (unsigned long)((ms / 1000UL) % 60UL),
                   (unsigned long)(ms % 1000UL));

    return time_str;

}

/**
 * get current process name interface
 *
 * @return current process name
 */
const char *elog_port_get_p_info(void) {

    /* 裸机没有"进程"概念，返回空串（日志格式中该项已通过 elog_set_fmt 关闭） */
    return "";

}

/**
 * get current thread name interface
 *
 * @return current thread name
 */
const char *elog_port_get_t_info(void) {

    /* 多任务工程中直接输出 FreeRTOS 任务名，便于分辨日志来源 */
    return (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) ? "main" : pcTaskGetName(NULL);

}
