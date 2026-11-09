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
uint32_t buttonDown[board::BUTTON_COUNT]={0}; bool buttonHeld[board::BUTTON_COUNT]={false};
constexpr size_t MAX_SERIAL_RX=2048;

// Per button: a long press fires once at 700 ms held; a short one on release after 30-700 ms (debounce).
void pollButtons(){
  const uint32_t now=millis();
  for(int i=0;i<board::BUTTON_COUNT;i++){
    if(digitalRead(board::BUTTONS[i])==LOW){
      if(!buttonDown[i])buttonDown[i]=now;
      else if(!buttonHeld[i] && now-buttonDown[i]>=700){buttonHeld[i]=true;runtime.button(i,true);}
    }else if(buttonDown[i]){
      if(!buttonHeld[i] && now-buttonDown[i]>=30)runtime.button(i,false);
      buttonDown[i]=0;buttonHeld[i]=false;
    }
  }
}

void setup(){
  Serial.setRxBufferSize(MAX_SERIAL_RX); // a pasted token arrives while the loop may be busy
  Serial.begin(115200);
  console.begin(display,config,wss,runtime);
  config.onPortalTick([]{console.poll();});
  for(int pin:board::BUTTONS)pinMode(pin,INPUT_PULLUP);
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
  pollButtons();
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
