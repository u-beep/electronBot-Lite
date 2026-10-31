# ============================================================================
# ElectronBot-Lite PC-Tools（纯软件三件套，无硬件运行）
#   eb-simulator  : 虚拟机器人模拟器（协议仿真 + 舵机动力学 + Win32 预览）
#   eb-emoji      : 表情工坊（Canvas 绘图 + 表情库 + .ebanim 动画格式）
#   studio        : 上位机 GUI（滑块控制 + 表情播放，复用上面两者）
#   sample_virtual: 命令行 demo（2.SDK/sample.cpp 的模拟器版）
# 依赖：CMake 3.15+ / MSVC 2019+，无任何第三方库
# ============================================================================

cmake_minimum_required(VERSION 3.15)
project(ElectronBot-PCTools CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)   # 源码含中文注释，统一按 UTF-8 解析
endif()

# 公共层：屏幕几何常量等（eb_screen.h），所有库/可执行经它获得根 include 路径
add_library(eb-common INTERFACE)
target_include_directories(eb-common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(simulator)
add_subdirectory(emoji-studio)
add_subdirectory(studio)
add_subdirectory(frame-converter)

# 命令行 demo（ rainbow / face 两种模式）
add_executable(sample_virtual samples/sample_virtual.cpp)
target_link_libraries(sample_virtual PRIVATE eb-simulator eb-emoji)
