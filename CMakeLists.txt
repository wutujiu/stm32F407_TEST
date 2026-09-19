cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32F407_TEST)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
# ---------------------------------------------------------------------------
# 用户手写的应用源码。
# 必须写在本文件（CubeMX 只生成一次、后续重新生成代码不会覆盖），
# 不能写进 cmake/stm32cubemx/CMakeLists.txt（该文件每次都会被重新生成）。
# ---------------------------------------------------------------------------
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/btn_app.c      # PA15 按键应用层（MultiButton 事件 → LED 模式）
    Core/Src/led_ctrl.c     # PC13 LED 多模式闪烁引擎（1 ms 中断上下文）
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# ---------------------------------------------------------------------------
# EasyLogger 日志组件（第三方，源码位于 Middlewares/easylogger）
#
# 注意：必须写在本文件（CubeMX 只生成一次、后续重新生成代码不会覆盖），
#      不能写进 cmake/stm32cubemx/CMakeLists.txt（该文件每次都会被重新生成）。
#
# plugins/file 与 plugins/flash 依赖文件系统 / Flash 驱动，本项目不使用，故不编译。
# elog_async.c / elog_buf.c 在对应模式关闭时编译为空文件，保留以便后续切换模式。
# ---------------------------------------------------------------------------
set(EASYLOGGER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/easylogger)

add_library(EasyLogger STATIC
    ${EASYLOGGER_DIR}/src/elog.c
    ${EASYLOGGER_DIR}/src/elog_utils.c
    ${EASYLOGGER_DIR}/src/elog_async.c
    ${EASYLOGGER_DIR}/src/elog_buf.c
    ${EASYLOGGER_DIR}/port/elog_port.c
)
# PUBLIC：inc 目录自动传递给依赖它的目标（Core/Src/*.c 可直接 #include <elog.h>）
target_include_directories(EasyLogger PUBLIC ${EASYLOGGER_DIR}/inc)
target_link_libraries(EasyLogger PUBLIC stm32cubemx)

# ---------------------------------------------------------------------------
# MultiButton 按键驱动（第三方，源码位于 Middlewares/MultiButton）
# 组件自带 CMakeLists.txt，顶层只负责加入构建树，细节留在组件目录里。
# ---------------------------------------------------------------------------
add_subdirectory(Middlewares/MultiButton)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
    EasyLogger
    MultiButton
)
