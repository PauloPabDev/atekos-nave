#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "Board.h"
#include "Config.h"
#include "WssClient.h"
#include "Runtime.h"
#include "Console.h"

Display display;
Config config;
WssClient wss;
Runtime runtime;
Console console;
uint32_t wifiLost=0, wifiRetry=0;
uint32_t buttonDown=0; bool buttonHeld=false;
constexpr size_t MAX_SERIAL_RX=2048;

// Short press (>=30 ms, debounce) runs the selected action; holding 700 ms selects the next one.
void pollButton(){
  const uint32_t now=millis();
  if(digitalRead(board::BUTTON)==LOW){
    if(!buttonDown)buttonDown=now;
    else if(!buttonHeld && now-buttonDown>=700){buttonHeld=true;runtime.button(true);}
  }else if(buttonDown){
    if(!buttonHeld && now-buttonDown>=30)runtime.button(false);
    buttonDown=0;buttonHeld=false;
  }
}

void setup(){
  Serial.setRxBufferSize(MAX_SERIAL_RX); // a pasted token arrives while the loop may be busy
  Serial.begin(115200);
  console.begin(display,config,wss,runtime);
  config.onPortalTick([]{console.poll();});
  pinMode(board::BUTTON,INPUT_PULLUP);
  if(!display.begin(Config::deviceId()))Serial.println("Display init failed");
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
  console.poll();
  pollButton();
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
