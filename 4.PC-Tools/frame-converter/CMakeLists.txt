# ebconvert: 图片/GIF -> .ebanim 帧流转换器（WIC 解码，零第三方依赖）
add_executable(ebconvert ebconvert.cpp)
target_link_libraries(ebconvert PRIVATE eb-emoji windowscodecs ole32 shell32)
