/**
  ******************************************************************************
  * @file    led_ctrl.c
  * @brief   PC13 板载 LED 的多模式闪烁引擎
  *
  * 实现要点：
  *   1. 离散闪烁模式（OFF/SLOW/FAST/DOUBLE/SOS）统一用「段表」描述：一段 =
  *      持续时间 + 亮/灭，走到表尾自动回到表头，天然形成周期。新增模式只需
  *      加一张段表 + 一个枚举值 + PATTERNS 里一行。
  *   2. 呼吸灯走单独分支：三角波调制度（0→20→0，周期 2 s）叠加 20 ms 周期、
  *      20 级软件 PWM。PC13 在 STM32F407 上没有定时器复用功能（.ioc 里它是
  *      PC13-ANTI_TAMP，只有 RTC/TAMPER），拿不到硬件 PWM，只能用软件方案。
  *   3. 本文件运行在中断上下文（见 led_ctrl.h 的说明），不 include elog.h，
  *      不调用任何日志接口。
  ******************************************************************************
  */

#include "led_ctrl.h"
#include "main.h"

/*----------------------------------------------------------------------------*/
/* 可调参数                                                                   */
/*----------------------------------------------------------------------------*/

#define LED_TICK_MS         1U    /* led_ctrl_tick_1ms() 的调用周期（ms）      */
#define LED_PWM_PERIOD_MS   20U   /* 软件 PWM 周期 → 50 Hz，肉眼看不到闪       */
#define LED_PWM_LEVELS      (LED_PWM_PERIOD_MS / LED_TICK_MS)  /* 20 级亮度    */
#define LED_BREATH_MS       2000U /* 呼吸一个完整周期（ms）                    */
#define LED_BREATH_HALF_MS  (LED_BREATH_MS / 2U)               /* 单调上升段   */

#define ARRAY_LEN(a)        (sizeof(a) / sizeof((a)[0]))

/*----------------------------------------------------------------------------*/
/* 段表：离散闪烁模式                                                          */
/*----------------------------------------------------------------------------*/

typedef struct {
    uint16_t ms;    /*!< 本段持续时间（ms），必须 > 0 */
    uint8_t  on;    /*!< 本段电平：1 = 点亮，0 = 熄灭 */
} LedStep;

static const LedStep STEPS_OFF[] = {
    { 1000U, 0U }
};

static const LedStep STEPS_SLOW[] = {
    { 500U, 1U }, { 500U, 0U }
};

static const LedStep STEPS_FAST[] = {
    { 100U, 1U }, { 100U, 0U }
};

/* 双闪：闪两下（80 ms 亮 / 120 ms 灭）后长静默，整体 1 s 一个周期 */
static const LedStep STEPS_DOUBLE[] = {
    {  80U, 1U }, { 120U, 0U },
    {  80U, 1U }, { 120U, 0U },
    { 600U, 0U }
};

/* SOS：三短（150 ms）三长（450 ms）三短，段间隔 150 ms，
 * 字母之间 450 ms，周期尾部静默 1050 ms。总周期 4.2 s。 */
static const LedStep STEPS_SOS[] = {
    /* S */ { 150U, 1U }, { 150U, 0U },
    /* S */ { 150U, 1U }, { 150U, 0U },
    /* S */ { 150U, 1U }, { 450U, 0U },
    /* O */ { 450U, 1U }, { 150U, 0U },
    /* O */ { 450U, 1U }, { 150U, 0U },
    /* O */ { 450U, 1U }, { 450U, 0U },
    /* S */ { 150U, 1U }, { 150U, 0U },
    /* S */ { 150U, 1U }, { 150U, 0U },
    /* S */ { 150U, 1U }, { 1050U, 0U }
};

typedef struct {
    const LedStep *steps;   /*!< 段表首地址，NULL = 该模式不用段表（呼吸灯） */
    uint16_t       count;   /*!< 段数 */
} LedPattern;

/* 顺序必须与 LedMode 枚举一致，末尾有 _Static_assert 兜底 */
static const LedPattern PATTERNS[LED_MODE_COUNT] = {
    { STEPS_OFF,    ARRAY_LEN(STEPS_OFF)    },   /* LED_MODE_OFF    */
    { STEPS_SLOW,   ARRAY_LEN(STEPS_SLOW)   },   /* LED_MODE_SLOW   */
    { STEPS_FAST,   ARRAY_LEN(STEPS_FAST)   },   /* LED_MODE_FAST   */
    { NULL,         0U                      },   /* LED_MODE_BREATH */
    { STEPS_DOUBLE, ARRAY_LEN(STEPS_DOUBLE) },   /* LED_MODE_DOUBLE */
    { STEPS_SOS,    ARRAY_LEN(STEPS_SOS)    }    /* LED_MODE_SOS    */
};

_Static_assert(ARRAY_LEN(PATTERNS) == (size_t)LED_MODE_COUNT,
               "PATTERNS table must have one entry per LedMode");

static const char *const MODE_NAMES[LED_MODE_COUNT] = {
    "OFF", "SLOW", "FAST", "BREATH", "DOUBLE", "SOS"
};

/*----------------------------------------------------------------------------*/
/* 状态                                                                       */
/*----------------------------------------------------------------------------*/

/* s_mode / s_restart 由任务侧写、中断侧读，单字节访问天然原子，无需临界区。
 * 其余状态一律只由中断侧读写，避免跨上下文竞态。 */
static volatile LedMode  s_mode    = LED_MODE_OFF;  /* 初始化前保持熄灭        */
static volatile uint8_t  s_restart = 1U;            /* 请求从段 0 重新开始     */

