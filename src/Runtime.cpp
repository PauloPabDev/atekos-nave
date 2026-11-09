#include "Runtime.h"
#include "Board.h"
#include "Dynamic.h"
#include "Images.h"
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
namespace {
  const char* const ELEMENT_TYPES[]={"text","rect","line","progress","button","circle","ellipse","polygon","arc","curve","image"};
  bool isInt(JsonVariantConst v,long lo,long hi){return v.is<long>() && v.as<long>()>=lo && v.as<long>()<=hi;}
  // Only what protects the device: the server already validated the whole schema, canvas included.
  const char* elementsError(JsonVariantConst list,uint32_t rawSize){
    if(!list.is<JsonArrayConst>())return "invalid_template";
    for(JsonVariantConst v:list.as<JsonArrayConst>()){
      JsonObjectConst o=v;
      if(o.isNull())return "invalid_template";
      const char* type=o["type"]|"";
      bool known=false;
      for(const char* k:ELEMENT_TYPES)known=known || !strcmp(type,k);
      if(!known)return "unsupported_element";
      const size_t points=o["points"].size();
      if((!strcmp(type,"polygon") && (points<2 || points>64)) || (!strcmp(type,"curve") && (points<3 || points>4)))
        return "invalid_template";
      if(strcmp(type,"image"))continue;
      const String enc=o["encoding"]|"";
      uint32_t off,len;
      if((enc!="rgb565" && enc!="rgb565-rle") || !isInt(o["w"],1,320) || !isInt(o["h"],1,320) ||
         !imageRef(o["data"].as<const char*>(),rawSize,off,len))return "invalid_template";
      // rgb565 must give exactly w*h*2 bytes; an RLE is bounded when drawn (never past w*h pixels).
      const uint32_t bytes=uint32_t(o["w"].as<int>())*o["h"].as<int>()*2;
      if(enc=="rgb565" && len!=(bytes+2)/3*4)return "invalid_template";
    }
    return nullptr;
  }
}
void Runtime::begin(Display& display,WssClient& socket,Config& config){
  display_=&display;socket_=&socket;config_=&config;deviceId_=Config::deviceId();
  if(!LittleFS.begin(true)){display_->status("Error LittleFS");}
  LittleFS.remove("/rx.tmp");LittleFS.remove("/tpl.new"); // left by a reset during a download or install
  load();
  dirty_=true;
}
JsonObjectConst Runtime::tpl() const{
  JsonObjectConst t=tmpl_["template"];
  return t.isNull()?tmpl_.as<JsonObjectConst>():t;
}
bool Runtime::load(){
  File f=LittleFS.open("/template.json","r");
  if(f){JsonDocument loaded;
    if(deserializeJson(loaded,f,DeserializationOption::NestingLimit(16))==DeserializationError::Ok){
      tmpl_=std::move(loaded);
      if(LittleFS.exists("/template.raw"))raw_=LittleFS.open("/template.raw","r"); // 1.2.0 templates have none
      if(!templateError(tpl(),raw_?raw_.size():0))templateChanged();
      else{tmpl_.clear();raw_.close();}
    }f.close();}
  File d=LittleFS.open("/values.json","r");
  if(d){deserializeJson(data_,d);d.close();}
  return hasTemplate_;
}
void Runtime::templateChanged(){
  JsonObjectConst t=tpl();
  templateId_=t["id"]|""; templateVersion_=t["version"]|0;
  hasTemplate_=true; selected_=0; dirty_=true; animStart_=millis(); frame_=-1;
  buttonActions_=false;
  for(JsonObjectConst a:t["actions"].as<JsonArrayConst>())buttonActions_=buttonActions_ || a["trigger"]=="button";
}
const char* Runtime::templateError(JsonObjectConst t,uint32_t rawSize){
  const char* bad="invalid_template";
  if(t.isNull() || !t["id"].is<const char*>() || strlen(t["id"].as<const char*>())>64 || !t["version"].is<uint32_t>())return bad;
  const int rotation=t["rotation"]|-1;
  if(!t["rotation"].isNull() && (rotation<0 || rotation>270 || rotation%90))return bad;
  if(const char* why=elementsError(t["elements"],rawSize))return why;
  if(!t["animation"].isNull()){
    JsonArrayConst frames=t["animation"]["frames"];
    if(frames.size()==0)return bad;
    for(JsonVariantConst f:frames){
      if(!isInt(f["ms"],20,60000))return bad;
      if(const char* why=elementsError(f["elements"],rawSize))return why;
    }
  }
  if(!t["actions"].isNull() && !t["actions"].is<JsonArrayConst>())return bad;
  for(JsonVariantConst v:t["actions"].as<JsonArrayConst>()){
    JsonObjectConst a=v;
    const char* id=a["id"]|"";
    if(strlen(id)==0||strlen(id)>48)return bad;
    String mode=a["mode"]|"remote";
    if(mode!="local" && mode!="remote" && mode!="hybrid")return bad;
    String trigger=a["trigger"]|"tap", press=a["press"]|"short";
    if(trigger=="tap"){if(!a["x"].is<int>() || !a["y"].is<int>() || !a["w"].is<int>() || !a["h"].is<int>())return bad;}
    else if(trigger!="button" || !isInt(a["button"],0,15) || (press!="short" && press!="long"))return bad;
  }
  return nullptr;
}
// Room in LittleFS for `bytes` plus ~48 KB (structure and values.json). When it only fits without the current
// template, that one goes first: if the new one then fails, the screen has none and the next hello asks again.
bool Runtime::makeRoom(size_t bytes){
  const size_t need=bytes+48*1024, available=LittleFS.totalBytes()-LittleFS.usedBytes();
  if(available>=need)return true;
  size_t current=raw_?raw_.size():0;
  if(LittleFS.exists("/template.json")){File j=LittleFS.open("/template.json","r");current+=j.size();j.close();}
  if(available+current<need)return false; // not even without it: keep it
  raw_.close();LittleFS.remove("/template.json");LittleFS.remove("/template.raw");
  tmpl_.clear();hasTemplate_=false;templateId_="";templateVersion_=0;dirty_=true;
  return LittleFS.totalBytes()-LittleFS.usedBytes()>=need;
}
void Runtime::sendHello(){
  JsonDocument d;d["type"]="device.hello";d["protocol"]=1;
  d["deviceId"]=deviceId_;d["firmware"]=FIRMWARE_VERSION;
  d["display"]["width"]=board::WIDTH;d["display"]["height"]=board::HEIGHT;
  d["display"]["touch"]=false;d["display"]["color"]="RGB565";d["display"]["buttons"]=board::BUTTON_COUNT;
  d["template"]["id"]=templateId_;d["template"]["version"]=templateVersion_;
  JsonArray c=d["capabilities"].to<JsonArray>();
  for(auto cap:{"text","rect","line","button","progress","touch.tap","ui.result","template.cache",
      "rotation","stroke","circle","ellipse","polygon","arc","curve","image","animation","binds","rules","buttons","frag"})c.add(cap);
  String out;serializeJson(d,out);socket_->send(out);
  Serial.printf("[WSS %lus] hello sent, deviceId=%s\n",millis()/1000,deviceId_.c_str());
}
void Runtime::connected(bool up){if(up){config_->confirmTrial();sendHello();}else abortRx();dirty_=true;}
void Runtime::sendAck(const char* type,bool ok,const String& reason){
  JsonDocument d;d["type"]=type;d["ok"]=ok;
  d["templateId"]=templateId_;d["version"]=templateVersion_;
  if(reason.length())d["reason"]=reason;
  String s;serializeJson(d,s);socket_->send(s);
}
void Runtime::fragAck(const String& id,bool ok,const char* reason){
  JsonDocument d;d["type"]="frag.ack";d["id"]=id;d["ok"]=ok;
  if(reason)d["reason"]=reason;
  String s;serializeJson(d,s);socket_->send(s);
}
void Runtime::abortRx(){
  if(rx_.active){rx_.f.close();LittleFS.remove("/rx.tmp");dirty_=true;}
  rx_.active=false;
}
// Chunks of a message over 8192 bytes, sent in a row and in order: base64 appended to /rx.tmp.
void Runtime::onFrag(JsonVariantConst d){
  const String id=d["id"]|"";
  const uint32_t seq=d["seq"]|0, count=d["count"]|0, bytes=d["bytes"]|0;
  if(seq==0){
    if(rx_.active)fragAck(rx_.id,false,"incomplete");
    abortRx();
    if(count<2 || bytes==0 || !makeRoom(bytes)){fragAck(id,false,"too_large");return;}
    rx_.f=LittleFS.open("/rx.tmp","w");
    if(!rx_.f){fragAck(id,false,"storage_failed");return;}
    rx_.id=id;rx_.count=count;rx_.bytes=bytes;rx_.next=rx_.got=0;rx_.lastAt=millis();rx_.active=true;dirty_=true;
  }
  if(!rx_.active || id!=rx_.id)return; // chunks of another or a dropped message
  if(seq!=rx_.next){fragAck(id,false,"out_of_order");abortRx();return;}
  static uint8_t buf[6144];
  const char* b64=d["data"]|"";
  size_t n=0;
  if(mbedtls_base64_decode(buf,sizeof buf,&n,(const uint8_t*)b64,strlen(b64)) || rx_.got+n>rx_.bytes){
    fragAck(id,false,"bad_data");abortRx();return;
  }
  if(rx_.f.write(buf,n)!=n){fragAck(id,false,"storage_failed");abortRx();return;}
  rx_.got+=n;rx_.lastAt=millis();
  if(++rx_.next<rx_.count)return;
  rx_.f.close();rx_.active=false;
  if(rx_.got!=rx_.bytes){LittleFS.remove("/rx.tmp");fragAck(id,false,"incomplete");return;}
  // The template.ack or data.ack of the message itself is what the server acts on.
  const bool ok=incomingFile();
  fragAck(id,ok,ok?nullptr:"bad_data");
}
// The message in /rx.tmp (reassembled, or a whole template.set), without its image pixels in RAM: those stay
// in the file and /tpl.new gets the rest. False if it is not valid JSON.
bool Runtime::incomingFile(){
  File src=LittleFS.open("/rx.tmp","r"), dst=LittleFS.open("/tpl.new","w");
  const bool ok=src && dst && stripImages(src,dst);
  const uint32_t rawSize=src?src.size():0;
  src.close();dst.close();
  dirty_=true;
  JsonDocument filter,head;filter["type"]=true;
  if(ok){File f=LittleFS.open("/tpl.new","r");
    deserializeJson(head,f,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(16));f.close();}
  if(ok && head["type"]=="template.set"){installTemplate(rawSize);return true;}
  JsonDocument d;
  bool parsed=false;
  if(ok){File f=LittleFS.open("/tpl.new","r");parsed=deserializeJson(d,f)==DeserializationError::Ok;f.close();}
  LittleFS.remove("/tpl.new");LittleFS.remove("/rx.tmp");
  if(parsed)handle(d.as<JsonVariantConst>());
  return parsed;
}
// /tpl.new + /rx.tmp become /template.json + /template.raw; on any failure the current template stays.
void Runtime::installTemplate(uint32_t rawSize){
  JsonDocument next;
  File f=LittleFS.open("/tpl.new","r");
  const DeserializationError e=deserializeJson(next,f,DeserializationOption::NestingLimit(16));f.close();
  // No element limit: the RAM is the limit, and running out of it means too_large.
  const char* why=e==DeserializationError::NoMemory?"too_large":e?"invalid_template":
    templateError(next["template"].as<JsonObjectConst>(),rawSize);
  if(why){LittleFS.remove("/tpl.new");LittleFS.remove("/rx.tmp");sendAck("template.ack",false,why);return;}
  raw_.close();
  LittleFS.remove("/template.json");LittleFS.remove("/template.raw");
  const bool saved=LittleFS.rename("/tpl.new","/template.json") && LittleFS.rename("/rx.tmp","/template.raw");
  tmpl_=std::move(next);
  raw_=LittleFS.open("/template.raw","r");
  templateChanged();
  sendAck("template.ack",saved,saved?"":"storage_failed");
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
void Runtime::ackEvent(const char* id){if(!id)return;for(auto &p:pending_)if(p.active && p.id==id){p.active=false;return;}}
void Runtime::handleResult(JsonVariantConst d){
  ackEvent(d["eventId"]|"");
  const char* toast=d["toast"]|"";
  if(*toast)display_->feedback(String(toast).substring(0,24));
  if(d["patch"].is<JsonObjectConst>())dataUpdate(d["patch"],false);
  if(d["replace"].is<JsonObjectConst>())dataUpdate(d["replace"],true);
}
void Runtime::incoming(const String& text){
  if(text.length()>8192)return;
  JsonDocument d;
  if(deserializeJson(d,text)!=DeserializationError::Ok)return;
  if(d["type"]=="frag"){onFrag(d.as<JsonVariantConst>());return;}
  if(d["type"]=="template.set"){
    // Through /rx.tmp like a fragmented one, so there is a single way to install templates.
    d.clear();abortRx();
    if(!makeRoom(text.length())){sendAck("template.ack",false,"too_large");return;}
    File f=LittleFS.open("/rx.tmp","w");
    const bool written=f && f.print(text)==text.length();
    f.close();
    if(!written){LittleFS.remove("/rx.tmp");sendAck("template.ack",false,"storage_failed");return;}
    incomingFile();
    return;
  }
  handle(d.as<JsonVariantConst>());
}
void Runtime::handle(JsonVariantConst d){
  String kind=d["type"]|"";
  if(kind=="data.update"){
    if(!hasTemplate_){sendAck("data.ack",false,"no_template");return;}
    uint32_t v=d["templateVersion"]|0;
    if(v!=templateVersion_){sendAck("data.ack",false,"version_mismatch");return;}
    bool replace=String(d["strategy"] | "snapshot")=="snapshot";
    if(d["values"].is<JsonObjectConst>()){
      dataUpdate(d["values"],replace);
      sendAck("data.ack",true);
    }
  } else if(kind=="ui.result"){
    handleResult(d);
  } else if(kind=="event.ack"){
    ackEvent(d["eventId"]|"");
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
void Runtime::localAction(JsonVariantConst a){
  const String kind=a["kind"]|"";
  if(kind=="feedback")display_->feedback(String(a["message"]|"Procesando...").substring(0,24));
  else if(kind=="set_value"){
    const char* key=a["key"]|"";
    if(strlen(key)>0&&strlen(key)<=40 && !a["value"].is<JsonObjectConst>()&&!a["value"].is<JsonArrayConst>()){
      data_[key].set(a["value"]);dirty_=true;dataDirty_=true;
    }
  }
}
void Runtime::sendEvent(const String& eventId,const String& json){
  for(auto &p:pending_){
    if(!p.active){p.id=eventId;p.json=json;p.lastSent=0;p.active=true;return;}
  }
  display_->feedback("Cola de eventos llena",0xF800);
}
void Runtime::runAction(JsonObjectConst a,uint8_t button,bool longPress){
  String mode=a["mode"]|"remote";
  if(mode=="local"||mode=="hybrid")localAction(a["local"]);
  if(mode!="remote" && mode!="hybrid")return;
  if(mode=="remote" && !socket_->connected())display_->feedback("Sin conexion",0xF800);
  // Same touch.tap a touch screen sends, at the center of the action's zone (0,0 without one). The server
  // reads the payload from its copy of the template by actionId.
  JsonDocument event;event["type"]="touch.tap";
  String eventId=deviceId_+"-"+String((unsigned long)millis())+"-"+String(++sequence_);
  event["eventId"]=eventId;event["deviceId"]=deviceId_;
  event["actionId"]=a["id"]|"";
  event["name"]=a["event"]|"tap";
  event["x"]=(a["x"]|0)+(a["w"]|0)/2;event["y"]=(a["y"]|0)+(a["h"]|0)/2;
  event["templateId"]=templateId_;event["templateVersion"]=templateVersion_;
  event["uptimeMs"]=millis();
  event["source"]="button";event["button"]=button;event["press"]=longPress?"long":"short";
  String out;serializeJson(event,out);sendEvent(eventId,out);
}
bool Runtime::button(uint8_t index,bool longPress){
  if(!hasTemplate_)return false;
  JsonArrayConst actions=tpl()["actions"];
  if(buttonActions_){
    // The template maps the buttons itself: the action for this button and press, if there is one.
    for(JsonObjectConst a:actions){
      if(a["trigger"]!="button" || (a["button"]|-1)!=index || String(a["press"]|"short")!=(longPress?"long":"short"))continue;
      if(a["w"].is<int>())display_->highlight(a["x"]|0,a["y"]|0,a["w"]|0,a["h"]|0);
      runAction(a,index,longPress);
      return true;
    }
    display_->feedback("Sin accion");
    return true;
  }
  // As in 1.2.0: only button 0; short runs the selected action, long selects the next one.
  if(index!=0)return true;
  if(actions.size()==0){display_->feedback("Sin acciones");return false;}
  if(selected_>=actions.size())selected_=0;
  if(longPress){
    selected_=(selected_+1)%actions.size();dirty_=true;
    display_->feedback(String("Accion: ")+(actions[selected_]["id"]|""));
    return true;
  }
  runAction(actions[selected_],0,false);
  return true;
}
void Runtime::printStatus(Print& out) const{
  int pending=0;for(const auto& p:pending_)if(p.active)pending++;
  if(hasTemplate_)out.printf("[CMD] plantilla: %s v%u, eventos sin confirmar: %d\n",templateId_.c_str(),templateVersion_,pending);
  else out.printf("[CMD] plantilla: ninguna, eventos sin confirmar: %d\n",pending);
  if(rx_.active)out.printf("[CMD] recibiendo mensaje troceado: %u/%u trozos, %u/%u B\n",
    unsigned(rx_.next),unsigned(rx_.count),unsigned(rx_.got),unsigned(rx_.bytes));
}
void Runtime::printActions(Print& out) const{
  JsonArrayConst actions=tpl()["actions"];
  if(!hasTemplate_ || actions.size()==0){out.println("[CMD] la plantilla no tiene acciones");return;}
  const size_t current=selected_<actions.size()?selected_:0;
  for(size_t i=0;i<actions.size();i++){
    JsonObjectConst a=actions[i];
    out.printf("[CMD] %s %u. %s  modo=%s evento=%s",!buttonActions_ && i==current?"*":" ",unsigned(i+1),
      a["id"]|"",a["mode"]|"remote",a["event"]|"tap");
    if(a["trigger"]=="button")out.printf(" boton=%d %s",a["button"]|0,String(a["press"]|"short")=="long"?"larga":"corta");
    out.println();
  }
}
void Runtime::printTemplate(Print& out) const{
  if(!hasTemplate_){out.println("[CMD] no hay plantilla");return;}
  serializeJson(tpl(),out);out.println();
}
void Runtime::printValues(Print& out) const{serializeJson(data_,out);out.println();}
void Runtime::clearTemplate(){
  tmpl_.clear();hasTemplate_=false;templateId_="";templateVersion_=0;selected_=0;dirty_=true;
  raw_.close();LittleFS.remove("/template.json");LittleFS.remove("/template.raw");
  // The next hello carries no template, so the server sends its current one.
  socket_->reconnect("clear_template");
}
void Runtime::loop(){
  if(socket_->connected()){
    uint32_t now=millis();
    for(auto &p:pending_){
      if(p.active && (!p.lastSent || now-p.lastSent>5000)){
        if(socket_->send(p.json))p.lastSent=now;
      }
    }
  }else{
    for(auto &p:pending_)if(p.active)p.lastSent=0;
  }
  if(rx_.active && millis()-rx_.lastAt>15000){fragAck(rx_.id,false,"incomplete");abortRx();}
  if(dataDirty_ && millis()-lastPersist_>30000){
    File f=LittleFS.open("/values.tmp","w");
    if(f){serializeJson(data_,f);f.close();LittleFS.remove("/values.json");
      if(LittleFS.rename("/values.tmp","/values.json")){lastPersist_=millis();dataDirty_=false;}}
  }
  if(hasTemplate_){
    // The frame comes from real time: a slow redraw skips frames instead of slowing the animation down.
    JsonObjectConst animation=tpl()["animation"];
    const int frame=animation.isNull()?-1:dyn::currentFrame(animation,millis()-animStart_);
    if(frame!=frame_){frame_=frame;dirty_=true;}
  }
  if(display_->serviceFeedback())dirty_=true;
  if(dirty_ && millis()-lastDraw_>120){
    lastDraw_=millis();dirty_=false;
    if(hasTemplate_)display_->render(tpl(),data_.as<JsonObjectConst>(),socket_->connected(),
      !buttonActions_ && tpl()["actions"].size()>1?selected_:-1,frame_,raw_);
    else display_->status(rx_.active?"Recibiendo plantilla":"Esperando plantilla",socket_->connected()?"WSS conectado":"Sin conexion WSS");
  }
}
