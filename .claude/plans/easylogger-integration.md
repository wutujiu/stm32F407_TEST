# PC13 500ms 翻转 + EasyLogger(USART1) 集成方案

## 一、现状勘察结论

| 项 | 现状 | 结论 |
| --- | --- | --- |
| PC13 | `MX_GPIO_Init()` 已配置为推挽输出，初值 RESET（[gpio.c:53-60](Core/Src/gpio.c#L53-L60)） | 无需改 CubeMX 配置 |
| USART1 | 已初始化，PA9/PA10，115200 8N1（[usart.c:31-57](Core/Src/usart.c#L31-L57)） | 无需改 |
| `ledTask` | 已创建（优先级 Idle，栈 128 字），函数体为空 `osDelay(1)`（[freertos.c:111](Core/Src/freertos.c#L111)、[freertos.c:145-154](Core/Src/freertos.c#L145-L154)） | 直接复用，写入翻转+日志逻辑 |
| EasyLogger | `Middlewares/easylogger/` 已拷贝进工程，但 **git 未跟踪**、**未加入 CMake 构建**，`port/elog_port.c` 仍是上游空模板（所有接口为空实现） | 需完成：构建接入 + 移植层实现 + 配置裁剪 |
| 构建环境 | `arm-none-eabi-gcc 14.3.1` / `cmake 4.4.3` / `ninja 1.13.2` 均在 PATH，`build/Debug` 已有可用产物（`ninja: no work to do`） | 基线可编译，便于对比验证 |
| 基线占用 | `text 14940 / data 16 / bss 18016`（bss 含 FreeRTOS 堆 15360） | 见"资源占用预估" |
| 栈用量工具 | 工具链带 `-fstack-usage`，`.su` 文件已生成（如 `build/Debug/CMakeFiles/STM32F407_TEST.dir/Core/Src/freertos.c.su`，当前 `LedTask` 静态帧仅 16 字节） | 可直接量化验证任务栈 |
| FreeRTOS 配置 | `configASSERT` 已定义（陷阱=关中断死循环）；`INCLUDE_xTaskGetSchedulerState=1`；`pcTaskGetName()` 无宏保护，可直接用；`INCLUDE_uxTaskGetStackHighWaterMark` 未开 | 仅需补一个宏 |

**已确认的设计选择（你已选定）**：日志采用 **同步阻塞输出** —— `elog_port_output()` 内直接 `HAL_UART_Transmit()`，用 FreeRTOS 互斥量做输出锁，不启用异步/缓冲模式、不新增日志任务。

---

## 二、改动清单（8 处）

### 1. CMake 接入 — [CMakeLists.txt](CMakeLists.txt)（顶层，唯一安全位置）

`cmake/stm32cubemx/CMakeLists.txt` 是 CubeMX **每次重新生成都会覆盖**的文件，手写源文件清单放进去会丢。顶层 `CMakeLists.txt` 头部明确写着"generated only once / user is free to modify"，因此组件接入放这里：

```cmake
# ---- EasyLogger 日志组件（第三方，源码位于 Middlewares/easylogger）----
set(EASYLOGGER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/easylogger)

add_library(EasyLogger STATIC
    ${EASYLOGGER_DIR}/src/elog.c
    ${EASYLOGGER_DIR}/src/elog_utils.c
    ${EASYLOGGER_DIR}/src/elog_async.c    # 异步模式关闭时编译为空文件，保留便于后续切换
    ${EASYLOGGER_DIR}/src/elog_buf.c      # 同上
    ${EASYLOGGER_DIR}/port/elog_port.c    # 移植层（本项目实现）
)
target_include_directories(EasyLogger PUBLIC ${EASYLOGGER_DIR}/inc)
target_link_libraries(EasyLogger PUBLIC stm32cubemx)
```

并在文件末尾已有的 `target_link_libraries(${CMAKE_PROJECT_NAME} ${MX_LINK_LIBS})` 之前补一行 `target_link_libraries(${CMAKE_PROJECT_NAME} EasyLogger)`。

- `PUBLIC` 的 include 目录会自动传给 `Core/Src/*.c`，`freertos.c`/`main.c` 里 `#include <elog.h>` 即可，clangd 索引同步生效。
- `plugins/file`、`plugins/flash` **不编译**（依赖文件系统 / Flash 驱动，本项目用不到）。

### 2. 配置裁剪 — `Middlewares/easylogger/inc/elog_cfg.h`

| 配置项 | 原值 | 改为 | 原因 |
| --- | --- | --- | --- |
| `ELOG_OUTPUT_ENABLE` | 开 | 保持 | 总开关 |
| `ELOG_OUTPUT_LVL` | `VERBOSE` | 保持 | 6 级日志全开，模板工程便于裁剪 |
| `ELOG_LINE_BUF_SIZE` | `1024` | **`256`** | 单行日志缓冲（`elog.c` 静态数组）。行内容 ~80 字节，256 足够，省 768 B `.bss` |
| `ELOG_NEWLINE_SIGN` | `"\n"` | **`"\r\n"`** | 串口终端需要 CRLF，否则换行错位 |
| `ELOG_COLOR_ENABLE` | 开 | 保持 | 终端 ANSI 彩色；运行时可用 `elog_set_text_color_enabled(false)` 关 |
| `ELOG_FMT_USING_FUNC` / `_LINE` | 开 | 保持 | 日志带函数名+行号，便于定位 |
| `ELOG_FMT_USING_DIR` | 开 | **注释掉** | `__FILE__` 是绝对路径（`D:/project/...`，约 45 字符/行），白白撑长日志、占 Flash |
| `ELOG_ASYNC_OUTPUT_ENABLE` | 开 | **注释掉** | 同步模式；顺带移除 `ELOG_ASYNC_OUTPUT_USING_PTHREAD`（裸机上根本没有 pthread，当前配置若直接编译会链接失败） |
| `ELOG_BUF_OUTPUT_ENABLE` | 开 | **注释掉** | 缓冲模式与异步模式是二选一；两者都关时 `elog_output()` 直接走 `elog_port_output()` |

> 切换回异步模式的代价：放开 `ELOG_ASYNC_OUTPUT_ENABLE`、去掉 pthread 宏、实现 `elog_async_output_notice()` + 一个消费任务（`elog_async_get_line_log()`）。README 会写明步骤。

### 3. 移植层 — `Middlewares/easylogger/port/elog_port.c`（核心）

`elog.h` 要求的 8 个接口全部实现：

```c
#define LOG_TAG "elog"                 /* 移植层自身日志 tag */
#include <elog.h>
#include <stdio.h>
#include "usart.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define ELOG_PORT_TX_TIMEOUT_MS   100U  /* 256B@115200≈23ms，余量充足 */

static SemaphoreHandle_t elog_output_mutex = NULL;

/* 创建输出锁；失败时降级为"无锁"继续工作（ElogErrCode 枚举只有 ELOG_NO_ERR 一个值，
   无法上报错误，15KB 堆此时几乎不可能分配失败） */
ElogErrCode elog_port_init(void) {
    elog_output_mutex = xSemaphoreCreateMutex();
    return ELOG_NO_ERR;
}

void elog_port_deinit(void) {
    if (elog_output_mutex != NULL) { vSemaphoreDelete(elog_output_mutex); elog_output_mutex = NULL; }
}

/* 日志真正落地的唯一出口：阻塞发送 */
void elog_port_output(const char *log, size_t size) {
    if ((log == NULL) || (size == 0U)) return;
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)log, (uint16_t)size, ELOG_PORT_TX_TIMEOUT_MS);
}

/* 输出锁：调度器未启动时（elog_start() 的启动横幅在 osKernelStart() 之前打印）
   完全不碰 FreeRTOS，此时只有主线程在跑 */
void elog_port_output_lock(void) {
    if ((elog_output_mutex != NULL) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreTake(elog_output_mutex, portMAX_DELAY);
    }
}
void elog_port_output_unlock(void) {   /* 与 lock 判据严格一致 */
    if ((elog_output_mutex != NULL) && (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)) {
        (void)xSemaphoreGive(elog_output_mutex);
    }
}

/* 时间戳：HAL_GetTick() 毫秒 → "000:00:12.345"（时:分:秒.毫秒） */
const char *elog_port_get_time(void) {
    static char time_str[16];
    uint32_t ms = HAL_GetTick();
    (void)snprintf(time_str, sizeof(time_str), "%03lu:%02lu:%02lu.%03lu",
                   (unsigned long)(ms / 3600000UL), (unsigned long)((ms / 60000UL) % 60UL),
                   (unsigned long)((ms / 1000UL) % 60UL), (unsigned long)(ms % 1000UL));
    return time_str;
}

const char *elog_port_get_p_info(void) { return ""; }   /* 裸机无"进程"概念 */

/* 线程信息直接用 FreeRTOS 任务名，多任务日志一眼看出是谁打的 */
const char *elog_port_get_t_info(void) {
    return (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) ? "main" : pcTaskGetName(NULL);
}
```

要点：
- **输出锁用互斥量而非临界区**：临界区会关中断，而 `HAL_UART_Transmit()` 是阻塞轮询（含 `HAL_GetTick()` 超时判断），关中断跑阻塞发送是隐患。互斥量还带优先级继承，多任务竞争不会优先级反转。
- `HAL_UART_Transmit` 是唯一出口且始终在锁内，串口不会被打断/交错。
- **限制**：日志只能在**任务上下文**调用。中断里调会踩 `configASSERT`（关中断死循环）。README 会写明；将来要支持中断日志，走异步模式 + `FromISR` 通知。

### 4. 初始化 — [main.c](Core/Src/main.c) `USER CODE BEGIN 2`

```c
  /* EasyLogger 初始化：必须在 MX_USART1_UART_Init() 之后 */
  elog_init();
  /* 逐级打开日志格式：等级 + tag + 时间 + 任务名 + 函数 + 行号 */
  for (uint8_t lvl = ELOG_LVL_ASSERT; lvl <= ELOG_LVL_VERBOSE; lvl++) {
    elog_set_fmt(lvl, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME |
                      ELOG_FMT_T_INFO | ELOG_FMT_FUNC | ELOG_FMT_LINE);
  }
  elog_start();
```

- `elog_init()` 内部创建输出锁（调度器启动前创建互斥量合法），`elog_start()` 打印启动横幅（含版本号），此时走"调度器未启动"分支，阻塞发送到串口正常出字。
- **必须显式调 `elog_set_fmt()`**：`elog.c` 里 `static EasyLogger elog;` 零初始化，`enabled_fmt_set[]` 全 0 —— 不设置的话日志只有裸消息，没有等级/时间/tag。
- 位置在 `MX_FREERTOS_Init()` 之前，保证 `ledTask` 首次打印时日志系统已就绪。

### 5. 业务逻辑 — [freertos.c](Core/Src/freertos.c) `LedTask` + Includes

```c
/* USER CODE BEGIN Includes */
#include <elog.h>          /* 上一行需先 #define LOG_TAG "led" / LOG_LVL */
/* USER CODE END Includes */
```
（`LOG_TAG "led"`、`LOG_LVL ELOG_LVL_DEBUG` 定义在 `#include <elog.h>` 之前，之后即可用简写 `log_i()/log_d()`）

```c
void LedTask(void const * argument)
{
  /* USER CODE BEGIN LedTask */
  static uint32_t toggle_cnt = 0U;
  GPIO_PinState  level;

  log_i("ledTask start: PC13 toggle every 500 ms");
  for(;;)
  {
    HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    toggle_cnt++;
    level = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13);   /* 回读，日志反映真实电平 */
    log_i("PC13 -> %s, toggle count = %lu",
          (level == GPIO_PIN_SET) ? "HIGH" : "LOW", (unsigned long)toggle_cnt);

    /* 每 10 s 报一次任务栈余量，用于验证栈深度是否足够（可删） */
    if ((toggle_cnt % 20U) == 0U) {
      log_d("ledTask stack high water mark = %lu words",
            (unsigned long)uxTaskGetStackHighWaterMark(NULL));
    }
    osDelay(500);
  }
  /* USER CODE END LedTask */
}
```

- 翻转周期实际为 `500 ms + 串口发送时间`（~5 ms），非严格 500.000 ms；`vTaskDelayUntil` 未开启（`INCLUDE_vTaskDelayUntil 0`），如需严格周期再议。
- 日志文本用英文：避免源码 UTF-8 中文在部分串口终端/工具链下的编码问题。

### 6. 任务栈放大 — `.ioc` + `freertos.c`

当前 `ledTask` 栈 = 128 字（512 B），实测静态帧仅 16 B。加上日志链路后调用栈为
`LedTask → elog_output(局部变量+vsnprintf) → elog_strcpy/vsnprintf(newlib-nano) → HAL_UART_Transmit(48 B)`，
512 B 偏紧。**放大到 256 字（1 KB）**，两处同步改：

1. `STM32F407_TEST.ioc`：`FREERTOS.Tasks01=...;ledTask,-3,128,...` → `ledTask,-3,256,...`
   （改 .ioc 是为了**以后重新生成代码时不会被改回 128**；你也可以在 CubeMX GUI 的 Tasks and Queues 里改）
2. `Core/Src/freertos.c:111`：`osThreadDef(ledTask, LedTask, osPriorityIdle, 0, 128)` → `256`
   （生成行，先手改保证本次编译生效，重新生成后会与 .ioc 一致）

### 7. 栈余量观测宏 — `Core/Inc/FreeRTOSConfig.h` `USER CODE BEGIN Defines`

```c
#define INCLUDE_uxTaskGetStackHighWaterMark   1
```
放 CubeMX 保留区，重新生成不丢。供第 5 步的 `log_d()` 使用（`pcTaskGetName()` 无需宏）。

### 8. 文档 — [README.md](README.md)

- 目录树补 `Middlewares/easylogger/`
- 任务表：`ledTask` 说明改为"PC13 500ms 翻转 + 日志输出"，栈 128 → 256
- 新增章节 **「日志系统（EasyLogger）」**：组件来源/版本（v2.2.99，MIT）/目录结构、`elog_cfg.h` 关键配置表、移植层 4 个关键接口说明、使用方法（`LOG_TAG`/`LOG_LVL` + `log_i()`）、输出样例、同步/异步切换步骤、注意事项（仅任务上下文、ANSI 颜色、`\r\n`、互斥量与串口的独占关系）
- 许可证章节补 EasyLogger 的 MIT 说明

---

## 三、资源占用预估

| 项 | 增量 | 说明 |
| --- | --- | --- |
| Flash (text) | **+约 4~5 KB** | `elog.c` + `elog_utils.c` + newlib-nano `vsnprintf` 链路 |
| `.bss` | **+约 0.4 KB** | 行缓冲 256 B + `elog` 对象（fmt 集/tag/keyword 缓冲） |
| FreeRTOS 堆 | **+约 1.6 KB** | 互斥量 ~80 B + `ledTask` 栈增量 512 B（`heap_4` 共 15360 B，余量充足） |
| 单条日志耗时 | ~5 ms @115200 | ~70 字节/行，500 ms 周期占比约 1% |

最终数字以编译后 `arm-none-eabi-size` 实测为准（基线 `14940 / 16 / 18016`）。

---

## 四、验证步骤

**我这边（静态，无需硬件）**

1. `cmake --build build/Debug`（`CMakeLists.txt` 变更会自动触发重新 configure），确认 0 error。
2. `arm-none-eabi-size build/Debug/STM32F407_TEST.elf` 与基线对比，核对 Flash/RAM 增量。
3. 读 `build/Debug/CMakeFiles/STM32F407_TEST.dir/Core/Src/freertos.c.su` 核对 `LedTask` 静态帧，并在 `.map` 里确认 `elog_*` 符号已链接（无未定义符号）。
4. `-Wall` 下 easylogger 源码若有告警，逐条列出并给出处置（必要时对 `EasyLogger` 目标单独加编译选项，不动全局 flags）。

**硬件（需要你确认怎么配合）**

5. 烧录 + 串口抓 115200 8N1 输出，确认每 500 ms 一行、电平 HIGH/LOW 交替、计数递增、`stack high water mark` 有合理余量。
   - 方式 A：你烧录并看串口，把输出贴给我核对；
   - 方式 B：我用 `openocd` 命令烧录（需要 DAPLink 空闲、你允许我操作硬件），串口输出仍需你这边看。
   - 我**不会**在未确认前动硬件。

---

## 五、Git 提交计划

当前 `Middlewares/easylogger/` 未被跟踪，工作区干净。计划分两次提交，便于单独回退：

1. `引入 EasyLogger 日志组件（v2.2.99，MIT，源码原样）` —— 仅新增 `Middlewares/easylogger/**`
2. `集成 EasyLogger 到工程：PC13 500ms 翻转日志输出到 USART1` —— CMake / elog_cfg.h / elog_port.c / main.c / freertos.c / FreeRTOSConfig.h / .ioc / README.md，提交正文写清改动点、资源占用、验证结果与注意事项

> 你说的"完整近半说明"我理解为 **完整的开发/变更说明**（README 章节 + 详细提交信息）。如果你指的是别的（比如单独的 `docs/` 文档、或中英双语说明），告诉我，我按你的口径写。

---

## 六、风险与注意事项

| 风险 | 处置 |
| --- | --- |
| 日志从 ISR 调用 → `configASSERT` 死循环 | 文档写明"仅任务上下文"；后续需要时切异步模式 |
| `HAL_UART_Transmit` 超时（100 ms）后返回 HAL_TIMEOUT | 只丢弃该行日志，不影响任务；`ELOG_LINE_BUF_SIZE=256` 时理论最长 23 ms，超时不会触发 |
| 其它代码若直接操作 `huart1` 会与日志交错 | 文档写明：UART1 归日志独占；如要共用，需复用同一把锁 |
| 重新生成 CubeMX 代码 | 手写代码全在 `USER CODE` 区内；`.ioc` 已同步栈大小；组件接入在顶层 `CMakeLists.txt`（CubeMX 不覆盖） |
| 修改了组件源码（`elog_cfg.h` / `port/elog_port.c`） | 这是 EasyLogger 设计上的标准做法（配置文件 + 移植层就是给用户改的）；README 注明"升级上游时这两个文件需重新适配" |
| 终端不支持 ANSI 颜色时出现 `\033[32m` 乱码 | 一行 `elog_set_text_color_enabled(false)` 或注释掉 `ELOG_COLOR_ENABLE` |
