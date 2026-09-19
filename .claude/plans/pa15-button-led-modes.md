# PA15 按键控制 PC13 LED 多模式闪烁 — 实施方案

## 一、现状勘察结论

| 项 | 现状 | 结论 |
| --- | --- | --- |
| MultiButton | `Middlewares/MultiButton/`（`multi_button.c/h`，v1.1.1）已拷进工程，但 **git 未跟踪、未加入 CMake 构建**，全工程无任何 `#include "multi_button.h"` | 需完成：构建接入 + 应用层 |
| PA15 | 已配成 `GPIO_MODE_INPUT` + **`GPIO_NOPULL`**（[gpio.c:62-66](Core/Src/gpio.c#L62-L66)） | 空闲电平悬空 → 必须补内部上下拉 |
| PA15 = JTDI | **F4 上无需任何 SWJ 重映射**。已确认：`Drivers/` 里不存在 `__HAL_RCC_AFIO_CLK_ENABLE` / `AFIO_MAPR_SWJ_CFG`（那是 F1 的机制），CubeMX 也只生成了普通 `GPIO_MODE_INPUT`。SWD 只用 PA13/PA14，把 PA15 配成 GPIO 不影响调试 | 无需改 .ioc 的调试口配置 |
| PC13 | 推挽输出、初值 `RESET`（**低电平点亮**，[gpio.c:53-60](Core/Src/gpio.c#L53-L60)） | 无需改；注意上电瞬间是"亮"的 |
| PC13 硬件 PWM | PC13 在 F407 上**没有定时器 AF**（.ioc 里它叫 `PC13-ANTI_TAMP`，只有 RTC/TAMPER 功能） | 呼吸灯只能**软件 PWM** |
| `ledTask` | 优先级 `osPriorityIdle`、栈 256 字（[freertos.c:115](Core/Src/freertos.c#L115)）；但 **.ioc 里写的是 128**（[STM32F407_TEST.ioc:7](STM32F407_TEST.ioc#L7)），重新生成代码会掉回 128 → 栈溢出 | 顺手对齐 |
| `osThreadDef` 栈单位 | 已确认 `cmsis_os.c:osThreadCreate()` 把 `thread_def->stacksize` **原样**传给 `xTaskCreate()`，即**字（word）** | 256 = 1 KB |
| CMSIS 优先级映射 | `makeFreeRtosPriority()` = `tskIDLE_PRIORITY + (priority - osPriorityIdle)`；`osPriorityIdle(-3)` → FreeRTOS 优先级 **0，与空闲任务同级** | 按键任务不能再用 Idle |
| 1 ms 时基 | TIM1 作为 HAL 时基（[stm32f4xx_hal_timebase_tim.c](Core/Src/stm32f4xx_hal_timebase_tim.c)），`HAL_Init()` 里就使能了中断，`HAL_TIM_PeriodElapsedCallback()` 有 `USER CODE BEGIN/END Callback 1` 保留区（[main.c:195-197](Core/Src/main.c#L195-L197)） | 可挂 1 ms 软件 PWM |
| 日志 | EasyLogger 同步阻塞，单条 ~85 B ≈ **7 ms** @115200，只能在任务上下文调用 | 中断里绝不能打日志 |
| 构建环境 | `build/Debug` 有可用产物，基线 `text 14940 / data 16 / bss 18016` | 可编译可对比 |

### 已确认的需求（你已选定）

- **6 种闪烁模式**：`0=OFF`、`1=SLOW(1 Hz)`、`2=FAST(5 Hz)`、`3=BREATH(呼吸)`、`4=DOUBLE(双闪)`、`5=SOS(三短三长三短)`
- **按键映射**：单击→下一模式、双击→上一模式、长按 1 s→回到模式 1、长按保持→每 300 ms 快速滚动模式；按下/松开/连击只打日志（演示事件流）
- **验证**：允许我用 `openocd` 烧录；串口输出需要你贴给我（我看不到 COM 口）
- **硬件接法未知**（鹿小班经典 F407 板，PA15 按键）→ 见下面第五节的自检方案

---

## 二、架构与数据流

```
        ┌──────────────────── BtnTask（任务上下文，优先级 Normal，栈 256 字） ────────────────────┐
        │  osDelay(5) ──► btn_app_process() ──► button_ticks() ──► MultiButton 状态机            │
        │                                              │                                          │
        │                                              ▼ 7 个事件回调（跑在 BtnTask 里）          │
        │                                    led_ctrl_next_mode() / prev_mode() / set_mode()     │
        │                                              │ 只写 volatile uint8_t s_mode + s_restart │
        │                                              ▼                                          │
        │                                    log_i("event: ... -> mode N (NAME)")                 │
        └──────────────────────────────────────────────┼──────────────────────────────────────────┘
                                                       │ 单字节原子写，无锁
        ┌──────────────────────────────────────────────┼──────────────────────────────────────────┐
        │  TIM1 更新中断（1 kHz，优先级 0，HAL_IncTick 的同一个中断）                              │
        │  led_ctrl_tick_1ms() ──► 段表状态机（闪烁模式）/ 三角波（呼吸）+ 20 级软件 PWM ──► PC13   │
        └─────────────────────────────────────────────────────────────────────────────────────────┘
```

**为什么 LED 引擎放中断、不放任务**：按钮回调里要打日志，一条 ~85 B 日志 @115200 阻塞 **7 ms**。
如果 LED 引擎也在 BtnTask 里，每次按键都会让 PWM 和闪烁时序停 7 ms（呼吸灯会看到明显顿挫）。
放 TIM1 中断后完全不受影响，且 1 kHz 的粒度才够做 20 级 PWM。
代价：中断里**不能调用任何日志接口**（本工程 README 已明确此约束），所以中断侧代码零日志。

---

## 三、改动清单（10 处）

### 1. 新增 [Middlewares/MultiButton/CMakeLists.txt](Middlewares/MultiButton/CMakeLists.txt) — 组件自带构建脚本

```cmake
# MultiButton 按键驱动（第三方，MIT，https://github.com/0x1abin/MultiButton）
add_library(MultiButton STATIC multi_button.c)
target_include_directories(MultiButton PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# 预留：若将来在 multi_button.h 前定义 MULTIBUTTON_THREAD_SAFE，需要 FreeRTOS 头文件
target_link_libraries(MultiButton PUBLIC stm32cubemx)
```

### 2. [CMakeLists.txt](CMakeLists.txt) — 顶层接入（唯一不会被 CubeMX 覆盖的位置）

```cmake
# ---- MultiButton 按键驱动 ----
add_subdirectory(Middlewares/MultiButton)

# ---- 用户应用源码（CubeMX 只生成 Core/Src 下的外设文件，这两个是手写的）----
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/btn_app.c
    Core/Src/led_ctrl.c
)
```
并在文件末尾的 `target_link_libraries(${CMAKE_PROJECT_NAME} ...)` 里补 `MultiButton`。

> `cmake/stm32cubemx/CMakeLists.txt` 每次重新生成都会被覆盖，所以新源文件**必须**写在顶层。
> 这与 EasyLogger 的接入方式一致。

### 3. [Core/Src/gpio.c](Core/Src/gpio.c) `USER CODE BEGIN 2` — 给 PA15 补内部上拉

```c
  /* PA15 按键：CubeMX 生成的是 NOPULL（悬空），这里改成内部上拉，让空闲电平确定为高。
     按键接 GND 时按下为低 → 与 btn_app.c 的 BTN_ACTIVE_LEVEL = 0 对应。
     写在 USER CODE 区，重新生成代码不会丢；若板上已有外部上拉，内部上拉与之并存无害。 */
  GPIO_InitStruct.Pin  = GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
```

> 用 `USER CODE` 覆盖而不是直接改 `MX_GPIO_Init()` 生成段，是为了**重新生成代码后依然生效**。
> （想彻底改干净的话，在 CubeMX GUI 的 GPIO 视图里把 PA15 的 Pull-up 打开即可，会同步写进 .ioc。）

### 4. [Core/Inc/main.h](Core/Inc/main.h) `USER CODE BEGIN Private defines` — 引脚宏

```c
#define BTN_PIN            GPIO_PIN_15
#define BTN_GPIO_Port      GPIOA
#define LED_PIN            GPIO_PIN_13
#define LED_GPIO_Port      GPIOC
```

### 5. 新增 [Core/Inc/led_ctrl.h](Core/Inc/led_ctrl.h) + [Core/Src/led_ctrl.c](Core/Src/led_ctrl.c) — LED 模式引擎

接口：

```c
typedef enum {
    LED_MODE_OFF = 0,   /* 常灭 */
    LED_MODE_SLOW,      /* 500 ms 亮 / 500 ms 灭 */
    LED_MODE_FAST,      /* 100 ms 亮 / 100 ms 灭 */
    LED_MODE_BREATH,    /* 2 s 周期呼吸（软件 PWM） */
    LED_MODE_DOUBLE,    /* 双闪 + 长静默 */
    LED_MODE_SOS,       /* 三短三长三短 */
    LED_MODE_COUNT
} LedMode;

void        led_ctrl_init(void);          /* 进入默认模式（SLOW）；之前 tick 空转 */
void        led_ctrl_tick_1ms(void);      /* 1 ms 周期，TIM1 中断内调用 */
void        led_ctrl_set_mode(LedMode m); /* 任务上下文调用 */
void        led_ctrl_next_mode(void);
void        led_ctrl_prev_mode(void);
LedMode     led_ctrl_get_mode(void);
const char *led_ctrl_mode_name(LedMode m);
```

核心设计——**段表 + 1 ms 软 PWM**：

```c
/* 离散闪烁模式：一段 = 持续时间 + 亮/灭，表尾自然回到表头形成周期 */
typedef struct { uint16_t ms; uint8_t on; } LedStep;

static const LedStep STEPS_SLOW[]   = { {500,1}, {500,0} };
static const LedStep STEPS_FAST[]   = { {100,1}, {100,0} };
static const LedStep STEPS_DOUBLE[] = { { 80,1}, {120,0}, {80,1}, {120,0}, {600,0} };
static const LedStep STEPS_SOS[]    = {
    {150,1},{150,0}, {150,1},{150,0}, {150,1},{450,0},   /* S S S */
    {450,1},{150,0}, {450,1},{150,0}, {450,1},{450,0},   /* O O O */
    {150,1},{150,0}, {150,1},{150,0}, {150,1},{1050,0},  /* S S S + 周期尾静默 */
};
```

呼吸灯：三角波 `0→20→0`（周期 2 s），叠加 **20 ms 周期 / 20 级**软件 PWM（50 Hz，肉眼不闪）：

```c
#define LED_PWM_PERIOD_MS  20U    /* → 50 Hz */
#define LED_PWM_LEVELS     (LED_PWM_PERIOD_MS / LED_TICK_MS)   /* 20 级 */
#define LED_BREATH_MS      2000U

s_phase_ms = (s_phase_ms + 1U) % LED_BREATH_MS;
tri  = (s_phase_ms < 1000U) ? s_phase_ms : (LED_BREATH_MS - s_phase_ms);
duty = (uint8_t)(((uint32_t)tri * LED_PWM_LEVELS) / 1000U);    /* 0..20 */
s_pwm_cnt = (s_pwm_cnt + 1U) % LED_PWM_PERIOD_MS;
led_write(s_pwm_cnt < duty);
```

三个必须处理的细节：

1. **`s_led_on` 缓存初值必须是"未知"（0xFF）**。`MX_GPIO_Init()` 把 PC13 置成 `RESET`（= 点亮），
   若缓存初值为 0（= 灭），首次 `led_write(1)` 会被"值未变化"提前返回，LED 就卡在亮。
2. **`s_ready` 门闩**。TIM1 中断在 `HAL_Init()` 里就使能了，早于 `MX_GPIO_Init()`——
   此时 GPIOC 时钟还没开。`led_ctrl_init()` 置 `s_ready = 1`，之前 tick 直接返回。
3. **跨上下文竞态**。任务写 `s_mode` 与 `s_restart` 不是原子的，中断可能读到新 mode + 旧 `s_step`，
   而新模式的段数更少 → 数组越界。中断里加两行兜底：
   ```c
   if (s_step >= p->count) { s_step = 0U; s_step_ms = 0U; }
   ```

### 6. 新增 [Core/Inc/btn_app.h](Core/Inc/btn_app.h) + [Core/Src/btn_app.c](Core/Src/btn_app.c) — 按键应用层

```c
#define BTN_APP_POLL_MS   5U    /* 必须等于 multi_button.h 的 TICKS_INTERVAL */

void btn_app_init(void);     /* 自检 + button_init/attach/start（调度器启动前调用） */
void btn_app_process(void);  /* 5 ms 周期 → button_ticks() */
```

电平读取回调（MultiButton 要求返回 0/1）：

```c
static uint8_t btn_read_level(uint8_t id)
{
    (void)id;
    return (HAL_GPIO_ReadPin(BTN_GPIO_Port, BTN_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}
```

7 个事件回调：

| 事件 | 行为 | 日志 |
| --- | --- | --- |
| `BTN_PRESS_DOWN` | 记录按下时刻 | `D` `event: PRESS_DOWN` |
| `BTN_PRESS_UP` | — | `D` `event: PRESS_UP (held NNN ms)` |
| `BTN_PRESS_REPEAT` | — | `I` `event: PRESS_REPEAT xN` |
| `BTN_SINGLE_CLICK` | `led_ctrl_next_mode()` | `I` `event: SINGLE_CLICK -> mode N (NAME)` |
| `BTN_DOUBLE_CLICK` | `led_ctrl_prev_mode()` | `I` `event: DOUBLE_CLICK -> mode N (NAME)` |
| `BTN_LONG_PRESS_START` | `led_ctrl_set_mode(LED_MODE_SLOW)` | `I` `event: LONG_PRESS_START -> mode 1 (SLOW)` |
| `BTN_LONG_PRESS_HOLD` | 节流 300 ms 后 `led_ctrl_next_mode()` | `I` `event: LONG_PRESS_HOLD -> mode N (NAME)` |

> **`LONG_PRESS_HOLD` 必须自己节流**：驱动在 `BTN_STATE_LONG_HOLD` 状态下**每 5 ms 回调一次**
> （[multi_button.c:227-238](Middlewares/MultiButton/multi_button.c#L227-L238)），直接用会一秒钟滚 200 个模式。
> 用 `HAL_GetTick()` 做 300 ms 节流，正好演示"长按连滚"这个典型用法。

**线程安全**：MultiButton 的 `MULTIBUTTON_THREAD_SAFE` **不开**——按键 API 只在 BtnTask 一个任务里调用，
不需要锁（开了要额外定义 LOCK/UNLOCK 宏并传给 `multi_button.c`，徒增复杂度）。README 写明此前提。

### 7. [Core/Src/main.c](Core/Src/main.c) — 挂 1 ms LED 时基

`USER CODE BEGIN Includes` 加 `#include "led_ctrl.h"`；`USER CODE BEGIN Callback 1` 加：

```c
  if (htim->Instance == TIM1)
  {
    /* LED 软件 PWM / 呼吸灯的 1 ms 时基（本回调运行在中断上下文，禁止调用日志接口） */
    led_ctrl_tick_1ms();
  }
```

> 放在 `USER CODE BEGIN Callback 1`（生成的 `if (htim->Instance == TIM1) { HAL_IncTick(); }` 之后），
> 而不是去改生成段，保证重新生成代码后不丢。

### 8. [Core/Src/freertos.c](Core/Src/freertos.c) — `ledTask` 改造为 `BtnTask`

```c
void BtnTask(void const * argument)
{
  /* USER CODE BEGIN BtnTask */
  static uint32_t poll_cnt = 0U;

  led_ctrl_init();      /* 先点灯引擎就绪，再初始化按键（自检日志要能看到 PA15 电平） */
  btn_app_init();

  log_i("BtnTask start: mode %u (%s), key poll every %u ms",
        (unsigned)led_ctrl_get_mode(), led_ctrl_mode_name(led_ctrl_get_mode()),
        (unsigned)BTN_APP_POLL_MS);

  for(;;)
  {
    btn_app_process();            /* button_ticks()，与 TICKS_INTERVAL 对齐 */
    osDelay(BTN_APP_POLL_MS);     /* 5 ms（FreeRTOS tick = 1 ms） */

    /* 每 10 s 报一次任务栈余量，验证栈深度是否够（可删） */
    if ((++poll_cnt % 2000U) == 0U) {
      log_d("BtnTask stack high water mark = %lu words",
            (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }
  }
  /* USER CODE END BtnTask */
}
```

任务创建（生成段，与 .ioc 同步手改）：

```c
  osThreadDef(btnTask, BtnTask, osPriorityNormal, 0, 256);
  btnTaskHandle = osThreadCreate(osThread(btnTask), NULL);
```

- 栈 **256 字**（与原来的 256 一致）：调用链 `BtnTask → button_ticks → 回调 → log_i → elog_output → vsnprintf → HAL_UART_Transmit`，
  上一轮实测日志链峰值 146 字，加上 MultiButton 状态机与回调帧仍在 256 以内，用 high water mark 复核。
- 优先级 **`osPriorityIdle` → `osPriorityNormal`**：原 Idle 映射到 FreeRTOS 优先级 0，**与空闲任务同级**，
  靠时间片轮转才拿到 CPU；按键轮询需要确定性，抬到 Normal。
- 任务改名 `ledTask` → `btnTask`（现在它同时管按键和 LED，叫 ledTask 会误导）。

### 9. [STM32F407_TEST.ioc](STM32F407_TEST.ioc#L7) — 任务表与代码同步

```
- FREERTOS.Tasks01=defaultTask,0,128,StartDefaultTask,Default,NULL,Dynamic,NULL,NULL;ledTask,-3,128,LedTask,Default,NULL,Dynamic,NULL,NULL
+ FREERTOS.Tasks01=defaultTask,0,128,StartDefaultTask,Default,NULL,Dynamic,NULL,NULL;btnTask,0,256,BtnTask,Default,NULL,Dynamic,NULL,NULL
```

（同时修掉原 `ledTask` 栈 **128 与 freertos.c 的 256 不一致**的隐患——重新生成代码会把栈改回 128，
在日志链路下会直接栈溢出。）

### 10. [README.md](README.md)

- 硬件平台表：补 PA15 按键行、说明 PC13 无定时器 AF 所以呼吸灯是软件 PWM
- 目录树：补 `Middlewares/MultiButton/`、`Core/Src/btn_app.c`、`Core/Src/led_ctrl.c`
- 任务表：`ledTask` → `btnTask`（优先级 Normal、栈 256 字、职责改写）
- 新增章节 **「PA15 按键控制 LED（MultiButton）」**：模式表、事件映射表、段表/软件 PWM 实现说明、
  「为什么 LED 引擎放 TIM1 中断」、「F4 上 PA15 无需 SWJ 重映射」、接法自检与切换方法、实测验证记录
- 许可证章节：补 MultiButton 的 MIT 说明

---

## 四、资源占用预估

| 项 | 增量 | 说明 |
| --- | --- | --- |
| Flash (text) | **+约 3 KB** | `multi_button.c` ~1.2 KB + `btn_app.c`/`led_ctrl.c` ~1 KB + 段表/格式串 ~0.5 KB |
| `.bss` / `.data` | **+约 60 B** | `Button` 结构体 ~44 B + 若干状态变量 |
| FreeRTOS 堆 | **+约 80 B** | `btnTask` 栈沿用 256 字（未变），新增 `defaultTask` 无变化；改名不影响 |
| 中断负载 | +约 2 µs / 1 ms = **0.2 % CPU** | `led_ctrl_tick_1ms()` 在 TIM1 中断内 |

最终数字以 `arm-none-eabi-size` 实测为准（基线 `text 14940 / data 16 / bss 18016`）。

---

## 五、PA15 接法未知怎么办（这是本方案唯一的不确定点）

鹿小班 F407 板 PA15 按键的上下拉与接法我无法确证，所以做**可自证的配置**，而不是猜：

1. **默认按最常见的接法实现**：按键接 GND、按下为低 → `gpio.c` 给 PA15 开内部上拉、`BTN_ACTIVE_LEVEL = 0`。
   （板上若已有外部上拉，内部上拉并存无害。）
2. **上电自检日志**（`btn_app_init()` 里读一次 PA15）：
   - 读到 `1` → `I/btn PA15 idle level = 1, active level = 0 -> OK`，接法与假设一致；
   - 读到 `0` → `W/btn PA15 idle level = 0 equals BTN_ACTIVE_LEVEL (0): wiring/pull mismatch ... set BTN_ACTIVE_LEVEL to 1`
     —— 日志直接告诉你该把哪个宏改成什么。
3. **每次事件都打日志**，所以"按键到底通没通"看串口就有答案：
   按下出现 `PRESS_DOWN`、松开出现 `PRESS_UP (held NNN ms)` 即硬件链路正确。
4. 若自检报不匹配，我改 `btn_app.c` 的 `BTN_ACTIVE_LEVEL`（必要时把 `gpio.c` 的 `PULLUP` 换成 `PULLDOWN`）重烧一次即可，
   这是**一行宏**的改动。

> 我没有做"上电自动判读并自动取反"的魔法逻辑：如果上电瞬间你正好按着按键，自动判读会反相且很难排查。
> 显式宏 + 自检日志更可控，代价只是可能需要多烧一次。

---

## 六、验证步骤

**我这边（静态，无需硬件）**

1. `cmake --preset Debug`（新增了 `add_subdirectory`，需要重新 configure）→ `cmake --build build/Debug`，确认 0 error。
2. `arm-none-eabi-size build/Debug/STM32F407_TEST.elf` 与基线对比。
3. 在 `.map` 里确认 `multi_button.c`、`btn_app.c`、`led_ctrl.c` 的符号已链接，无未定义符号。
4. 用 `-fstack-usage` 生成的 `.su` 文件核对 `BtnTask`/回调/`led_ctrl_tick_1ms` 的静态帧大小。
5. `-Wall` 下若有告警，逐条列出并处置（只对 `MultiButton` 目标单独加选项，不动全局 flags）。

**硬件（你已授权我烧录）**

6. 我用 `openocd` 烧录（需要 DAPLink 空闲、没有正在运行的调试会话）。
7. 串口输出由你抓（COM3 @115200 8N1）并贴给我，核对：
   - 上电 `PA15 idle level` 自检行；
   - 按下/松开/单击/双击/长按/长按连滚各自的日志；
   - 6 种模式的实际观感（尤其呼吸灯是否顺滑、双闪与 SOS 的节奏）；
   - `BtnTask stack high water mark` 是否 < 256 字。
8. 如果 COM 口空闲，我可以试着用 PowerShell 直接读串口自行核对（不保证成功，被别的程序占用就读不到）。

---

## 七、Git 提交计划

`Middlewares/MultiButton/` 当前未被跟踪，工作区只有它和 `.claude/` 是未跟踪状态。分两次提交：

1. `引入 MultiButton 按键驱动（v1.1.1，MIT，源码原样）` —— 仅新增 `Middlewares/MultiButton/**`（含自带 `CMakeLists.txt`）
2. `PA15 按键控制 PC13 LED 六种闪烁模式：接入 MultiButton + 软件 PWM 呼吸灯` ——
   CMake / gpio.c / main.h / main.c / freertos.c / .ioc / btn_app.c+h / led_ctrl.c+h / README.md，
   提交正文写清改动点、模式与事件映射、为什么 LED 引擎放中断、接法自检、资源占用与实测结果

---

## 八、风险与注意事项

| 风险 | 处置 |
| --- | --- |
| PA15 实际接法与假设相反 → 按键完全无响应 | 上电自检日志直接报出空闲电平并给出应改的宏值；改一行重烧（第五节） |
| 在 TIM1 中断里误用日志接口 → `configASSERT` 关中断死循环 | `led_ctrl.c` 只依赖 `HAL_GPIO_WritePin()`，**不 include elog**；README 明确写"中断侧零日志" |
| 任务写 mode 与中断读 mode 的竞态 → 段表越界 | 中断内 `if (s_step >= p->count)` 兜底；单字节 `volatile` 写天然原子，无需临界区 |
| TIM1 中断早于 `MX_GPIO_Init()` 触发 → 访问未开时钟的 GPIOC | `s_ready` 门闩，`led_ctrl_init()` 之后引擎才工作 |
| 20 级 PWM（50 Hz）在呼吸灯上可能有轻微台阶感 | 受限于 1 ms 时基（FreeRTOS tick = 1 ms）与 PC13 无定时器 AF；README 写明，若要更细腻需换 PWM 引脚 |
| 重新生成 CubeMX 代码 | 手写代码全在 `USER CODE` 区；新源文件在顶层 `CMakeLists.txt`（CubeMX 不覆盖）；`.ioc` 已同步任务名/栈/优先级 |
| `BTN_APP_POLL_MS` 与 `TICKS_INTERVAL` 不一致 → 按键时长判定全错 | `btn_app_init()` 里运行期比对并 `log_e`；两者都写成 5 并有注释互指 |
| `configTICK_RATE_HZ != 1000` 时 `osDelay(5)` 不是 5 ms | `btn_app_init()` 运行期检查并 `log_w` |
| MultiButton 上游升级 | `Middlewares/MultiButton/` 为源码原样引入，README 注明升级时 `btn_app.c` 需复核事件语义 |
