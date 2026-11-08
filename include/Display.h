#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
class Display {
 public:
  bool begin();
  void status(const String& line1, const String& line2="");
  void render(JsonVariantConst layout, JsonVariantConst values, bool online);
  void feedback(const String &message, uint16_t color=0xFFFF, uint32_t durationMs=1700);
  bool serviceFeedback();
  void setBrightness(uint8_t percent);
 private:
  Arduino_DataBus* bus_ = nullptr;
  Arduino_GFX* gfx_ = nullptr;
  uint32_t feedbackUntil_ = 0;
  bool feedbackActive_ = false;
  uint16_t color(const char* value, uint16_t fallback=0xffff);
  String text(JsonObjectConst item, JsonVariantConst values);
  float number(JsonObjectConst item, JsonVariantConst values);
};
