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
| 板载 LED | PC13，推挽输出，低电平点亮 |
| 串口 | USART1，PA9 = TX / PA10 = RX，115200 8N1 |
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
├── Middlewares/Third_Party/FreeRTOS/
│   └── Source/
│       ├── CMSIS_RTOS/         # CMSIS-RTOS v1 封装层（osThreadCreate 等）
│       └── portable/GCC/ARM_CM4F/
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
| `ledTask` | Idle | 128 | `LedTask` | LED 任务，当前仅 `osDelay(1)` |

> 当前两个任务的用户代码区（`USER CODE BEGIN/END` 之间）均为空，属于工程脚手架状态。业务逻辑请在对应区域内编写，以保证重新生成代码时不丢失。

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

`Core/` 与 `Drivers/` 目录下的 STM32 相关代码版权归 STMicroelectronics 所有，遵循其随附的许可证（见各文件头部说明）。FreeRTOS 遵循 MIT 许可证。其余工程代码可自行决定许可方式。
