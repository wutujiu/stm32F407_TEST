/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015-2016, Armink, <armink.ztl@gmail.com>
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
 * Function: It is the configure head file for this library.
 * Created on: 2015-07-30
 */

#ifndef _ELOG_CFG_H_
#define _ELOG_CFG_H_
/*---------------------------------------------------------------------------*/
/*
 * 本文件已针对 STM32F407_TEST 工程（STM32F407ZGT6 + FreeRTOS + USART1）裁剪，
 * 与上游默认值的差异见各条目后的「本项目」注释。
 * 升级 EasyLogger 上游版本时，本文件与 port/elog_port.c 需要重新适配。
 */
/*---------------------------------------------------------------------------*/
/* enable log output. */
#define ELOG_OUTPUT_ENABLE
/* setting static output log level. range: from ELOG_LVL_ASSERT to ELOG_LVL_VERBOSE */
#define ELOG_OUTPUT_LVL                          ELOG_LVL_VERBOSE
/* enable assert check */
#define ELOG_ASSERT_ENABLE
/* buffer size for every line's log */
/* 本项目：上游默认 1024，单行日志实测约 80 字节，降到 256 省 768 B .bss */
#define ELOG_LINE_BUF_SIZE                       256
/* output line number max length */
#define ELOG_LINE_NUM_MAX_LEN                    5
/* output filter's tag max length */
/* 本项目：上游默认 30，改小到 8。该宏同时决定两件事：
 *   1) 输出行里 tag 字段的对齐宽度 = ELOG_FILTER_TAG_MAX_LEN / 2 + 1 = 5 字符
 *      （默认 30 时是 16 字符，短 tag 会补十几个空格，日志行被拉得很长）
 *   2) 可参与 elog_set_filter_tag/tag_lvl 过滤的 tag 最大长度
 * 注意：日志中实际输出的 tag 不受本宏限制，超长 tag 只是无法按 tag 过滤。 */
#define ELOG_FILTER_TAG_MAX_LEN                  8
/* output filter's keyword max length */
#define ELOG_FILTER_KW_MAX_LEN                   16
/* output filter's tag level max num */
#define ELOG_FILTER_TAG_LVL_MAX_NUM              5
/* output newline sign */
/* 本项目：上游默认 "\n"，串口终端需要 CRLF 换行 */
#define ELOG_NEWLINE_SIGN                        "\r\n"
/*---------------------------------------------------------------------------*/
/* enable log color */
/* 本项目：关闭彩色输出。开启后每条日志会插入 ANSI 转义序列
 * （如 ESC[36;22m ... ESC[0m），不支持 ANSI 的串口助手会把 ESC 字节丢掉，
 * 只显示出 "[36;22m" "[0m" 这类残留文本，看起来像乱码。
 * 若使用支持 ANSI 的终端（MobaXterm / VS Code 串口监视器 / minicom 等），
 * 放开本宏即可恢复彩色；注意放开本宏是唯一开关，仅调用
 * elog_set_text_color_enabled(true) 在宏关闭时不产生任何颜色。 */
/* #define ELOG_COLOR_ENABLE */
/* change the some level logs to not default color if you want */
#define ELOG_COLOR_ASSERT                        (F_MAGENTA B_NULL S_NORMAL)
#define ELOG_COLOR_ERROR                         (F_RED B_NULL S_NORMAL)
#define ELOG_COLOR_WARN                          (F_YELLOW B_NULL S_NORMAL)
#define ELOG_COLOR_INFO                          (F_CYAN B_NULL S_NORMAL)
#define ELOG_COLOR_DEBUG                         (F_GREEN B_NULL S_NORMAL)
#define ELOG_COLOR_VERBOSE                       (F_BLUE B_NULL S_NORMAL)
/*---------------------------------------------------------------------------*/
/* enable log fmt */
/* comment it if you don't want to output them at all */
#define ELOG_FMT_USING_FUNC
/* 本项目：注释掉 DIR，__FILE__ 是编译时的绝对路径（约 45 字符/行），徒增日志长度与 Flash */
/* #define ELOG_FMT_USING_DIR */
#define ELOG_FMT_USING_LINE
/*---------------------------------------------------------------------------*/
/* 本项目：采用同步阻塞输出，异步模式与缓冲模式均关闭。
 * 关闭后 elog_output() 直接调用 elog_port_output()（见 elog.c 末尾的 #if 分支链）。
 *
 * 若要改回异步模式（日志调用不阻塞，由独立任务发送），需：
 *   1. 放开下面的 ELOG_ASYNC_OUTPUT_ENABLE；
 *   2. 不要放开 ELOG_ASYNC_OUTPUT_USING_PTHREAD（裸机无 pthread，放开会链接失败）；
 *   3. 在移植层实现 elog_async_output_notice()（用 xTaskNotifyGive 唤醒日志任务）；
 *   4. 新建一个日志任务，循环调用 elog_async_get_line_log() 取日志并输出。
 */
#if 0
/* enable asynchronous output mode */
#define ELOG_ASYNC_OUTPUT_ENABLE
/* the highest output level for async mode, other level will sync output */
#define ELOG_ASYNC_OUTPUT_LVL                    ELOG_LVL_ASSERT
/* buffer size for asynchronous output mode */
#define ELOG_ASYNC_OUTPUT_BUF_SIZE               (ELOG_LINE_BUF_SIZE * 10)
/* each asynchronous output's log which must end with newline sign */
#define ELOG_ASYNC_LINE_OUTPUT
/* asynchronous output mode using POSIX pthread implementation */
#define ELOG_ASYNC_OUTPUT_USING_PTHREAD
/*---------------------------------------------------------------------------*/
/* enable buffered output mode */
#define ELOG_BUF_OUTPUT_ENABLE
/* buffer size for buffered output mode */
#define ELOG_BUF_OUTPUT_BUF_SIZE                 (ELOG_LINE_BUF_SIZE * 10)
#endif /* 本项目：同步输出，异步/缓冲模式关闭 */

#endif /* _ELOG_CFG_H_ */
