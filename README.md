# STM32F407_TEST

基于 **STM32F407ZGT6** 的裸机 + FreeRTOS 工程模板，由 STM32CubeMX 生成外设初始化代码，使用 **CMake + Ninja + arm-none-eabi-gcc** 构建，**OpenOCD + cortex-debug** 完成烧录与调试。

可作为 STM32F4 系列项目的起手模板使用。

## 硬件平台

| 项目 | 参数 |
| --- | --- |
| MCU | STM32F407ZGT6（Cortex-M4F，LQFP144） |
| 主频 | 168 MHz（HSI 16 MHz → PLL：M=8 / N=168 / P=2 / Q=4） |
| Flash / RAM | 1 MB / 192 KB |
| 调试接口 | SWD（PA13 = SWDIO，PA14 = SWCLK） |
| 板载 LED | PC13，推挽输出，低电平点亮；闪烁模式由 PA15 按键切换（见下文） |
| 用户按键 | PA15，输入 + 内部上拉，按下为低电平（按键接地） |
| 串口 | USART1，PA9 = TX / PA10 = RX，115200 8N1（EasyLogger 日志输出口） |
| 时基 | TIM1（`HAL_InitTick` 使用 TIM 而非 SysTick，避免与 FreeRTOS 抢占） |

## 软件架构

```
STM32F407_TEST/
├── Core/
│   ├── Inc/                    # 用户头文件（main.h / FreeRTOSConfig.h / HAL 配置）
│   │   ├── btn_app.h           # PA15 按键应用层接口
│   │   └── led_ctrl.h          # LED 多模式闪烁引擎接口
│   └── Src/
│       ├── main.c              # 入口：HAL 初始化 → 时钟 → 外设 → 启动 FreeRTOS
│       ├── freertos.c          # 任务创建与任务函数
│       ├── btn_app.c           # PA15 按键事件 → LED 模式（任务上下文，手写）
│       ├── led_ctrl.c          # LED 段表/软件 PWM 引擎（1 ms 中断上下文，手写）
│       ├── gpio.c / usart.c    # 外设初始化
│       ├── stm32f4xx_it.c      # 中断服务函数
│       ├── stm32f4xx_hal_msp.c # HAL 底层初始化（GPIO/时钟使能/NVIC）
│       └── system_stm32f4xx.c  # CMSIS 系统初始化
├── Drivers/
│   ├── CMSIS/                  # ARM CMSIS 内核 + STM32F4 设备头文件
│   └── STM32F4xx_HAL_Driver/   # STM32Cube HAL 驱动
├── Middlewares/
│   ├── Third_Party/FreeRTOS/
│   │   └── Source/
│   │       ├── CMSIS_RTOS/     # CMSIS-RTOS v1 封装层（osThreadCreate 等）
│   │       └── portable/GCC/ARM_CM4F/
│   ├── MultiButton/            # MultiButton 按键驱动（v1.1.1，MIT）
│   │   ├── multi_button.c/.h   # 事件驱动按键状态机（源码原样引入）
│   │   └── CMakeLists.txt      # 组件自带构建脚本（顶层 add_subdirectory 接入）
│   └── easylogger/             # EasyLogger 日志组件（v2.2.99，MIT）
│       ├── inc/                # elog.h + elog_cfg.h（配置文件，已按本工程裁剪）
│       ├── src/                # elog.c / elog_utils.c / elog_async.c / elog_buf.c
│       ├── port/elog_port.c    # 移植层：USART1 输出 + FreeRTOS 输出锁（本工程实现）
│       └── plugins/            # file / flash 插件（依赖文件系统与 Flash 驱动，未加入构建）
├── cmake/
│   ├── gcc-arm-none-eabi.cmake # 工具链文件（GCC）
│   ├── starm-clang.cmake       # 工具链文件（Clang，备用）
│   └── stm32cubemx/CMakeLists.txt  # CubeMX 自动生成的源文件/头文件清单
├── startup_stm32f407xx.s       # 启动文件（向量表 + Reset_Handler）
├── STM32F407xx_FLASH.ld        # 链接脚本
├── STM32F407_TEST.ioc          # CubeMX 工程文件（重新生成代码的入口）
├── CMakeLists.txt              # 顶层构建脚本
└── CMakePresets.json           # Debug / Release 预设
```

### FreeRTOS 配置

