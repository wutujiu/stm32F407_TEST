# ---------------------------------------------------------------------------
# MultiButton 按键驱动（第三方，MIT，https://github.com/0x1abin/MultiButton）
#
# 放在组件自己的目录里而不是顶层 CMakeLists.txt，是为了让顶层只留一行
# add_subdirectory()，组件相关的编译选项将来也只影响本目标。
#
# 链接 stm32cubemx 而非任何 STM32 外设驱动：multi_button.c 是纯逻辑，
# 通过函数指针回调读引脚，不依赖 HAL。
#
# 关于 MULTIBUTTON_THREAD_SAFE：本工程不开。按键 API（button_start /
# button_ticks / button_attach）只在 btnTask 一个任务里调用，不需要加锁。
# 将来若要多任务并发调用，需在包含 multi_button.h 之前定义
# MULTIBUTTON_THREAD_SAFE 以及 MULTIBUTTON_LOCK()/UNLOCK() 两个宏，
# 并且因为 multi_button.c 也要看到同样的宏，必须在本目标上加
# target_compile_definitions()——届时把 FreeRTOS 头文件路径一并加进来。
# ---------------------------------------------------------------------------
add_library(MultiButton STATIC
    multi_button.c
)

target_include_directories(MultiButton PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MultiButton PUBLIC stm32cubemx)
