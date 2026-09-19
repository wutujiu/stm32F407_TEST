# 通过 SWD 模拟 PA15 按键：把 PA15 临时切成推挽输出并拉低 = "按下"，
# 切回输入（内部上拉生效）= "松开"。这样不需要用手按，就能端到端验证
# MultiButton 事件 → LED 模式切换的整条链路。
#
# 电气上是安全的：PA15 平时靠内部上拉（约 40 kΩ）保持高电平，
# 输出低时只有 3.3V/40kΩ ≈ 82 µA 流过，远低于任何损伤阈值；
# 按键若同时被按下，两者都是低电平，也不冲突。
#
# 用法: openocd -s <scripts> -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg \
#               -f .claude/tools/simulate_button.tcl

# GPIOA 基址 0x40020000：MODER 偏移 0x00，BSRR 偏移 0x18
# （注意别写成 0x40020800 —— 那是 GPIOC）
set GPIOA_MODER 0x40020000
set GPIOA_BSRR  0x40020018

init
# 不复位，直接接管正在运行的固件，避免干扰串口抓取的时间轴
sleep 800

# 保存原始 MODER，测试结束后原样恢复
set moder_orig [expr {"0x[lindex [mdw $GPIOA_MODER] 1]"}]
puts [format "MODER_ORIG = 0x%08x" $moder_orig]

proc pa15_output {} {
    global GPIOA_MODER moder_orig
    # MODER15 = bits 30:31，置为 01（通用推挽输出）
    mww $GPIOA_MODER [expr {($moder_orig & ~0xC0000000) | 0x40000000}]
}
proc pa15_input {} {
    global GPIOA_MODER moder_orig
    mww $GPIOA_MODER $moder_orig
}
proc pa15_low {} {
    global GPIOA_BSRR
    mww $GPIOA_BSRR 0x80000000      ;# BSRR 高 16 位 = 复位对应引脚 → PA15 输出低
}
proc pa15_press {ms} {
    pa15_output
    pa15_low
    sleep $ms
    pa15_input
}
proc pa15_release_wait {ms} { sleep $ms }

puts "--- 测试 1：单击（按下 150 ms，松开后等 700 ms 出 SINGLE_CLICK）---"
pa15_press 150
pa15_release_wait 700

puts "--- 测试 2：双击（两次 100 ms 按下，间隔 100 ms）---"
pa15_press 100
pa15_release_wait 100
pa15_press 100
pa15_release_wait 700

puts "--- 测试 3：长按（按住 1500 ms → LONG_PRESS_START + 数次 LONG_PRESS_HOLD）---"
pa15_press 1500
pa15_release_wait 500

puts "--- 测试 4：再单击两次，确认模式继续滚动 ---"
pa15_press 150
pa15_release_wait 700
pa15_press 150
pa15_release_wait 700

pa15_input
puts "MODER restored"

shutdown
