#include "Runtime.h"
#include "Board.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
namespace {
  String serialId(){
    uint64_t mac=ESP.getEfuseMac();char s[20];
    snprintf(s,sizeof(s),"md-%04X%08X",unsigned((mac>>32)&0xffff),unsigned(mac&0xffffffff));
    return String(s);
  }
  bool inBounds(JsonObjectConst o,bool area){
    int x=o["x"]|(-1),y=o["y"]|(-1);
    if(x<0||y<0||x>=board::WIDTH||y>=board::HEIGHT)return false;
    if(!area)return true;
    int w=o["w"]|0,h=o["h"]|0;
    return w>0&&h>0&&x+w<=board::WIDTH&&y+h<=board::HEIGHT;
  }
  bool validElementType(String type){return type=="text"||type=="rect"||type=="line"||type=="button"||type=="progress";}
}
void Runtime::begin(Display& display,WssClient& socket,Config& config){
  display_=&display;socket_=&socket;config_=&config;deviceId_=serialId();
  if(!LittleFS.begin(true)){display_->status("Error LittleFS");}
  load();
  dirty_=true;
}
bool Runtime::load(){
  File f=LittleFS.open("/template.json","r");
  if(f){JsonDocument loaded;
    if(deserializeJson(loaded,f)==DeserializationError::Ok && validTemplate(loaded.as<JsonVariantConst>())){
      tmpl_.set(loaded.as<JsonVariantConst>());
      templateId_=tmpl_["id"] | "";templateVersion_=tmpl_["version"]|0;
      hasTemplate_=true;
    }f.close();}
  File d=LittleFS.open("/values.json","r");
  if(d){deserializeJson(data_,d);d.close();}
  return hasTemplate_;
}
bool Runtime::validTemplate(JsonVariantConst v){
  if(!v.is<JsonObjectConst>() || !v["id"].is<const char*>() || !v["version"].is<uint32_t>())return false;
  if(strlen(v["id"].as<const char*>())>64)return false;
  if(!v["elements"].is<JsonArrayConst>() || v["elements"].as<JsonArrayConst>().size()>45)return false;
  for(JsonObjectConst o:v["elements"].as<JsonArrayConst>()){
    String kind=o["type"]|"";
    if(!validElementType(kind) || !inBounds(o,kind=="rect"||kind=="button"||kind=="progress"))return false;
    if(kind=="line"){
      int x2=o["x2"]|(-1), y2=o["y2"]|(-1);
      if(x2<0||x2>=board::WIDTH||y2<0||y2>=board::HEIGHT)return false;
    }
  }
  return true;
}
bool Runtime::persistTemplate(){
  File f=LittleFS.open("/template.tmp","w");
  if(!f)return false;
  const size_t written=serializeJson(tmpl_,f);f.flush();f.close();
  if(written==0)return false;
  LittleFS.remove("/template.json");
  return LittleFS.rename("/template.tmp","/template.json");
}
void Runtime::sendHello(){
  JsonDocument d;d["type"]="device.hello";d["protocol"]=1;
  d["deviceId"]=deviceId_;d["firmware"]="1.0.0";
  d["display"]["width"]=board::WIDTH;d["display"]["height"]=board::HEIGHT;
  d["display"]["touch"]=false;d["display"]["color"]="RGB565";
  d["template"]["id"]=templateId_;d["template"]["version"]=templateVersion_;
  JsonArray c=d["capabilities"].to<JsonArray>();
  for(auto cap:{"text","rect","line","button","progress","template.cache"})c.add(cap);
  String out;serializeJson(d,out);socket_->send(out);
}
void Runtime::connected(bool up){if(up){config_->confirmTrial();sendHello();}dirty_=true;}
void Runtime::sendAck(const char* type,bool ok,const String& reason){
  JsonDocument d;d["type"]=type;d["ok"]=ok;
  d["templateId"]=templateId_;d["version"]=templateVersion_;
  if(reason.length())d["reason"]=reason;
  String s;serializeJson(d,s);socket_->send(s);
}
void Runtime::dataUpdate(JsonVariantConst v,bool replace){
  if(!v.is<JsonObjectConst>() || v.as<JsonObjectConst>().size()>64)return;
  if(replace)data_.clear();
  for(JsonPairConst p:v.as<JsonObjectConst>()){
    if(strlen(p.key().c_str())>40)continue;
    // Values intentionally restricted to flat scalars: no nested execution model.
    if(p.value().is<const char*>() && strlen(p.value().as<const char*>())>140)continue;
    if(!p.value().is<JsonObjectConst>() && !p.value().is<JsonArrayConst>())data_[p.key()].set(p.value());
  }
  dirty_=true;dataDirty_=true;
}
void Runtime::incoming(const String& text){
  if(text.length()>8192)return;
  JsonDocument d;
  if(deserializeJson(d,text)!=DeserializationError::Ok)return;
  String kind=d["type"]|"";
  if(kind=="template.set"){
    JsonVariantConst incoming=d["template"];
    if(!validTemplate(incoming)){sendAck("template.ack",false,"invalid_template");return;}
    tmpl_.clear();tmpl_.set(incoming);
    templateId_=tmpl_["id"]|""; templateVersion_=tmpl_["version"]|0;
    hasTemplate_=true;
    bool saved=persistTemplate();dirty_=true;
    sendAck("template.ack",saved,saved?"":"storage_failed");
  } else if(kind=="data.update"){
    if(!hasTemplate_){sendAck("data.ack",false,"no_template");return;}
    uint32_t v=d["templateVersion"]|0;
    if(v!=templateVersion_){sendAck("data.ack",false,"version_mismatch");return;}
    bool replace=String(d["strategy"] | "snapshot")=="snapshot";
    if(d["values"].is<JsonObjectConst>()){
      dataUpdate(d["values"],replace);
      sendAck("data.ack",true);
    }
  } else if(kind=="trust.stage"){
    bool ok=config_->stagePrivateCA(String(d["caDerBase64"]|""));
    JsonDocument ack;ack["type"]="trust.ack";ack["operation"]="stage";ack["ok"]=ok;
    String out;serializeJson(ack,out);socket_->send(out);
    if(ok)socket_->configure(config_->get().url,config_->get().token,config_->get().caPem);
  } else if(kind=="trust.activate"){
    bool ok=config_->activateStagedCA();
    if(!ok){JsonDocument ack;ack["type"]="trust.ack";ack["operation"]="activate";ack["ok"]=false;
      String out;serializeJson(ack,out);socket_->send(out);}
  } else if(kind=="ota.install"){
    String error;
    bool ok=firmwareUpdate_.install(String(d["url"]|""),String(d["sha256"]|""),
       String(d["signature"]|""),config_->get().caPem,*display_,error);
    JsonDocument ack;ack["type"]="ota.ack";ack["ok"]=ok;ack["reason"]=error;
    String out;serializeJson(ack,out);socket_->send(out);
  } else if(kind=="display.refresh"){
    dirty_=true;
  } else if(kind=="display.brightness"){
    display_->setBrightness(constrain((int)(d["percent"]|30),0,50));
  }
}
void Runtime::loop(){
  if(dataDirty_ && millis()-lastPersist_>30000){
    File f=LittleFS.open("/values.tmp","w");
    if(f){serializeJson(data_,f);f.close();LittleFS.remove("/values.json");
      if(LittleFS.rename("/values.tmp","/values.json")){lastPersist_=millis();dataDirty_=false;}}
  }
  if(dirty_ && millis()-lastDraw_>120){
    lastDraw_=millis();dirty_=false;
    if(hasTemplate_)display_->render(tmpl_.as<JsonVariantConst>(),data_.as<JsonVariantConst>(),socket_->connected());
    else display_->status("Esperando plantilla",socket_->connected()?"WSS conectado":"Sin conexion WSS");
  }
}
