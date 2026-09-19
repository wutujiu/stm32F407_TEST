/**
  ******************************************************************************
  * @file    led_ctrl.h
  * @brief   PC13 板载 LED 的多模式闪烁引擎（软件 PWM 呼吸灯 + 段表闪烁）
  *
  * 时基：led_ctrl_tick_1ms() 必须由 1 ms 周期的中断调用（本工程挂在 TIM1 更新
  *       中断，即 HAL 时基中断里）。之所以放在中断而不是任务里：按键回调要打
  *       日志，一条 85 字节日志 @115200 阻塞约 7 ms，若引擎跑在任务里，每次
  *       按键都会让 PWM 与闪烁时序停 7 ms（呼吸灯会出现可见顿挫）。
  *
  * 约束：本模块运行在中断上下文，**不得调用任何日志接口**（EasyLogger 的同步
  *       输出走 FreeRTOS 互斥量，在中断里会触发 configASSERT 关中断死循环）。
  *       因此本文件不 include elog.h，全部状态靠"模式名"接口在任务侧打印。
  ******************************************************************************
  */

#ifndef LED_CTRL_H
#define LED_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 闪烁模式。顺序即单击切换的顺序，PATTERNS 表按同样顺序初始化 */
typedef enum {
    LED_MODE_OFF = 0,   /*!< 常灭                                        */
    LED_MODE_SLOW,      /*!< 500 ms 亮 / 500 ms 灭（1 Hz）               */
    LED_MODE_FAST,      /*!< 100 ms 亮 / 100 ms 灭（5 Hz）               */
    LED_MODE_BREATH,    /*!< 2 s 周期呼吸灯（20 ms 周期 / 20 级软件 PWM） */
    LED_MODE_DOUBLE,    /*!< 双闪两下 + 长静默（1 s 周期）                */
    LED_MODE_SOS,       /*!< 三短三长三短 + 静默（4.2 s 周期）            */
    LED_MODE_COUNT      /*!< 模式总数，非有效模式                         */
} LedMode;

/**
  * @brief  初始化 LED 引擎：进入默认模式（LED_MODE_SLOW）并放行 tick
  * @note   必须在中断开始调用 tick 之前调用（本工程在 BtnTask 里、调度器启动后）。
  *         tick 在 s_ready 置位前直接返回，避免在 MX_GPIO_Init() 之前访问
  *         未开时钟的 GPIOC。
  */
void led_ctrl_init(void);

/**
  * @brief  LED 引擎节拍，1 ms 调用一次（TIM1 更新中断内，中断上下文）
  */
void led_ctrl_tick_1ms(void);

/**
  * @brief  直接设置模式（任务上下文）
  * @param  mode: 目标模式，越界值忽略
  */
void led_ctrl_set_mode(LedMode mode);

/** @brief 切到下一个模式，末尾回到 LED_MODE_OFF（任务上下文） */
void led_ctrl_next_mode(void);

/** @brief 切到上一个模式，LED_MODE_OFF 回到末尾（任务上下文） */
void led_ctrl_prev_mode(void);

/** @brief 取当前模式（任务上下文） */
LedMode led_ctrl_get_mode(void);

/**
  * @brief  取模式名，用于日志
  * @param  mode: 模式
  * @retval 静态字符串，越界返回 "UNKNOWN"
  */
const char *led_ctrl_mode_name(LedMode mode);

#ifdef __cplusplus
}
#endif

#endif /* LED_CTRL_H */
