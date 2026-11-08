#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "Display.h"
#include "WssClient.h"
#include "FirmwareUpdate.h"
#include "Config.h"
class Runtime {
 public:
  void begin(Display& display,WssClient& socket,Config& config);
  void connected(bool up);
  void incoming(const String &text);
  void loop();
 private:
  Display* display_=nullptr; WssClient* socket_=nullptr; Config* config_=nullptr;
  FirmwareUpdate firmwareUpdate_;
  JsonDocument tmpl_, data_;
  bool hasTemplate_=false,dirty_=false,dataDirty_=false;
  String deviceId_,templateId_;
  uint32_t templateVersion_=0, lastPersist_=0,lastDraw_=0;
  bool validTemplate(JsonVariantConst incoming);
  bool load();
  bool persistTemplate();
  void sendHello();
  void sendAck(const char* type,bool ok,const String &reason="");
  void dataUpdate(JsonVariantConst v, bool replace);
};
