#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "Board.h"
#include "Config.h"
#include "WssClient.h"
#include "Runtime.h"

Display display;
Config config;
WssClient wss;
Runtime runtime;
uint32_t wifiLost=0, wifiRetry=0;

void setup(){
  Serial.begin(115200);
  if(!display.begin())Serial.println("Display init failed");
  bool configured=config.begin(display);
  // No network after the portal: reboot to retry WiFi and reopen the AP (keeps any error visible 3s).
  if(!configured){delay(3000);ESP.restart();}
  configTime(0,0,"pool.ntp.org","time.cloudflare.com");
  uint32_t started=millis();
  while(time(nullptr)<1700000000 && millis()-started<15000)delay(100);
  runtime.begin(display,wss,config);
  wss.onText([](const String& msg){runtime.incoming(msg);});
  wss.onState([](bool up){runtime.connected(up);});
  wss.configure(config.get().url,config.get().token,config.get().caPem);
}

void loop(){
  if(WiFi.status()!=WL_CONNECTED){
    if(!wifiLost)wifiLost=millis();
    if(millis()-wifiRetry>12000){wifiRetry=millis();WiFi.reconnect();}
    if(millis()-wifiLost>120000){
      // Local recovery for owner, no need to trust any remote host.
      config.portal(display); ESP.restart();
    }
  }else wifiLost=0;
  wss.loop();runtime.loop();
  if(!wss.connected())config.rollbackTrialIfNeeded(0);
  delay(5);
}
