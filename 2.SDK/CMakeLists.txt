cmake_minimum_required(VERSION 3.15)
project(ElectronBot-Lite-SDK CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK static library
add_library(ElectronBotSDK-Lite STATIC electron_low_level.cpp)
target_include_directories(ElectronBotSDK-Lite PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        )

# Demo application
add_executable(sample sample.cpp)
target_link_libraries(sample PRIVATE
        ElectronBotSDK-Lite
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/USBInterface.lib
        )

# Copy USBInterface.dll next to the exe at build time
add_custom_command(TARGET sample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/USBInterface.dll
        $<TARGET_FILE_DIR:sample>
        )