static uint16_t s_step;         /* 当前段号                                     */
static uint16_t s_step_ms;      /* 当前段已走过的毫秒数                         */
static uint16_t s_pwm_cnt;      /* 软件 PWM 计数器 0..LED_PWM_PERIOD_MS-1       */
static uint16_t s_phase_ms;     /* 呼吸灯相位 0..LED_BREATH_MS-1                */
static uint8_t  s_led_on;       /* 已写入 PC13 的电平（0/1），0xFF = 未知       */
static uint8_t  s_ready;        /* 0 = led_ctrl_init() 尚未调用，tick 直接返回  */

/*----------------------------------------------------------------------------*/
/* 内部函数                                                                   */
/*----------------------------------------------------------------------------*/

/**
  * @brief  写 PC13。板载 LED 低电平点亮，且只在电平变化时写寄存器
  *         （1 kHz 下省掉无谓的 BSRR 写）
  */
static void led_write(uint8_t on)
{
    on = (on != 0U) ? 1U : 0U;

    if (s_led_on == on) {
        return;
    }
    s_led_on = on;

    HAL_GPIO_WritePin(LED_GPIO_Port, LED_PIN,
                      (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/*----------------------------------------------------------------------------*/
/* 对外接口                                                                   */
/*----------------------------------------------------------------------------*/

void led_ctrl_init(void)
{
    /* 所有引擎状态在中断里重置，这里只需要置位门闩并指定默认模式。
     * s_led_on 置 0xFF（未知）：MX_GPIO_Init() 把 PC13 写成 RESET（点亮），
     * 若这里认为"当前是灭"，首次 led_write(1) 会被"电平未变"提前返回，
     * LED 就卡在常亮。 */
    s_led_on  = 0xFFU;
    s_mode    = LED_MODE_SLOW;
    s_restart = 1U;
    s_ready   = 1U;
}

void led_ctrl_tick_1ms(void)
{
    const LedPattern *pattern;
    uint16_t tri;
    uint16_t duty;

    if (s_ready == 0U) {
        return;     /* 中断早于 MX_GPIO_Init() 就开始跑了，此时 GPIOC 时钟未开 */
    }

    /* 模式切换请求：把全部引擎状态归零，让新模式从段 0 干净起步 */
    if (s_restart != 0U) {
        s_restart  = 0U;
        s_step     = 0U;
        s_step_ms  = 0U;
        s_pwm_cnt  = 0U;
        s_phase_ms = 0U;
    }

    if (s_mode == LED_MODE_BREATH) {
        /* 三角波调制度：0 → LED_PWM_LEVELS → 0，再按 20 ms 周期做软件 PWM */
        s_phase_ms++;
        if (s_phase_ms >= LED_BREATH_MS) {
            s_phase_ms = 0U;
        }

        tri = (s_phase_ms < LED_BREATH_HALF_MS)
                  ? s_phase_ms
                  : (uint16_t)(LED_BREATH_MS - s_phase_ms);

        duty = (uint16_t)(((uint32_t)tri * LED_PWM_LEVELS) / LED_BREATH_HALF_MS);

        s_pwm_cnt++;
        if (s_pwm_cnt >= LED_PWM_PERIOD_MS) {
            s_pwm_cnt = 0U;
        }

        led_write((s_pwm_cnt < duty) ? 1U : 0U);
        return;
    }

    pattern = &PATTERNS[(uint32_t)s_mode];

    if (pattern->steps == NULL) {
        return;     /* 段表模式下不该出现，防御性返回 */
    }

    /* 兜底：任务写 s_mode 与 s_restart 之间有一拍窗口，中断可能读到"新模式 +
     * 旧段号"，而新模式的段数更少 → 越界。这里强制回到段 0。 */
    if (s_step >= pattern->count) {
        s_step    = 0U;
        s_step_ms = 0U;
    }

    /* 先推进段计时、再按新段写电平。用 while 而不是 if，这样即使某段短于
     * 一个 tick 也不会把该段整个跳过。 */
    s_step_ms = (uint16_t)(s_step_ms + LED_TICK_MS);
    while (s_step_ms >= pattern->steps[s_step].ms) {
        s_step_ms = (uint16_t)(s_step_ms - pattern->steps[s_step].ms);
        s_step++;
        if (s_step >= pattern->count) {
            s_step = 0U;    /* 表尾回到表头 → 周期循环 */
        }
    }

    led_write(pattern->steps[s_step].on);
}

void led_ctrl_set_mode(LedMode mode)
{
    if ((uint32_t)mode >= (uint32_t)LED_MODE_COUNT) {
        return;     /* 越界值忽略 */
    }

    s_mode    = mode;
    s_restart = 1U;
}

void led_ctrl_next_mode(void)
{
    uint32_t next = (uint32_t)s_mode + 1U;

    led_ctrl_set_mode((next >= (uint32_t)LED_MODE_COUNT) ? LED_MODE_OFF
                                                         : (LedMode)next);
}

void led_ctrl_prev_mode(void)
{
    uint32_t prev = (uint32_t)s_mode;

    led_ctrl_set_mode((prev == 0U) ? (LedMode)(LED_MODE_COUNT - 1U)
                                   : (LedMode)(prev - 1U));
}

LedMode led_ctrl_get_mode(void)
{
    return s_mode;
}

const char *led_ctrl_mode_name(LedMode mode)
{
    if ((uint32_t)mode >= (uint32_t)LED_MODE_COUNT) {
        return "UNKNOWN";
    }
    return MODE_NAMES[(uint32_t)mode];
}
