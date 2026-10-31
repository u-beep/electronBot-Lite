# studio: 上位机 GUI（WIN32 子系统，无控制台黑窗）
# 后端二选一：
#   默认            VirtualElectronBot 模拟器（无硬件运行）
#   EB_WITH_REAL_SDK=ON  链接 2.SDK ElectronLowLevel（需 USBInterface.dll + 驱动 + 真机）
#     用法: cmake -B build -DEB_WITH_REAL_SDK=ON && cmake --build build --config Release
option(EB_WITH_REAL_SDK "Link 2.SDK real hardware backend into studio" OFF)

add_executable(studio WIN32 main.cpp)
target_link_libraries(studio PRIVATE eb-simulator eb-emoji comctl32 gdi32 user32)

if(EB_WITH_REAL_SDK)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../2.SDK
                     ElectronBotSDK-Lite-build EXCLUDE_FROM_ALL)
    target_compile_definitions(studio PRIVATE EB_WITH_REAL_SDK)
    target_link_libraries(studio PRIVATE
            ElectronBotSDK-Lite
            ${CMAKE_CURRENT_SOURCE_DIR}/../../2.SDK/lib/USBInterface.lib)
    # 把 USBInterface.dll 复制到 studio.exe 旁边
    add_custom_command(TARGET studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/../../2.SDK/lib/USBInterface.dll
            $<TARGET_FILE_DIR:studio>)
endif()
