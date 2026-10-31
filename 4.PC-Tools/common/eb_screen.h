#ifndef EB_COMMON_SCREEN_H
#define EB_COMMON_SCREEN_H

// ============================================================================
// eb-common: 全 PC-Tools 共享的屏幕几何常量（单一事实来源）
//
// GC9A01 圆屏 240x240 RGB888 —— 固件 SPI 显存、SDK SetImageSrc 输入、
// 模拟器虚拟显存、Canvas 画布、.ebanim 帧、ebconvert 输出全部使用此格式。
// 任何一端改分辨率，只需改这里。
// ============================================================================

#define EB_SCREEN_WIDTH  240
#define EB_SCREEN_HEIGHT 240
#define EB_SCREEN_BPP    24
#define EB_FRAME_SIZE    (EB_SCREEN_WIDTH * EB_SCREEN_HEIGHT * 3)  // 172800 B

// 兼容别名（emoji Canvas 历史命名）
#define EB_LCD_W EB_SCREEN_WIDTH
#define EB_LCD_H EB_SCREEN_HEIGHT

#endif // EB_COMMON_SCREEN_H
