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
  void tap(uint16_t x,uint16_t y);
  void loop();
 private:
  Display* display_=nullptr; WssClient* socket_=nullptr; Config* config_=nullptr;
  FirmwareUpdate firmwareUpdate_;
  JsonDocument tmpl_, data_;
  bool hasTemplate_=false,dirty_=false,dataDirty_=false;
  String deviceId_,templateId_;
  uint32_t templateVersion_=0, lastPersist_=0,lastDraw_=0,sequence_=0;
  struct Pending {String id,json;uint32_t lastSent=0;bool active=false;};
  Pending pending_[12];
  bool validTemplate(JsonVariantConst incoming);
  bool load();
  bool persistTemplate();
  void sendHello();
  void sendAck(const char* type,bool ok,const String &reason="");
  void dataUpdate(JsonVariantConst v, bool replace);
  void sendEvent(const String& eventId,const String& json);
  void ackEvent(const char* eventId);
  void handleResult(JsonVariantConst doc);
  void localAction(JsonVariantConst action);
};
