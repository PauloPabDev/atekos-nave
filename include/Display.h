#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <ArduinoJson.h>
#include <FS.h>
class Display {
 public:
  bool begin(const String& deviceId);
  void status(const String& line1, const String& line2="");
  void portalInfo(const String& ssid, const String& pass, const String& ip, bool clientJoined);
  // A validated template with its dynamic layer; frame: animation frame drawn on top (-1 none); raw: image pixels.
  void render(JsonObjectConst tpl, JsonObjectConst values, bool online, int selectedAction, int frame, File& raw);
  void feedback(const String &message, uint16_t color=0xFFFF, uint32_t durationMs=1700);
  void highlight(int x, int y, int w, int h); // outline around a pressed button's zone for ~300 ms
  bool serviceFeedback(); // true when a toast or highlight just ended and the screen needs a redraw
  void setBrightness(uint8_t percent);
 private:
  Arduino_DataBus* bus_ = nullptr;
  Arduino_GFX* gfx_ = nullptr;
  uint32_t feedbackUntil_ = 0, highlightUntil_ = 0;
  bool feedbackActive_ = false, highlightActive_ = false;
  int16_t highlight_[4] = {0};
  String feedbackText_;
  uint16_t feedbackColor_ = 0xFFFF;
  void drawFeedback();
  void drawHighlight();
  void drawDeviceId(int y);
  void drawElement(JsonObjectConst e, File& raw);
  void drawImage(File& raw, int x, int y, int w, int h, bool rle, const char* ref);
  void thickLine(int x0, int y0, int x1, int y1, int s, uint16_t c);
  void polyline(const int16_t (*pt)[2], int n, bool closed, int s, uint16_t c);
  void fillPolygon(const int16_t (*pt)[2], int n, uint16_t c);
  String deviceId_;
  uint16_t color(const char* value, uint16_t fallback=0xffff);
};
