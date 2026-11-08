#include "Console.h"
#include <WiFi.h>
#include <ArduinoJson.h>
namespace {
  struct Command {const char* name; const char* args; const char* help; bool inPortal;};
  // inPortal: allowed while the setup access point is up (WSS and runtime are not running).
  const Command COMMANDS[]={
    {"help","","lista los comandos",true},
    {"id","","deviceId para registrar en el servidor",true},
    {"status","","firmware, WiFi, WSS, plantilla y memoria",false},
    {"config","","URL, token enmascarado, CA y clave del portal",true},
    {"set-url","<wss://...>","cambia la URL y reconecta",true},
    {"set-token","<token>","cambia el token y reconecta",true},
    {"portal","","abre el portal de configuracion sin borrar nada",false},
    {"wifi-reset","","borra solo la WiFi y reinicia en el portal",true},
    {"factory-reset","","borra todo y reinicia en el portal",true},
    {"reconnect","","cierra el WSS y reconecta ya",false},
    {"wss-off","","desconecta el WSS hasta wss-on",false},
    {"wss-on","","vuelve a conectar el WSS",false},
    {"log","wss on|off","imprime los mensajes que entran y salen por el WSS",false},
    {"send","<json>","envia un JSON al servidor",false},
    {"press","","simula una pulsacion corta del boton",false},
    {"press-long","","simula una pulsacion larga del boton",false},
    {"actions","","lista las acciones de la plantilla",false},
    {"brightness","<0-50>","cambia el brillo",false},
    {"template","","imprime la plantilla guardada",false},
    {"values","","imprime los valores actuales",false},
    {"clear-template","","borra la plantilla en cache y la pide de nuevo",false},
    {"reboot","","reinicia la placa",true},
  };
  String masked(const String& token){
    if(token.isEmpty())return "(vacio)";
    return token.length()>8?"****"+token.substring(token.length()-4):"****";
  }
}
void Console::begin(Display& display,Config& config,WssClient& wss,Runtime& runtime){
  display_=&display;config_=&config;wss_=&wss;runtime_=&runtime;
  line_.reserve(MAX_LINE);
}
void Console::poll(){
  while(Serial.available()){
    const char c=Serial.read();
    if(c!='\n' && c!='\r'){
      if(line_.length()<MAX_LINE)line_+=c; else overflow_=true;
      continue;
    }
    // Take the line before running it: `portal` polls the console again from inside.
    String line=line_; const bool overflow=overflow_;
    line_="";overflow_=false;
    line.trim();
    if(overflow){Serial.printf("[CMD] linea demasiado larga (max. %u caracteres), ignorada\n",MAX_LINE);continue;}
    if(line.isEmpty())continue;
    const int sp=line.indexOf(' ');
    String arg=sp<0?"":line.substring(sp+1); arg.trim();
    run(sp<0?line:line.substring(0,sp),arg);
  }
}
void Console::credentialsChanged(bool portal){
  if(portal){Serial.println("[CMD] guardado. Escribe reboot para salir del portal y conectar");return;}
  const Settings& s=config_->get();
  wss_->configure(s.url,s.token,s.caPem);
  Serial.println("[CMD] guardado, reconectando WSS...");
}
void Console::run(const String& cmd,const String& arg){
  const Command* found=nullptr;
  for(const auto& c:COMMANDS)if(cmd==c.name)found=&c;
  if(!found){Serial.println("[CMD] desconocido: '"+cmd+"'. Escribe help");return;}
  const bool portal=config_->portalActive();
  if(portal && !found->inPortal){
    Serial.println("[CMD] '"+cmd+"' no es valido con el punto de acceso activo (portal WiFi). Escribe help");
    return;
  }
  if(cmd=="help"){
    Serial.println(portal?"[CMD] comandos (portal WiFi activo):":"[CMD] comandos:");
    for(const auto& c:COMMANDS)if(!portal || c.inPortal)Serial.printf("  %-14s %-11s %s\n",c.name,c.args,c.help);
  }else if(cmd=="id"){
    Serial.println("[CMD] id: "+Config::deviceId());
  }else if(cmd=="status"){
    Serial.printf("[CMD] firmware %s, id %s, encendida %lus, memoria libre %u B\n",
      FIRMWARE_VERSION,Config::deviceId().c_str(),millis()/1000,ESP.getFreeHeap());
    if(WiFi.status()==WL_CONNECTED)
      Serial.printf("[CMD] WiFi: %s, %d dBm, IP %s\n",WiFi.SSID().c_str(),WiFi.RSSI(),WiFi.localIP().toString().c_str());
    else Serial.println("[CMD] WiFi: desconectada");
    wss_->printStatus(Serial);
    runtime_->printStatus(Serial);
  }else if(cmd=="config"){
    const Settings& s=config_->get();
    Serial.println("[CMD] url: "+(s.url.length()?s.url:String("(vacia)")));
    Serial.println("[CMD] token: "+masked(s.token));
    Serial.println(String("[CMD] CA: ")+(s.caPem.length()?"privada":"publica (la del firmware)"));
    Serial.println("[CMD] clave del portal: "+s.portalPass);
  }else if(cmd=="set-url"){
    if(!config_->setUrl(arg)){Serial.println("[CMD] URL no valida: wss://..., 12-240 caracteres, sin espacios, @ ni #");return;}
    credentialsChanged(portal);
  }else if(cmd=="set-token"){
    if(!config_->setToken(arg)){Serial.println("[CMD] token no valido: vacio o de mas de 380 caracteres");return;}
    credentialsChanged(portal);
  }else if(cmd=="portal"){
    Serial.println("[CMD] abriendo el portal WiFi (MetaDisplay-Setup); al terminar se reinicia");
    config_->portal(*display_); ESP.restart();
  }else if(cmd=="wifi-reset"){
    Serial.println("[CMD] wifi-reset: borrando la WiFi guardada (URL y token se conservan). Reiniciando...");
    display_->status("WiFi borrada","Reiniciando en modo configuracion");
    config_->wifiReset();
  }else if(cmd=="factory-reset"){
    Serial.println("[CMD] factory-reset: borrando URL, token, CA, WiFi y plantilla. Reiniciando...");
    display_->status("Formateando...","Reiniciando en modo configuracion");
    config_->factoryReset();
  }else if(cmd=="reconnect"){
    wss_->reconnect("console_reconnect");
    Serial.println("[CMD] reconectando WSS...");
  }else if(cmd=="wss-off"){
    wss_->setEnabled(false);
    Serial.println("[CMD] WSS apagado; no reconecta hasta wss-on");
  }else if(cmd=="wss-on"){
    wss_->setEnabled(true);
    Serial.println("[CMD] WSS encendido, conectando...");
  }else if(cmd=="log"){
    if(arg!="wss on" && arg!="wss off"){Serial.println("[CMD] uso: log wss on|off");return;}
    wss_->setLogTraffic(arg=="wss on");
    Serial.println("[CMD] log wss "+arg.substring(4));
  }else if(cmd=="send"){
    JsonDocument d;
    if(deserializeJson(d,arg)!=DeserializationError::Ok || !d.is<JsonObject>()){Serial.println("[CMD] JSON no valido: debe ser un objeto {...}");return;}
    if(!wss_->connected()){Serial.println("[CMD] WSS desconectado, no se envia");return;}
    Serial.println(wss_->send(arg)?"[CMD] enviado":"[CMD] fallo el envio");
  }else if(cmd=="press" || cmd=="press-long"){
    if(!runtime_->button(cmd=="press-long"))Serial.println("[CMD] no hay plantilla con acciones");
    else Serial.println("[CMD] "+cmd+" simulado");
  }else if(cmd=="actions"){
    runtime_->printActions(Serial);
  }else if(cmd=="brightness"){
    bool digits=arg.length()>0 && arg.length()<=2;
    for(size_t i=0;i<arg.length();i++)digits=digits && isdigit((unsigned char)arg[i]);
    if(!digits || arg.toInt()>50){Serial.println("[CMD] uso: brightness <0-50>");return;}
    display_->setBrightness(arg.toInt());
    Serial.println("[CMD] brillo "+arg+"%");
  }else if(cmd=="template"){
    runtime_->printTemplate(Serial);
  }else if(cmd=="values"){
    runtime_->printValues(Serial);
  }else if(cmd=="clear-template"){
    runtime_->clearTemplate();
    Serial.println("[CMD] plantilla borrada; reconectando para que el servidor la reenvie");
  }else if(cmd=="reboot"){
    Serial.println("[CMD] reiniciando...");
    delay(100); ESP.restart();
  }
}
