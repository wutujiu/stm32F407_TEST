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
| 板载 LED | PC13，推挽输出，低电平点亮，每 500 ms 翻转一次 |
| 串口 | USART1，PA9 = TX / PA10 = RX，115200 8N1（EasyLogger 日志输出口） |
| 时基 | TIM1（`HAL_InitTick` 使用 TIM 而非 SysTick，避免与 FreeRTOS 抢占） |

## 软件架构

```
STM32F407_TEST/
├── Core/
│   ├── Inc/                    # 用户头文件（main.h / FreeRTOSConfig.h / HAL 配置）
│   └── Src/
│       ├── main.c              # 入口：HAL 初始化 → 时钟 → 外设 → 启动 FreeRTOS
│       ├── freertos.c          # 任务创建与任务函数
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
| `ledTask` | Idle | 256 | `LedTask` | 每 500 ms 翻转 PC13，并打印一条日志到 USART1 |

> `ledTask` 的栈由默认的 128 字放大到 256 字（`STM32F407_TEST.ioc` 与 `freertos.c` 已同步），
> 因为任务内调用日志接口会引入 `elog_output()` → `vsnprintf()` → `HAL_UART_Transmit()` 的调用链。
> `defaultTask` 的用户代码区仍为空，属工程脚手架状态。业务逻辑请写在 `USER CODE BEGIN/END` 之间，重新生成代码不会丢失。

## 日志系统（EasyLogger）

USART1（PA9/PA10，115200 8N1）上的日志由 [EasyLogger](https://github.com/armink/EasyLogger) v2.2.99 输出，采用**同步阻塞**方式。

### 输出效果

```
I/elog            [000:00:00.032 main] (238 elog_start) EasyLogger V2.2.99 is initialize success.
I/led             [000:00:00.048 main] (152 LedTask) ledTask start: PC13 toggle every 500 ms
I/led             [000:00:00.053 ledTask] (163 LedTask) PC13 -> LOW, toggle count = 1
I/led             [000:00:00.558 ledTask] (163 LedTask) PC13 -> HIGH, toggle count = 2
...
D/led             [000:00:10.571 ledTask] (170 LedTask) ledTask stack high water mark = 216 words
```

字段依次为：`等级 / tag / [时间 任务名] (行号 函数名) 消息`。
等级：`A` 断言、`E` 错误、`W` 警告、`I` 信息、`D` 调试、`V` 详细。
带 ANSI 颜色（终端不支持时见下方"关闭颜色"）。

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
| `Middlewares/easylogger/inc/elog_cfg.h` | 组件配置：输出级别、行缓冲 256 B、`\r\n` 换行、彩色输出、格式项（已关 `ELOG_FMT_USING_DIR`）；**异步/缓冲模式均关闭** |
| `Middlewares/easylogger/port/elog_port.c` | 移植层：`HAL_UART_Transmit()` 输出、FreeRTOS 互斥量做输出锁、`HAL_GetTick()` 时间戳、FreeRTOS 任务名 |
| `Core/Src/main.c` | `elog_init()` → `elog_set_fmt()` → `elog_start()`（在 `MX_USART1_UART_Init()` 之后） |
| `CMakeLists.txt` | `EasyLogger` 静态库的接入（写在顶层，CubeMX 重新生成不会覆盖） |

`elog_set_fmt()` 必须显式调用：组件内部的 `elog` 对象是零初始化的，不设置的话日志只剩裸消息（无等级/tag/时间）。

### 注意事项

- **只能在任务上下文调用日志接口**。在中断里调用会触发 FreeRTOS `configASSERT`（关中断死循环）。若需要中断中打日志，请改用异步模式（步骤见 `elog_cfg.h` 中的注释）。
- **USART1 由日志系统独占**。其它代码若要直接操作 `huart1`，需复用移植层里的同一把互斥量，否则输出会交错。
- 单条日志 @115200 约 5 ms（70 字节左右），`ledTask` 的实际周期是 `500 ms + 发送耗时`。
- 关闭颜色：`elog_set_text_color_enabled(false)`，或注释掉 `elog_cfg.h` 里的 `ELOG_COLOR_ENABLE`。
- newlib-nano 的 `printf` 在 `%s` 参数超过 64 字节时会调用 newlib 自己的 `malloc`（堆区在 `.bss` 之上，与 FreeRTOS `heap_4` 不重叠）。本工程日志均为短格式，不会触发。
- 升级 EasyLogger 上游版本时，`elog_cfg.h` 与 `port/elog_port.c` 需要重新适配。

### 资源占用（相对无日志的基线）

| 项 | 增量 | 主要构成 |
| --- | --- | --- |
| Flash | **+12.4 KB** | EasyLogger 自身 3.2 KB、newlib-nano printf 族 2.4 KB、FreeRTOS 队列/互斥量机制 3.2 KB、HAL UART 发送链路 2.0 KB、newlib malloc 0.4 KB |
| RAM | **+1.0 KB** | `.bss` +864 B（行缓冲 256 B、elog 对象 248 B、newlib stdio/reent 约 336 B）、`.data` +128 B |
| FreeRTOS 堆 | +约 1.7 KB | 输出互斥量约 80 B + `ledTask` 栈增量 512 B（`heap_4` 共 15360 B） |

> Flash 增量主要不是日志组件本身，而是"首次使用 printf 与互斥量"带来的运行时机制（此前被 `--gc-sections` 回收）。
> 若需进一步压缩：把 `ELOG_OUTPUT_LVL` 降到 `ELOG_LVL_INFO`（`log_d`/`log_v` 不再生成代码），或按下方说明改为无锁输出。

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

`Core/` 与 `Drivers/` 目录下的 STM32 相关代码版权归 STMicroelectronics 所有，遵循其随附的许可证（见各文件头部说明）。FreeRTOS 与 EasyLogger（`Middlewares/easylogger/`，Armink）均遵循 MIT 许可证。其余工程代码可自行决定许可方式。
