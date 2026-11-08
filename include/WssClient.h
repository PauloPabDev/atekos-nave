#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <functional>
class WssClient {
 public:
  using OnText=std::function<void(const String&)>;
  using OnState=std::function<void(bool)>;
  void configure(const String& url,const String& token,const String& customCAPem);
  void onText(OnText cb){onText_=cb;}
  void onState(OnState cb){onState_=cb;}
  void loop();
  bool send(const String &json);
  bool connected()const{return connected_;}
  void reconnect(const String& why); // drop the link now and retry at once
  void setEnabled(bool on);          // off: stay disconnected until turned on
  void setLogTraffic(bool on){logTraffic_=on;}
  void printStatus(Print& out) const;
 private:
  bool parseUrl();
  bool connectNow(String& why);
  bool sendFrame(uint8_t opcode,const uint8_t* bytes,size_t len);
  void readIncoming();
  void handleFrame(uint8_t opcode,const uint8_t* bytes,size_t len);
  void markOffline(const String& why,bool retryNow=false);
  void disconnect(const String& why,bool retryNow);
  String url_,token_,ca_,host_,path_;
  uint16_t port_=443;
  WiFiClientSecure client_;
  OnText onText_; OnState onState_;
  bool connected_=false, enabled_=true, logTraffic_=false;
  String lastError_;
  uint32_t retryAt_=0, retryDelay_=2000, lastPing_=0,lastPong_=0,connectedAt_=0;
  static constexpr size_t MAX_FRAME=8192;
  uint8_t buffer_[MAX_FRAME+32]={0};
  size_t rxLen_=0;
};
