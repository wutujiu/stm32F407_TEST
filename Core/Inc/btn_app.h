/**
  ******************************************************************************
  * @file    btn_app.h
  * @brief   PA15 按键应用层：MultiButton 驱动与 LED 模式引擎之间的粘合
  *
  * 职责划分：
  *   - btn_app.c   按键事件 → 动作（切模式）+ 日志，跑在任务上下文
  *   - led_ctrl.c  LED 闪烁/呼吸的实际时序，跑在 1 ms 中断上下文
  *
  * 线程安全前提：MultiButton 的按键 API 只在 BtnTask 一个任务里调用，
  * 因此不启用 MULTIBUTTON_THREAD_SAFE。若将来有第二个任务要调用按键接口，
  * 必须补上锁（见 Middlewares/MultiButton/CMakeLists.txt 的说明）。
  ******************************************************************************
  */

#ifndef BTN_APP_H
#define BTN_APP_H

/* TICKS_INTERVAL：MultiButton 状态机的时间基准（ms），当前为 5。
 * 轮询周期必须严格等于它，否则单击/双击/长按的时长判定全部失准，
 * 所以这里直接引用宏而不是另写一个数字。 */
#include "multi_button.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 按键轮询周期（ms），与 MultiButton 的 TICKS_INTERVAL 绑定 */
#define BTN_APP_POLL_MS     TICKS_INTERVAL

/**
  * @brief  初始化 PA15 按键：接法自检 + button_init/attach/start
  * @note   在调度器启动后、BtnTask 首次循环前调用。
  *         会打印 PA15 空闲电平，用于确认硬件接法是否与 BTN_ACTIVE_LEVEL 一致。
  */
void btn_app_init(void);

/**
  * @brief  按键轮询，每 BTN_APP_POLL_MS 毫秒调用一次（内部即 button_ticks()）
  * @note   事件回调在**调用者上下文**执行，因此必须在任务里调用，
  *         不能在中断里调用（回调会打日志）。
  */
void btn_app_process(void);

#ifdef __cplusplus
}
#endif

#endif /* BTN_APP_H */