| 配置项 | 值 |
| --- | --- |
| API 层 | CMSIS-RTOS v1（`osThreadCreate` / `osDelay`） |
| 调度方式 | 抢占式（`configUSE_PREEMPTION = 1`） |
| Tick 频率 | 1000 Hz（1 ms） |
| 优先级数 | 7 |
| 堆方案 | `heap_4.c`（带碎片合并） |
| 堆大小 | 15360 字节 |
| 静态分配 | 已开启（Idle 任务使用静态内存） |

已创建的任务：

| 任务名 | 优先级 | 栈（字） | 函数 | 说明 |
| --- | --- | --- | --- | --- |
| `defaultTask` | Normal | 128 | `StartDefaultTask` | 默认任务，当前仅 `osDelay(1)` |
| `btnTask` | Normal | 256 | `BtnTask` | 每 5 ms 轮询 PA15 按键；按键事件切换 PC13 闪烁模式并打印日志 |

> `btnTask`（原 `ledTask`）的栈由默认的 128 字放大到 256 字（`STM32F407_TEST.ioc` 与 `freertos.c` 已同步），
> 因为任务内调用日志接口会引入 `elog_output()` → `vsnprintf()` → `HAL_UART_Transmit()` 的调用链。
> 实测栈峰值 160 字（`high water mark = 96 words`），余量 37%。
>
> 优先级为 `osPriorityNormal`。**不要改回 `osPriorityIdle`**：CMSIS-RTOS v1 的
> `makeFreeRtosPriority()` 把 `osPriorityIdle(-3)` 映射成 FreeRTOS 优先级 0，即与空闲任务同级、
> 靠时间片轮转才拿到 CPU，按键轮询需要确定性。
>
> `defaultTask` 的用户代码区仍为空，属工程脚手架状态。业务逻辑请写在 `USER CODE BEGIN/END` 之间，重新生成代码不会丢失。

## 日志系统（EasyLogger）

