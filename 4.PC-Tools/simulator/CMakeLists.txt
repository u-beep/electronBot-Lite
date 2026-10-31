# eb-simulator: 虚拟机器人核心库
add_library(eb-simulator STATIC
        virtual_robot.cpp
        win32_preview.cpp)
target_include_directories(eb-simulator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eb-simulator PUBLIC eb-common gdi32 user32)   # Win32 GDI 预览窗口
