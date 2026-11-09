// Host checks of the parts with no hardware: dynamic layer (cases from atekos apps/api/test/display-render.test.ts)
// and stripImages. Run: test/host/run.sh
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <ArduinoJson.h>
inline void delay(unsigned long){}
#include "Dynamic.h"
#include "Images.h"

namespace {
JsonDocument json(const char* s){JsonDocument d; assert(deserializeJson(d,s)==DeserializationError::Ok); return d;}
// resolveElement(e, values) with the given params; out is null when the element ends up hidden.
JsonDocument resolve(const char* e,const char* values,const char* params="{}"){
  JsonDocument el=json(e), v=json(values), p=json(params), out;
  if(!dyn::resolveElement(el.as<JsonObjectConst>(),{v.as<JsonObjectConst>(),p.as<JsonObjectConst>()},out))out.clear();
  return out;
}
struct Mem { // a File for stripImages
  std::string s; size_t at=0;
  size_t read(uint8_t* b,size_t n){n=std::min(n,s.size()-at); memcpy(b,s.data()+at,n); at+=n; return n;}
  size_t write(uint8_t c){s+=char(c);return 1;}
  size_t write(const uint8_t* b,size_t n){s.append((const char*)b,n);return n;}
};
}

int main(){
  const char* text=R"({"type":"text","x":1,"y":1,"value":"base","color":"#FFFFFF")";
  auto with=[&](const char* extra){static std::string s; s=std::string(text)+","+extra+"}"; return s.c_str();};

  // bind and format give the final text in value; a missing or null variable is empty
  JsonDocument r=resolve(R"({"type":"text","x":1,"y":1,"bind":"a"})",R"({"a":7})");
  assert(r["value"]==7 && r["bind"].isNull());
  assert(resolve(with(R"("format":"T: {t} C {falta}{n}")"),R"({"t":21.5,"n":null})")["value"]=="T: 21.5 C ");
  assert(!dyn::has(resolve(R"({"type":"text","x":1,"y":1,"bind":"toString"})","{}").as<JsonObjectConst>(),"value"));
  assert(resolve(with(R"("format":"{a}{ {b} {}x{c")"),R"({"a":1,"b":"B"})")["value"]=="1{ B {}x{c");

  // binds convert to the property's type, else the base stays
  const char* c=R"({"type":"circle","x":50,"y":50,"r":10,"color":"#FFFFFF","binds":{"r":"r","color":"c","filled":"f"}})";
  r=resolve(c,R"({"r":"12.6","c":"#FF0000","f":1})");
  assert(r["r"]==13 && r["color"]=="#FF0000" && r["filled"]==true && r["binds"].isNull());
  r=resolve(c,R"({"r":"mucho","c":"rojo","f":"false"})");
  assert(r["r"]==10 && r["color"]=="#FFFFFF" && r["filled"]==false);
  assert(resolve(c,R"({"r":2.5})")["r"]==3 && resolve(c,R"({"r":-2.5})")["r"]==-2);
  assert(resolve(c,R"({"r":" 7 "})")["r"]==7 && resolve(c,R"({"r":""})")["r"]==10 && resolve(c,R"({"r":true})")["r"]==10);

  // when applies the matching rules in order, after binds; visible:false hides it
  const char* e=R"({"type":"text","x":1,"y":1,"value":"base","color":"#FFFFFF","binds":{"color":"c"},"when":[
    {"key":"t","op":"gt","value":30,"set":{"color":"#FF0000"}},
    {"key":"t","op":"gte","value":40,"set":{"color":"#00FF00","value":"muy alto"}},
    {"key":"ocultar","op":"truthy","set":{"visible":false}}]})";
  assert(resolve(e,R"({"t":20,"c":"#0000FF"})")["color"]=="#0000FF");
  assert(resolve(e,R"({"t":35,"c":"#0000FF"})")["color"]=="#FF0000");
  r=resolve(e,R"({"t":40})"); assert(r["color"]=="#00FF00" && r["value"]=="muy alto");
  assert(resolve(e,R"({"t":"35"})")["color"]=="#FF0000");  // gt compares as numbers
  assert(resolve(e,R"({"t":true})")["color"]=="#FFFFFF");  // a boolean is not a number
  assert(resolve(e,R"({"t":null})")["color"]=="#FFFFFF");
  assert(resolve(e,R"({"ocultar":"si"})").isNull());
  auto rule=[&](const char* op,const char* v){
    static std::string el; el=std::string(text)+R"(,"when":[{"key":"k","op":")"+op+R"(","value":5,"set":{"value":"si"}}]})";
    static std::string vals; vals=std::string(R"({"k":)")+v+"}";
    return resolve(el.c_str(),vals.c_str())["value"].as<std::string>();
  };
  assert(rule("eq","5")=="si" && rule("eq","5.0")=="si" && rule("eq","\"5\"")=="base" && rule("ne","\"5\"")=="si");
  assert(rule("falsy","0")=="si" && rule("lte","\" 5 \"")=="si" && rule("lt","\"\"")=="si" && rule("gt","\"x\"")=="base");
  assert(resolve(R"({"type":"text","x":1,"y":1,"value":"a","when":[{"key":"no","op":"ne","value":1,"set":{"value":"b"}}]})","{}")["value"]=="b");

  // params defaults sit under the received values; a received null wins over the default
  const char* params=R"({"temp":{"type":"number","default":21},"alarma":{"type":"boolean","default":false}})";
  const char* temp=R"({"type":"text","x":10,"y":38,"color":"#B6A3FF","format":"Temp: {temp} C",
    "when":[{"key":"temp","op":"gt","value":30,"set":{"color":"#FF5050"}}]})";
  r=resolve(temp,"{}",params); assert(r["value"]=="Temp: 21 C" && r["color"]=="#B6A3FF");
  r=resolve(temp,R"({"temp":45})",params); assert(r["value"]=="Temp: 45 C" && r["color"]=="#FF5050");
  assert(resolve(temp,R"({"temp":null})",params)["value"]=="Temp:  C");
  const char* tri=R"({"type":"polygon","points":[[12,162],[32,126],[52,162]],"visible":false,"binds":{"visible":"alarma"}})";
  assert(resolve(tri,"{}",params).isNull() && !resolve(tri,R"({"alarma":true})",params).isNull());

  // currentFrame walks the frames and, with repeat, stays on the last
  JsonDocument a=json(R"({"frames":[{"ms":100,"elements":[]},{"ms":300,"elements":[]}]})");
  const int loop[][2]={{0,0},{99,0},{100,1},{399,1},{400,0},{550,1}};
  for(auto& t:loop)assert(dyn::currentFrame(a.as<JsonObjectConst>(),t[0])==t[1]);
  a["repeat"]=2;
  const int twice[][2]={{0,0},{450,0},{550,1},{799,1},{800,1},{5000,1}};
  for(auto& t:twice)assert(dyn::currentFrame(a.as<JsonObjectConst>(),t[0])==t[1]);

  // stripImages: image data (elements and frames) becomes "@offset:len" into the source; other "data" stays
  Mem src,dst;
  src.s=R"({"type":"template.set","template":{"id":"t","elements":[{"type":"image","w":1,"h":1,"data":"AAAA"},
    {"type":"text","value":"data"}],"animation":{"frames":[{"ms":20,"elements":[{"data":"QUJD"}]}]},
    "actions":[{"id":"a","payload":{"data":"x"}}]}})";
  assert(stripImages(src,dst));
  JsonDocument s=json(dst.s.c_str());
  JsonObjectConst t=s["template"];
  uint32_t off,len;
  assert(imageRef(t["elements"][0]["data"],src.s.size(),off,len) && src.s.substr(off,len)=="AAAA");
  assert(imageRef(t["animation"]["frames"][0]["elements"][0]["data"],src.s.size(),off,len) && src.s.substr(off,len)=="QUJD");
  assert(t["elements"][1]["value"]=="data" && t["actions"][0]["payload"]["data"]=="x");
  Mem bad{R"({"elements":[{"data":"AA\"AA"}]})"},out;
  assert(!stripImages(bad,out));
  assert(!imageRef("@4294967295:4",100,off,len) && !imageRef("@0:5",100,off,len) && !imageRef("@0:4x",100,off,len));
  assert(imageRef("@96:4",100,off,len) && !imageRef("@97:4",100,off,len));

  // The showcase from atekos: its icon is stripped and still decodes to 16x16 RLE pixels
  std::ifstream f(std::string(TEST_DIR)+"/showcase-template.json");
  std::stringstream ss; ss<<f.rdbuf();
  JsonDocument full=json(ss.str().c_str()), msg;
  msg["type"]="template.set"; msg["template"]=full;
  Mem show,strip; serializeJson(msg,show.s);
  assert(stripImages(show,strip));
  JsonDocument small=json(strip.s.c_str());
  JsonObjectConst icon=small["template"]["elements"][1];
  assert(imageRef(icon["data"],show.s.size(),off,len));
  assert(show.s.substr(off,len)==full["elements"][1]["data"].as<std::string>());
  assert(strip.s.size()<show.s.size());

  puts("host tests OK");
}
