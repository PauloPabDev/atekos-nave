#pragma once
#include <stdint.h>
// Waveshare ESP32-C6-LCD-1.47 (ST7789, no touch). The Touch-LCD-1.47 uses other pins.
namespace board {
constexpr int WIDTH = 172, HEIGHT = 320;
constexpr int LCD_SCK=7, LCD_MOSI=6, LCD_CS=14, LCD_DC=15, LCD_RST=21, LCD_BL=22;
constexpr int SD_CS=4;
constexpr uint8_t BACKLIGHT_PERCENT=18;
}
