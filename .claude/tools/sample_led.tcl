# 通过 SWD 采样 GPIOC 输出数据寄存器（ODR）与 led_ctrl 内部状态，
# 观察 PC13 是否按预期闪烁。用法: openocd ... -f sample_led.tcl
#
# ODR   = 0x40020814，PC13 = bit13 (0x2000)，低电平点亮
# 状态变量地址由 arm-none-eabi-nm 从 .elf 里取出（本脚本内置，改动代码后需同步）

init
reset run
sleep 400

set ODR_ADDR  0x40020814
set S_MODE    0x2000411c
set S_STEP    0x2000411e
set S_STEP_MS 0x20004120
set S_LED_ON  0x20004126
set S_READY   0x20004127

# mdw/mdh/mdb 返回的是十六进制字符串（无 0x 前缀，可能带前导零），
# 直接交给 expr 会被当成八进制（"01e2" 不是合法八进制 → 报错），所以显式加前缀
proc hexval {addr size} {
    return [expr {"0x[lindex [$size $addr] 1]"}]
}

set samples {}
for {set i 0} {$i < 30} {incr i} {
    set odr  [hexval $ODR_ADDR mdw]
    set idr  [hexval 0x40020810 mdw]
    set moder [hexval 0x40020800 mdw]
    set led  [expr {($idr & 0x2000) ? 0 : 1}]
    set mode [hexval $S_MODE    mdb]
    set step [hexval $S_STEP    mdh]
    set sms  [hexval $S_STEP_MS mdh]
    set son  [hexval $S_LED_ON  mdb]
    puts [format "t=%4dms IDR=0x%08x LED=%s | ODR=0x%08x MODER13=%d | mode=%d s_step=%d s_step_ms=%d s_led_on=%d" \
              [expr {$i * 100}] $idr [expr {$led ? "ON " : "off"}] \
              $odr [expr {($moder >> 26) & 3}] $mode $step $sms $son]
    lappend samples $led
    sleep 100
}

puts "LED_SAMPLES: $samples"

shutdown
