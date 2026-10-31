# eb-emoji: 表情工坊库（Canvas 绘图 + 表情动画 + .ebanim 序列化）
add_library(eb-emoji STATIC
        canvas.cpp
        expression.cpp
        ebanim.cpp)
target_include_directories(eb-emoji PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eb-emoji PUBLIC eb-common)
