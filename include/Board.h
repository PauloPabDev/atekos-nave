#pragma once
#include <stdint.h>
// Waveshare ESP32-C6-LCD-1.47 (ST7789, no touch). The Touch-LCD-1.47 uses other pins.
namespace board {
constexpr int WIDTH = 172, HEIGHT = 320; // the panel in portrait, whatever the template rotation
// Template `rotation` -> where the USB connector ends up, taking 0 as USB at the bottom
// (worked out from the ST7789 MADCTL; check on the board): 90 right, 180 top, 270 left.
constexpr int LCD_SCK=7, LCD_MOSI=6, LCD_CS=14, LCD_DC=15, LCD_RST=21, LCD_BL=22;
constexpr int SD_CS=4;
// Physical buttons, INPUT_PULLUP and active LOW; the logical index is the position here (0 = BOOT, a strapping
// pin: only read after boot). For more, use free GPIOs: not LCD/SD 4,6,7,14,15,21,22, strapping 4,5,8,9,15 or USB 12,13.
constexpr int BUTTONS[] = {9};
constexpr int BUTTON_COUNT = sizeof(BUTTONS)/sizeof(BUTTONS[0]);
constexpr uint8_t BACKLIGHT_PERCENT=18;
}