USART1（PA9/PA10，115200 8N1）上的日志由 [EasyLogger](https://github.com/armink/EasyLogger) v2.2.99 输出，采用**同步阻塞**方式。

### 输出效果

以下为实机抓取的原始输出（纯 ASCII，无转义序列，每行末尾是 `\r\n`）：

```
I/elog [000:00:00.000 main] (246 elog_start)EasyLogger V2.2.99 is initialize success.
I/led  [000:00:00.001 ledTask] (158 LedTask)ledTask start: PC13 toggle every 500 ms
I/led  [000:00:00.009 ledTask] (169 LedTask)PC13 -> HIGH, toggle count = 1
I/led  [000:00:00.515 ledTask] (169 LedTask)PC13 -> LOW, toggle count = 2
I/led  [000:00:01.021 ledTask] (169 LedTask)PC13 -> HIGH, toggle count = 3
I/led  [000:00:01.527 ledTask] (169 LedTask)PC13 -> LOW, toggle count = 4
...
D/led  [000:02:01.038 ledTask] (177 LedTask)ledTask stack high water mark = 110 words
```

字段依次为：`等级 / tag / [时间 任务名] (行号 函数名)消息`。
等级：`A` 断言、`E` 错误、`W` 警告、`I` 信息、`D` 调试、`V` 详细。

> 两个容易踩的显示细节（本工程已处理）：
> - `(行号 函数名)` 与消息之间**没有空格**，这是组件 `elog_output()` 的固定拼接方式（需要在消息前加分隔请自行在格式串里加空格）。
> - tag 字段按 `ELOG_FILTER_TAG_MAX_LEN/2 + 1` 对齐补空格。上游默认 `ELOG_FILTER_TAG_MAX_LEN = 30` 会补出 16 字符宽的字段（短 tag 后面十几个空格），本工程已改为 8，字段宽度 5 字符。
> - `main` 表示调度器尚未启动（`elog_start()` 的启动横幅），任务内日志显示 FreeRTOS 任务名。

### 使用方法

在源文件中先定义 `LOG_TAG` / `LOG_LVL`，再包含 `elog.h`，之后即可使用简写接口：

```c
#define LOG_TAG    "app"            /* 日志 tag，建议每个模块一个 */
#define LOG_LVL    ELOG_LVL_DEBUG   /* 编译期过滤级别：低于此级别的 log_x() 不生成代码 */
#include <elog.h>

log_i("count = %lu", (unsigned long)cnt);   /* I/app [时间 任务名] ... */
log_e("init failed, err = %d", err);
```

也可以不定义 tag，直接用显式接口：`elog_i("app", "count = %lu", cnt)`。
运行期过滤：`elog_set_filter_lvl(ELOG_LVL_WARN)`、`elog_set_filter_tag("led")`、`elog_set_filter_tag_lvl("led", ELOG_LVL_INFO)`。

### 配置与移植层

| 位置 | 内容 |
| --- | --- |
| `Middlewares/easylogger/inc/elog_cfg.h` | 组件配置：输出级别、行缓冲 256 B、`\r\n` 换行、tag 字段宽 5 字符（`ELOG_FILTER_TAG_MAX_LEN = 8`）、格式项（已关 `ELOG_FMT_USING_DIR`）；**彩色输出关闭、异步/缓冲模式均关闭** |
| `Middlewares/easylogger/port/elog_port.c` | 移植层：`HAL_UART_Transmit()` 输出、FreeRTOS 互斥量做输出锁、`HAL_GetTick()` 时间戳、FreeRTOS 任务名 |
| `Core/Src/main.c` | `elog_init()` → `elog_set_fmt()` → `elog_start()`（在 `MX_USART1_UART_Init()` 之后） |
| `CMakeLists.txt` | `EasyLogger` 静态库的接入（写在顶层，CubeMX 重新生成不会覆盖） |

`elog_set_fmt()` 必须显式调用：组件内部的 `elog` 对象是零初始化的，不设置的话日志只剩裸消息（无等级/tag/时间）。

### 注意事项

- **只能在任务上下文调用日志接口**。在中断里调用会触发 FreeRTOS `configASSERT`（关中断死循环）。若需要中断中打日志，请改用异步模式（步骤见 `elog_cfg.h` 中的注释）。本工程的 `led_ctrl.c` 运行在 TIM1 中断里，因此它不 include `elog.h`。
- **USART1 由日志系统独占**。其它代码若要直接操作 `huart1`，需复用移植层里的同一把互斥量，否则输出会交错。
- 单条日志约 85 字节，@115200 实测耗时约 7 ms。`btnTask` 的 5 ms 轮询周期因此会被按键事件日志
  拉长一次（仅按键时发生），但按键时长判定不受影响 —— MultiButton 的计时单位是**被调用的次数**
  而非真实时间，而 `osDelay(5)` 的 5 个 tick 在按键事件之间的循环里是准确的。
- **彩色输出默认关闭**。开启后每条日志会插入 ANSI 转义序列（`ESC[36;22m ... ESC[0m`），不支持 ANSI 的串口助手会丢掉 ESC 字节、只显示 `[36;22m` `[0m` 之类的残留文本，看着像乱码。要恢复彩色需**放开 `elog_cfg.h` 里的 `ELOG_COLOR_ENABLE` 宏**（该宏是唯一开关，仅调用 `elog_set_text_color_enabled(true)` 在宏关闭时无效），适用于 MobaXterm / VS Code 串口监视器 / minicom 等支持 ANSI 的终端。
- newlib-nano 的 `printf` 在 `%s` 参数超过 64 字节时会调用 newlib 自己的 `malloc`（堆区在 `.bss` 之上，与 FreeRTOS `heap_4` 不重叠）。本工程日志均为短格式，不会触发。
- 升级 EasyLogger 上游版本时，`elog_cfg.h` 与 `port/elog_port.c` 需要重新适配。

### 资源占用（相对无日志的基线）

| 项 | 增量 | 主要构成 |
| --- | --- | --- |
| Flash | **+12.0 KB** | EasyLogger 自身约 2.9 KB、newlib-nano printf 族 2.4 KB、FreeRTOS 队列/互斥量机制 3.2 KB、HAL UART 发送链路 2.0 KB、newlib malloc 0.4 KB，其余为日志格式串等 `.rodata` |
| RAM | **+0.8 KB** | `.bss` +736 B（行缓冲 256 B、elog 对象与 tag 过滤表约 140 B、newlib stdio/reent 约 336 B）、`.data` +104 B |
| FreeRTOS 堆 | +约 1.7 KB | 输出互斥量约 80 B + `ledTask` 栈增量 512 B（`heap_4` 共 15360 B） |

> Flash 增量主要不是日志组件本身，而是"首次使用 printf 与互斥量"带来的运行时机制（此前被 `--gc-sections` 回收）。
> 若需进一步压缩 Flash：把 `ELOG_OUTPUT_LVL` 降到 `ELOG_LVL_INFO`，`log_d`/`log_v` 便不再生成代码。
> 另外约 3.2 KB 是 FreeRTOS 互斥量带来的队列机制；只有在确认日志仅由单任务调用时，才可以把移植层的锁改成空实现来省掉它（多任务下会失去串口输出的串行化保证，不建议）。

### 实测验证记录

实机（DAPLink + OpenOCD 烧录，COM3 @115200 抓取）结果：

| 项 | 实测值 | 说明 |
| --- | --- | --- |
| 日志周期 | **506 ms** | 500 ms 延时 + 约 7 ms 串口发送（85 字节/行 @115200） |
| 输出内容 | 纯 ASCII、无 ESC 字节、每行以 `\r\n` 结束（`od` 逐字节核对） | 彩色关闭后不再有转义序列残留 |
| 电平交替 | 严格 HIGH/LOW 交替，计数连续无丢行 | — |
| 启动时序 | 横幅 `000:00:00.000` → 任务启动 `000:00:00.001` → 首次翻转 `000:00:00.009` | 调度器启动后约 9 ms 首次运行 |
| `ledTask` 栈峰值 | **146 字 / 256 字**（`high water mark = 110 words`，即剩余 110 字≈440 B） | 见下方说明 |

> **栈深度是本次集成最容易踩的坑**：实测峰值 146 字，而 `ledTask` 原栈只有 128 字 ——
> 若沿用默认栈，调用日志接口会直接栈溢出（越过栈底写坏相邻的 `heap_4` 数据）。
> 放大到 256 字后仍有 42% 余量。峰值包含 FreeRTOS 保存的任务上下文，
> 以及 newlib-nano `vsnprintf()` 内部 64 字节的临时缓冲。

## PA15 按键控制 LED（MultiButton）

PA15 按键基于 [MultiButton](https://github.com/0x1abin/MultiButton) v1.1.1（MIT）的事件驱动状态机，
单击 / 双击 / 长按分别切换 PC13 板载 LED 的闪烁模式。

### 闪烁模式

| # | 模式 | 效果 | 周期 |
| --- | --- | --- | --- |
| 0 | `OFF` | 常灭 | — |
| 1 | `SLOW` | 500 ms 亮 / 500 ms 灭 | 1 s |
| 2 | `FAST` | 100 ms 亮 / 100 ms 灭 | 200 ms |
| 3 | `BREATH` | 呼吸灯（软件 PWM，20 ms 周期 / 20 级亮度） | 2 s |
| 4 | `DOUBLE` | 双闪两下（80 ms 亮 / 120 ms 灭）+ 长静默 | 1 s |
| 5 | `SOS` | 三短三长三短 + 静默 | 4.2 s |

上电默认进入模式 1（`SLOW`）。

### 按键映射

| 事件 | 动作 |
| --- | --- |
| 单击 | 下一个模式（5 → 0 循环） |
| 双击 | 上一个模式（0 → 5 循环） |
| 长按 ≥ 1 s | 回到模式 1（`SLOW`），即"恢复默认" |
| 长按保持 | 每 300 ms 自动滚到下一个模式（快速浏览全部模式） |
| 按下 / 松开 / 连击 | 只打印日志，用于确认硬件链路与事件流 |

> **`BTN_LONG_PRESS_HOLD` 必须自行节流**：驱动在 `BTN_STATE_LONG_HOLD` 状态下每 5 ms 就回调一次
> （见 `multi_button.c` 的状态机），直接用会一秒滚 200 个模式。`btn_app.c` 里用 `HAL_GetTick()`
> 做了 300 ms 节流。

### 代码结构

```
btn_app.c（任务上下文）                      led_ctrl.c（1 ms 中断上下文）
────────────────────────                    ────────────────────────────
osDelay(5) → button_ticks()                  led_ctrl_tick_1ms()
   │  MultiButton 状态机                        │  段表 / 三角波 + 软件 PWM
   ▼                                          ▼
7 个事件回调                                   led_write() → HAL_GPIO_WritePin(PC13)
   │ 只写 volatile s_mode / s_restart
   └────────────── 单字节原子写，无锁 ────────►
```

**为什么 LED 引擎放在 TIM1 中断里，而不是任务里**：按键回调要打日志，一条 85 字节日志
@115200 阻塞约 7 ms。如果引擎也在任务里，每次按键都会让 PWM 与闪烁时序停 7 ms
（呼吸灯会出现肉眼可见的顿挫）。放进 1 ms 的 HAL 时基中断后完全不受影响，
而且 1 kHz 的粒度才够做 20 级软件 PWM。

**代价**：中断侧代码不能调用任何日志接口（`configASSERT` 会关中断死循环），
所以 `led_ctrl.c` **不 include `elog.h`**，只依赖 `HAL_GPIO_WritePin()`。

**呼吸灯为什么是软件 PWM**：PC13 在 STM32F407 上**没有定时器复用功能**
（CubeMX 里它的名字是 `PC13-ANTI_TAMP`，只有 RTC/TAMPER 功能），拿不到硬件 PWM 通道，
只能用 1 ms 中断做 20 ms 周期 / 20 级的软件 PWM（50 Hz，肉眼看不到闪）。
受 1 ms 时基限制，20 级已是能做到的精度；若要更细腻的呼吸效果，需要换到有定时器 AF 的引脚。

### 新增一个闪烁模式

在 `led_ctrl.c` 里加一张段表 + 一个枚举值即可，不需要改状态机：

```c
static const LedStep STEPS_TRIPLE[] = {          /* 三段闪 + 静默 */
    { 80U, 1U }, { 120U, 0U },
    { 80U, 1U }, { 120U, 0U },
    { 80U, 1U }, { 800U, 0U }
};
```

然后在 `LedMode` 枚举尾部（`LED_MODE_COUNT` 之前）加 `LED_MODE_TRIPLE`，
并在 `PATTERNS` 与 `MODE_NAMES` 两张表的**同一位置**各补一行。文件里的
`_Static_assert` 会在张数对不上时直接编译报错。

### PA15 接法（硬件未知时怎么办）

本工程按最常见的接法实现：**按键接 GND，按下为低电平**，
所以 `gpio.c` 给 PA15 开**内部上拉**，`btn_app.c` 里 `BTN_ACTIVE_LEVEL = 0`。

上电时 `btn_app_init()` 会读一次 PA15 的空闲电平并打印自检结果：

```
I/btn [000:00:00.001 btnTask] (189 btn_app_init)PA15 idle level = 1, BTN_ACTIVE_LEVEL = 0 -> wiring OK
```

若显示 `wiring/pull mismatch`，说明接法与假设相反，日志会直接给出该把 `BTN_ACTIVE_LEVEL`
改成什么值（同时把 `gpio.c` 里 `GPIO_PA15_ButtonInit()` 的 `GPIO_PULLUP` 换成 `GPIO_PULLDOWN`）。
这是**一行宏**的改动。

> **PA15 与 JTAG**：PA15 复位后默认功能是 JTDI，但 **STM32F4 没有 F1 那套 AFIO/SWJ_CFG 重映射机制**
> （`Drivers/` 下不存在 `__HAL_RCC_AFIO_CLK_ENABLE` / `AFIO_MAPR_SWJ_CFG`），
> 只要把 MODER 配成输入就能当普通 GPIO 用；调试走 SWD（PA13/PA14），不受影响。
> 网上"PA15 要关 JTAG"的说法针对的是 STM32F1。

### 线程安全

MultiButton 的 `MULTIBUTTON_THREAD_SAFE` **未启用**：按键 API（`button_start()` /
`button_ticks()` / `button_attach()`）只在 `btnTask` 一个任务里调用，不需要加锁。
若将来有第二个任务要调用按键接口，必须启用该宏并定义 `MULTIBUTTON_LOCK()/UNLOCK()`
（步骤见 `Middlewares/MultiButton/CMakeLists.txt` 的注释）。

### 实测验证记录

实机（DAPLink + OpenOCD 烧录，COM3 @115200 抓取）结果：

| 项 | 实测值 | 说明 |
| --- | --- | --- |
| PA15 空闲电平 | `1`（自检日志 `wiring OK`） | 与"按键接地、内部上拉"的假设一致 |
| PC13 实际波形 | 通过 SWD 采样 `GPIOC_IDR`：亮段 `IDR.13 = 1`、灭段 `IDR.13 = 0` | 与 ODR 一致，游程 = 亮 500 ms / 灭 500 ms，即 SLOW 模式 1 Hz |
| 段表状态机 | `s_step` 在 0/1 间交替，`s_step_ms` 累加到 ~500 后归零 | 段计时与周期循环正确 |
| 上电时序 | 横幅 `000:00:00.000` → 接法自检 `000:00:00.001` → 任务启动 `000:00:00.023` | — |
| `btnTask` 栈峰值 | **172 字 / 256 字**（`high water mark = 84 words`） | 余量 33% |

**按键链路（用 `.claude/tools/simulate_button.tcl` 经 SWD 模拟按下，实测串口输出）**：

| 测试 | 实测事件序列 | 模式变化 |
| --- | --- | --- |
| 单击（按住 150 ms） | `PRESS_DOWN` → `PRESS_UP (held 152 ms)` → `SINGLE_CLICK` | 4 → 5 ✓ |
| 双击（100+100 ms） | `PRESS_DOWN` → `PRESS_UP (held 102 ms)` → `PRESS_DOWN` → `PRESS_REPEAT x2` → `PRESS_UP (held 109 ms)` → `DOUBLE_CLICK` | 5 → 4（上一个）✓ |
| 长按（1509 ms） | `PRESS_DOWN` → `LONG_PRESS_START` → `LONG_PRESS_HOLD` → `PRESS_UP (held 1509 ms)` | →1（复位）✓ 再 →2 ✓ |
| 单击 ×2 | `SINGLE_CLICK` ×2 | 2 → 3 → 4 ✓ |

原始输出节选：

```
D/btn  [000:02:52.047 btnTask] (88 on_press_down)event: PRESS_DOWN (pin level = 0)
D/btn  [000:02:52.199 btnTask] (96 on_press_up)event: PRESS_UP (held 152 ms)
I/btn  [000:02:52.510 btnTask] (113 on_single_click)event: SINGLE_CLICK -> mode 5 (SOS)
I/btn  [000:02:53.128 btnTask] (104 on_press_repeat)event: PRESS_REPEAT x2
I/btn  [000:02:53.540 btnTask] (124 on_double_click)event: DOUBLE_CLICK -> mode 4 (DOUBLE)
I/btn  [000:02:54.955 btnTask] (136 on_long_press_start)event: LONG_PRESS_START -> mode 1 (SLOW) reset
I/btn  [000:02:55.259 btnTask] (155 on_long_press_hold)event: LONG_PRESS_HOLD -> mode 2 (FAST)
D/btn  [000:02:55.452 btnTask] (96 on_press_up)event: PRESS_UP (held 1509 ms)
```

> **长按节流验证**：按住 1509 ms 只产生**一次** `LONG_PRESS_HOLD`，出现在
> `LONG_PRESS_START` 之后 304 ms —— 300 ms 节流生效。若不节流，这段时间会有约 250 次回调
> （驱动在 `LONG_HOLD` 状态下每 5 ms 回调一次），模式会瞬间滚到底。
>
> **计时精度**：指令按住 150 ms 实测 `held 152 ms`，指令 1500 ms 实测 `held 1509 ms`，
> 偏差来自 OpenOCD 的 `sleep` 粒度与 5 ms 轮询周期，属预期范围。
>
> 本次验证**没有用手按** PA15，而是用 SWD 把 PA15 临时切成推挽输出拉低来模拟按下
> （见 `.claude/tools/simulate_button.tcl`）。电气上安全：PA15 平时靠内部上拉（约 40 kΩ）
> 保持高电平，输出低时仅约 82 µA 流过；测试结束后脚本会把 MODER 原样恢复。
> 人手按下的行为与之一致，但**建议你自己按几次确认手感**（尤其双击的间隔习惯）。

### 资源占用（相对无按键功能的基线）

| 项 | 增量 | 主要构成 |
| --- | --- | --- |
| Flash (text) | **+约 3.9 KB** | MultiButton 状态机 1.7 KB、按键应用层（含格式串）1.7 KB、LED 引擎 0.5 KB |
| RAM (data + bss) | **+约 76 B** | `Button` 结构体 48 B + LED 引擎状态与段表 16 B + 按键应用层状态 12 B |

> 基线 = 集成 EasyLogger 之后的版本（`text 26880 / data 120 / bss 18752`），
> 本版本实测 `text 31044 / data 124 / bss 18820`。
> 实际增量大于代码本身，是因为新增了首次用到的 `printf` 格式（`%s`/`%u` 组合）与
> 新的 `.rodata` 格式串；若需压缩，可把 `btn_app.c` 的 `LOG_LVL` 降到 `ELOG_LVL_INFO`
> （会去掉 `PRESS_DOWN`/`PRESS_UP` 两条 `log_d`）。

## 开发环境

需要以下工具，且都在 `PATH` 中：

| 工具 | 版本 | 说明 |
| --- | --- | --- |
| [arm-none-eabi-gcc](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) | 14.3 rel1 | ARM 交叉编译工具链 |
| [CMake](https://cmake.org/download/) | ≥ 3.22 | 构建系统 |
| [Ninja](https://github.com/ninja-build/ninja/releases) | 任意 | 生成器（也可改用 Make） |
| [OpenOCD](https://github.com/openocd-org/openocd/releases) | 任意 | 烧录 / 调试服务 |
| [STM32CubeMX](https://www.st.com/en/development-tools/stm32cubemx.html) | 6.x | 修改外设配置时使用（可选） |

VS Code 扩展（可选，用于 IDE 内构建调试）：

- **CMake Tools**（`ms-vscode.cmake-tools`）
- **Cortex-Debug**（`marus25.cortex-debug`）
- **clangd**（`llvm-vs-code-extensions.vscode-clangd`）—— 代码跳转/补全

> 本工程默认用 clangd 做代码索引，**建议禁用 Microsoft C/C++ 扩展**（`ms-vscode.cpptools`），两者同时启用会互相冲突。`.clangd` 已配置为读取 `build/Debug/compile_commands.json`。

## 编译

### 首次配置（生成构建目录）

```bash
cmake --preset Debug     # 生成 build/Debug
cmake --preset Release   # 生成 build/Release（可选）
```

### 编译

```bash
cmake --build build/Debug      # Debug 构建
cmake --build build/Release    # Release 构建
```

产物：

```
build/Debug/STM32F407_TEST.elf    # 可执行文件（调试/烧录用）
build/Debug/STM32F407_TEST.map    # 内存映射（分析 Flash/RAM 占用）
build/Debug/compile_commands.json # 编译数据库（clangd 索引）
```

### 清理

```bash
cmake --build build/Debug --target clean
# 或直接删除 build/Debug 整个目录
```

## 烧录

### 命令行

```bash
openocd -s "C:/Program Files/OpenOCD/openocd/scripts" \
        -f interface/cmsis-dap.cfg \
        -f target/stm32f4x.cfg \
        -c "program build/Debug/STM32F407_TEST.elf verify reset exit"
```

参数说明：`program` 写入并校验，`reset` 复位，`exit` 烧录完成后退出 OpenOCD。

> **`-s` 指向的脚本目录随 OpenOCD 安装方式而变**。本机通过
> `winget install OpenOCD` 安装，脚本在 `C:/Program Files/OpenOCD/share/openocd/scripts`
> （不是网上常见的 `.../openocd/scripts`）。用 `openocd --version` 或看报错里的
> `Can't find .../scripts` 路径来确认。

### 不用手按也能验证按键（SWD 模拟）

`.claude/tools/simulate_button.tcl` 通过 SWD 把 PA15 临时切成推挽输出并拉低来模拟"按下"，
切回输入（内部上拉生效）模拟"松开"，可以自动跑完单击 / 双击 / 长按的组合：

```bash
# 一个终端抓串口，另一个终端跑按键序列
powershell -NoProfile -ExecutionPolicy Bypass -File .claude/tools/capture_com3.ps1 /tmp/cap.bin 9 &
openocd -s "C:/Program Files/OpenOCD/share/openocd/scripts" \
        -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg \
        -f .claude/tools/simulate_button.tcl
```

> **电气上安全**：PA15 平时靠内部上拉（约 40 kΩ）保持高电平，输出低时只有约 82 µA 流过；
> 若此刻你正好也按着按键，两者都是低电平，不冲突。脚本结束前会把 `GPIOA_MODER` 原样恢复。

### 观察 LED 实际波形（无需示波器）

`.claude/tools/sample_led.tcl` 是一个 OpenOCD 脚本，通过 SWD 反复读 `GPIOC_IDR`/`ODR`
与 `led_ctrl` 的内部状态变量，把 PC13 的亮/灭游程直接打印出来，用来验证闪烁时序：

```bash
openocd -s "C:/Program Files/OpenOCD/share/openocd/scripts" \
        -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg \
        -f .claude/tools/sample_led.tcl
```

```
t= 100ms IDR=0x0000ffff LED=off | ODR=0x00002000 MODER13=1 | mode=1 s_step=1 s_step_ms=99  s_led_on=0
t= 500ms IDR=0x0000dfff LED=ON  | ODR=0x00000000 MODER13=1 | mode=1 s_step=0 s_step_ms=14  s_led_on=1
```

> 脚本里的状态变量地址是**硬编码**的（由 `arm-none-eabi-nm` 从 `.elf` 里取出），
> 改代码后需要重新取值同步，否则读到的地址无意义。

### 抓串口日志（PowerShell）

`.claude/tools/capture_com3.ps1` 把 COM 口读成**原始字节**写文件，避免 PowerShell
文本管道把非 ASCII 字节替换成 `?`：

```bash
powershell -NoProfile -ExecutionPolicy Bypass \
  -File .claude/tools/capture_com3.ps1 /tmp/cap.bin 8   # 抓 8 秒
```

（`-ExecutionPolicy Bypass` 是必须的，默认策略会拒绝运行未签名脚本。）

### VS Code

`Ctrl+Shift+P` → `Tasks: Run Task`：

| 任务 | 作用 |
| --- | --- |
| `Build Debug` | 仅编译（默认构建任务，快捷键 `Ctrl+Shift+B`） |
| `Build Release` | Release 编译 |
| `Flash (OpenOCD)` | 先编译再烧录，不进调试会话 |
| `Erase Chip (OpenOCD)` | 全片擦除 |
| `Clean Debug` | 清理 Debug 构建产物 |

> 烧录前请先停止正在运行的调试会话（`Shift+F5`），否则 DAPLink 探针被占用，OpenOCD 会报 `unable to open CMSIS-DAP device`。

## 调试

VS Code 中按 `F5` 启动调试，可选两种配置：

| 配置 | 用途 |
| --- | --- |
| `Debug (DAPLink)` | 编译 → 烧录 → 停在 `main` 入口开始调试，带 FreeRTOS 任务感知 |
| `Attach (DAPLink)` | 附加到已在运行的芯片，不重新烧录 |

调试快捷键：

| 操作 | 快捷键 |
| --- | --- |
| 启动调试 | `F5` |
| 退出调试 | `Shift+F5` |
| 重启调试 | `Ctrl+Shift+F5` |
| 单步跳过 / 步入 / 步出 | `F10` / `F11` / `Shift+F11` |
| 继续运行 | `F5` |
| 打断点 | `F9` |

## 代码导航快捷键（VS Code）

| 功能 | 快捷键 |
| --- | --- |
| 转到定义 | `F12` 或 `Ctrl+Click` |
| 预览定义（不跳走） | `Alt+F12` |
| 查找所有引用 | `Shift+F12` |
| 返回上一个位置 | `Alt+←` |
| 前进到下一个位置 | `Alt+→` |
| 当前文件符号列表 | `Ctrl+Shift+O` |
| 全局搜索 | `Ctrl+Shift+F` |
| 按文件名打开 | `Ctrl+P` |

## 修改外设配置

外设（时钟树、引脚、FreeRTOS 任务）的增删改请通过 **STM32CubeMX 打开 `STM32F407_TEST.ioc`** 完成，然后重新生成代码。CubeMX 会自动更新 `cmake/stm32cubemx/CMakeLists.txt` 中的源文件清单。

手写的业务代码请务必写在 `/* USER CODE BEGIN xxx */` 与 `/* USER CODE END xxx */` 之间，重新生成时这些区域会被保留。

## 许可证

`Core/` 与 `Drivers/` 目录下的 STM32 相关代码版权归 STMicroelectronics 所有，遵循其随附的许可证（见各文件头部说明）。FreeRTOS、EasyLogger（`Middlewares/easylogger/`，Armink）与 MultiButton（`Middlewares/MultiButton/`，Zibin Zheng）均遵循 MIT 许可证。其余工程代码可自行决定许可方式。
