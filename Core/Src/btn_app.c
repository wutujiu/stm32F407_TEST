/**
  ******************************************************************************
  * @file    btn_app.c
  * @brief   PA15 按键控制 PC13 LED 多模式闪烁
  *
  * 按键映射：
  *   单击            → 下一个模式
  *   双击            → 上一个模式
  *   长按（≥1 s）    → 回到模式 1（SLOW），即"恢复默认"
  *   长按保持        → 每 300 ms 快速滚动模式（驱动每 5 ms 回调一次，需自行节流）
  *   按下 / 松开 / 连击 → 只打日志，用于验证硬件链路与事件流
  ******************************************************************************
  */

/* LOG_TAG / LOG_LVL 必须在包含 <elog.h> 之前定义 */
#define LOG_TAG    "btn"
#define LOG_LVL    ELOG_LVL_DEBUG
#include <elog.h>

#include "btn_app.h"
#include "led_ctrl.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

/*----------------------------------------------------------------------------*/
/* 编译期前提检查                                                              */
/*----------------------------------------------------------------------------*/

/* 轮询周期与驱动时间基准必须一致，否则按键时长判定整体偏移。
 * 这里是编译期检查（两个宏都是纯字面量），运行期还会再查一次 osKernelSysTick
 * 频率与 FreeRTOS tick 频率是否一致（见 btn_app_init()）。 */
#if (BTN_APP_POLL_MS != TICKS_INTERVAL)
#error "BTN_APP_POLL_MS must equal MultiButton's TICKS_INTERVAL"
#endif

/* configTICK_RATE_HZ 是 ((TickType_t)1000) 带强制转换的表达式，不能用于 #if，
 * 所以 tick 频率的检查放到 btn_app_init() 里做（configTICK_RATE_HZ 与
 * osKernelSysTickFrequency 都必须是 1000，即 1 tick == 1 ms）。 */

/*----------------------------------------------------------------------------*/
/* 可调参数                                                                    */
/*----------------------------------------------------------------------------*/

#define BTN_ID              0U      /* 按键编号，单按键固定 0 */

/* PA15 按键的有效电平：
 *   0 = 按下为低（按键接 GND，PA15 用内部上拉）—— 默认，最常见的接法
 *   1 = 按下为高（按键接 3.3V，需把 gpio.c 里 PA15 的 PULLUP 改成 PULLDOWN）
 *
 * 板子接法不确定也没关系：上电日志会打印 PA15 的空闲电平，
 * 若显示 "wiring/pull mismatch" 就按日志提示把本宏取反。 */
#define BTN_ACTIVE_LEVEL    0U

/* 长按保持的节流周期：驱动在 LONG_HOLD 状态下每 5 ms 回调一次，
 * 不节流的话一秒能滚 200 个模式。 */
#define BTN_HOLD_STEP_MS    300U

/*----------------------------------------------------------------------------*/
/* 状态                                                                       */
/*----------------------------------------------------------------------------*/

static Button   s_btn;              /* MultiButton 句柄 */
static uint32_t s_press_tick;       /* PRESS_DOWN 时刻，用于算按住时长 */
static uint32_t s_hold_tick;        /* LONG_PRESS_HOLD 的上次动作时刻（节流用） */

/*----------------------------------------------------------------------------*/
/* 电平读取：MultiButton 通过函数指针回调本函数取引脚状态                     */
/*----------------------------------------------------------------------------*/

