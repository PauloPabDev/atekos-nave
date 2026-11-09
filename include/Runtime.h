#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include "Display.h"
#include "WssClient.h"
#include "FirmwareUpdate.h"
#include "Config.h"
constexpr const char* FIRMWARE_VERSION="1.3.0";
class Runtime {
 public:
  void begin(Display& display,WssClient& socket,Config& config);
  void connected(bool up);
  void incoming(const String &text);
  bool button(uint8_t index,bool longPress); // false: no template with actions
  void printStatus(Print& out) const;
  void printActions(Print& out) const;
  void printTemplate(Print& out) const;
  void printValues(Print& out) const;
  void clearTemplate();
  void loop();
 private:
  Display* display_=nullptr; WssClient* socket_=nullptr; Config* config_=nullptr;
  FirmwareUpdate firmwareUpdate_;
  // tmpl_: the template.set message without image pixels (1.2.0 saved the bare template); see tpl().
  JsonDocument tmpl_, data_;
  File raw_; // /template.raw: the template.set as received, where image pixels are read from
  bool hasTemplate_=false,dirty_=false,dataDirty_=false,buttonActions_=false;
  String deviceId_,templateId_;
  uint32_t templateVersion_=0, lastPersist_=0,lastDraw_=0,sequence_=0,animStart_=0;
  int frame_=-1;
  uint8_t selected_=0; // template action the button runs (templates without button actions)
  struct Pending {String id,json;uint32_t lastSent=0;bool active=false;};
  Pending pending_[12];
  // A fragmented message being reassembled in /rx.tmp; one at a time.
  struct Rx {String id; uint32_t next=0,count=0,bytes=0,got=0,lastAt=0; File f; bool active=false;} rx_;
  JsonObjectConst tpl() const;
  const char* templateError(JsonObjectConst t,uint32_t rawSize); // nullptr, or the template.ack reason
  bool load();
  void templateChanged();
  bool makeRoom(size_t bytes);
  void handle(JsonVariantConst d);
  void onFrag(JsonVariantConst d);
  void abortRx();
  void fragAck(const String& id,bool ok,const char* reason=nullptr);
  bool incomingFile();
  void installTemplate(uint32_t rawSize);
  void sendHello();
  void sendAck(const char* type,bool ok,const String &reason="");
  void dataUpdate(JsonVariantConst v, bool replace);
  void sendEvent(const String& eventId,const String& json);
  void ackEvent(const char* eventId);
  void handleResult(JsonVariantConst doc);
  void localAction(JsonVariantConst action);
  void runAction(JsonObjectConst action,uint8_t button,bool longPress);
};
