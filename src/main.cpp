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
uint32_t buttonDown=0; bool buttonHeld=false;

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

// Serial console, one command per line (needs a line ending: Enter in pio device monitor).
void pollConsole(){
  static String line;
  while(Serial.available()){
    const char c=Serial.read();
    if(c!='\n' && c!='\r'){if(line.length()<64)line+=c;continue;}
    line.trim();
    if(line=="factory-reset"){
      Serial.println("[CMD] factory-reset: borrando URL, token, CA, WiFi y plantilla. Reiniciando...");
      display.status("Formateando...","Reiniciando en modo configuracion");
      config.factoryReset();
    } else if(line.length()) Serial.println("[CMD] desconocido: '"+line+"'. Comandos: factory-reset");
    line="";
  }
}

void setup(){
  Serial.begin(115200);
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
  pollConsole();
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
