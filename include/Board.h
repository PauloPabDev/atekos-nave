#pragma once
#include <stdint.h>
namespace board {
constexpr int WIDTH = 172, HEIGHT = 320;
constexpr int LCD_SCK=1, LCD_MOSI=2, LCD_CS=14, LCD_DC=15, LCD_RST=22, LCD_BL=23;
constexpr int TP_SDA=18, TP_SCL=19, TP_RST=20, TP_INT=21;
constexpr int SD_CS=4;
constexpr uint8_t TOUCH_ADDR=0x63;
constexpr uint8_t BACKLIGHT_PERCENT=36;
// This chip's native coordinates normally have X flipped relative to screen.
// Adjust after testing touch_calibration diagnostics on your physical revision.
constexpr bool TOUCH_MIRROR_X = true;
constexpr bool TOUCH_MIRROR_Y = false;
}