static uint8_t btn_read_level(uint8_t button_id)
{
    (void)button_id;    /* 单按键，编号用不上 */

    return (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

/*----------------------------------------------------------------------------*/
/* 事件回调（全部在 BtnTask 上下文执行，可以自由打日志）                       */
/*----------------------------------------------------------------------------*/

static void on_press_down(Button *handle, void *user_data)
{
    (void)handle;
    (void)user_data;

    s_press_tick = HAL_GetTick();
    log_d("event: PRESS_DOWN (pin level = %u)", (unsigned)btn_read_level(BTN_ID));
}

static void on_press_up(Button *handle, void *user_data)
{
    (void)handle;
    (void)user_data;

    log_d("event: PRESS_UP (held %lu ms)",
          (unsigned long)(HAL_GetTick() - s_press_tick));
}

static void on_press_repeat(Button *handle, void *user_data)
{
    (void)user_data;

    log_i("event: PRESS_REPEAT x%u", (unsigned)button_get_repeat_count(handle));
}

static void on_single_click(Button *handle, void *user_data)
{
    (void)handle;
    (void)user_data;

    led_ctrl_next_mode();
    log_i("event: SINGLE_CLICK -> mode %u (%s)",
          (unsigned)led_ctrl_get_mode(),
          led_ctrl_mode_name(led_ctrl_get_mode()));
}

static void on_double_click(Button *handle, void *user_data)
{
    (void)handle;
    (void)user_data;

    led_ctrl_prev_mode();
    log_i("event: DOUBLE_CLICK -> mode %u (%s)",
          (unsigned)led_ctrl_get_mode(),
          led_ctrl_mode_name(led_ctrl_get_mode()));
}

static void on_long_press_start(Button *handle, void *user_data)
{
    (void)handle;
    (void)user_data;

    led_ctrl_set_mode(LED_MODE_SLOW);
    s_hold_tick = HAL_GetTick();
    log_i("event: LONG_PRESS_START -> mode %u (%s) reset",
          (unsigned)led_ctrl_get_mode(),
          led_ctrl_mode_name(led_ctrl_get_mode()));
}

static void on_long_press_hold(Button *handle, void *user_data)
{
    uint32_t now = HAL_GetTick();

    (void)handle;
    (void)user_data;

    /* 驱动在 LONG_HOLD 状态下每 5 ms 就回调一次，必须自己节流 */
    if ((uint32_t)(now - s_hold_tick) < BTN_HOLD_STEP_MS) {
        return;
    }
    s_hold_tick = now;

    led_ctrl_next_mode();
    log_i("event: LONG_PRESS_HOLD -> mode %u (%s)",
          (unsigned)led_ctrl_get_mode(),
          led_ctrl_mode_name(led_ctrl_get_mode()));
}

/*----------------------------------------------------------------------------*/
/* 对外接口                                                                    */
/*----------------------------------------------------------------------------*/

void btn_app_init(void)
{
    uint8_t idle_level;

    /* tick 频率必须是 1 kHz，osDelay(BTN_APP_POLL_MS) 才是"毫秒"，
     * 按键时长判定才成立（宏带强制转换，没法用 #if 检查，只能运行期查）。 */
    if ((uint32_t)configTICK_RATE_HZ != 1000U) {
        log_w("configTICK_RATE_HZ = %lu != 1000: osDelay(%u) is not %u ms, key timing will be off",
              (unsigned long)configTICK_RATE_HZ,
              (unsigned)BTN_APP_POLL_MS, (unsigned)BTN_APP_POLL_MS);
    }

    idle_level = btn_read_level(BTN_ID);

    /* 接法自检：空闲电平 = 按键松开时的引脚电平。它与 BTN_ACTIVE_LEVEL 相等，
     * 说明"松开的电平"和"按下的电平"被当成了同一个 → 接法/上下拉与假设相反，
     * 或者上电瞬间按键正被按住。这是硬件接法未知时唯一的判据来源。 */
    if (idle_level == BTN_ACTIVE_LEVEL) {
        log_w("PA15 idle level = %u equals BTN_ACTIVE_LEVEL (%u): wiring/pull mismatch "
              "(or the key was held down at boot)",
              (unsigned)idle_level, (unsigned)BTN_ACTIVE_LEVEL);
        log_w("PA15 is stuck at the 'pressed' level -> set BTN_ACTIVE_LEVEL to %u in btn_app.c "
              "and use the matching pull in gpio.c",
              (unsigned)(BTN_ACTIVE_LEVEL ^ 1U));
    } else {
        log_i("PA15 idle level = %u, BTN_ACTIVE_LEVEL = %u -> wiring OK",
              (unsigned)idle_level, (unsigned)BTN_ACTIVE_LEVEL);
    }

    log_i("button timing: debounce %u x %u ms, short click >= %u ms, long press >= %u ms, poll %u ms",
          (unsigned)DEBOUNCE_TICKS, (unsigned)TICKS_INTERVAL,
          (unsigned)(SHORT_TICKS * TICKS_INTERVAL),
          (unsigned)(LONG_TICKS * TICKS_INTERVAL),
          (unsigned)BTN_APP_POLL_MS);

    button_init(&s_btn, btn_read_level, BTN_ACTIVE_LEVEL, BTN_ID);

    button_attach(&s_btn, BTN_PRESS_DOWN,        on_press_down,       NULL);
    button_attach(&s_btn, BTN_PRESS_UP,          on_press_up,         NULL);
    button_attach(&s_btn, BTN_PRESS_REPEAT,      on_press_repeat,     NULL);
    button_attach(&s_btn, BTN_SINGLE_CLICK,      on_single_click,     NULL);
    button_attach(&s_btn, BTN_DOUBLE_CLICK,      on_double_click,     NULL);
    button_attach(&s_btn, BTN_LONG_PRESS_START,  on_long_press_start, NULL);
    button_attach(&s_btn, BTN_LONG_PRESS_HOLD,   on_long_press_hold,  NULL);

    if (button_start(&s_btn) != 0) {
        log_e("button_start() failed, PA15 key will not work");
    }
}

void btn_app_process(void)
{
    button_ticks();
}
