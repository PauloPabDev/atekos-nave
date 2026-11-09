#include "Dynamic.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
namespace dyn {
namespace {
  bool isColor(JsonVariantConst v){
    const char* s=v.as<const char*>();
    if(!s || strlen(s)!=7 || s[0]!='#')return false;
    for(int i=1;i<7;i++)if(!isxdigit((unsigned char)s[i]))return false;
    return true;
  }
  // Number() of JavaScript on a whole string, spaces around allowed. Blank is 0 in rules and invalid in binds.
  // shortcut: strtod also takes a signed hex ("-0x10") or hex float, which JS rejects; upgrade if a template needs it.
  bool parseNumber(const char* s,bool blankIsZero,double& out){
    while(isspace((unsigned char)*s))s++;
    if(!*s){out=0;return blankIsZero;}
    char* end; out=strtod(s,&end);
    if(end==s)return false;
    while(isspace((unsigned char)*end))end++;
    return !*end && isfinite(out);
  }
  // Number(v) of JavaScript as rules use it: true 1, false/null 0, strings parsed; a missing value is NaN.
  bool toNumber(JsonVariantConst v,bool exists,double& n){
    if(!exists)return false;
    if(v.isNull()){n=0;return true;}
    if(v.is<bool>()){n=v.as<bool>();return true;}
    if(v.is<double>()){n=v.as<double>();return true;}
    return v.is<const char*>() && parseNumber(v.as<const char*>(),true,n);
  }
  // === of JavaScript: same type and value (5 is 5.0, but not "5").
  bool same(bool ea,JsonVariantConst a,bool eb,JsonVariantConst b){
    if(!ea || !eb)return ea==eb;
    if(a.isNull() || b.isNull())return a.isNull() && b.isNull();
    if(a.is<bool>() || b.is<bool>())return a.is<bool>() && b.is<bool>() && a.as<bool>()==b.as<bool>();
    if(a.is<double>() || b.is<double>())return a.is<double>() && b.is<double>() && a.as<double>()==b.as<double>();
    return a.is<const char*>() && b.is<const char*>() && !strcmp(a.as<const char*>(),b.as<const char*>());
  }
  // A variable's value converted to the property's type; if it does not convert, the base value stays.
  void coerce(JsonString prop,JsonVariantConst v,JsonDocument& out){
    const char* p=prop.c_str();
    if(!strcmp(p,"color") || !strcmp(p,"fillColor")){if(isColor(v))out[prop].set(v);}
    else if(!strcmp(p,"filled") || !strcmp(p,"closed") || !strcmp(p,"visible"))out[prop]=truthy(v);
    else if(!strcmp(p,"value"))out[prop].set(v);
    else if(!strcmp(p,"format")){if(v.is<const char*>())out[prop].set(v);}
    else{
      double n;
      if(v.is<double>())n=v.as<double>();
      else if(!v.is<const char*>() || !parseNumber(v.as<const char*>(),false,n))return;
      out[prop]=floor(n+0.5); // Math.round: 2.5 -> 3, -2.5 -> -2
    }
  }
}
bool has(JsonObjectConst o,const char* key){
  for(JsonPairConst p:o)if(!strcmp(p.key().c_str(),key))return true;
  return false;
}
bool Scope::lookup(const char* key,JsonVariantConst& out) const{
  if(has(values,key)){out=values[key];return true;}
  JsonObjectConst p=params[key];
  if(has(p,"default")){out=p["default"];return true;}
  return false;
}
bool truthy(JsonVariantConst v){
  if(v.is<bool>())return v.as<bool>();
  if(v.is<double>())return v.as<double>()!=0;
  const char* s=v.as<const char*>();
  return s && *s && strcmp(s,"0") && strcmp(s,"false");
}
bool testRule(JsonObjectConst r,const Scope& s){
  JsonVariantConst a;
  const bool exists=s.lookup(r["key"]|"",a);
  const char* op=r["op"]|"";
  if(!strcmp(op,"truthy"))return truthy(a);
  if(!strcmp(op,"falsy"))return !truthy(a);
  const bool hasValue=has(r,"value");
  if(!strcmp(op,"eq"))return same(exists,a,hasValue,r["value"]);
  if(!strcmp(op,"ne"))return !same(exists,a,hasValue,r["value"]);
  double x,y;
  if(!exists || a.isNull() || a.is<bool>() || !toNumber(a,true,x) || !toNumber(r["value"],hasValue,y))return false;
  if(!strcmp(op,"gt"))return x>y;
  if(!strcmp(op,"gte"))return x>=y;
  if(!strcmp(op,"lt"))return x<y;
  if(!strcmp(op,"lte"))return x<=y;
  return false;
}
std::string text(JsonVariantConst v){
  if(v.isNull())return "";
  if(v.is<const char*>())return v.as<const char*>();
  if(v.is<bool>())return v.as<bool>()?"true":"false";
  std::string out; serializeJson(v,out); return out;
}
// Each {key} (1-40 chars, no braces) becomes that variable; missing or null leaves it empty, an unclosed { stays.
std::string formatText(const char* f,const Scope& s){
  std::string out;
  for(const char* p=f;*p;){
    if(*p=='{'){
      const char* q=p+1;
      while(*q && *q!='{' && *q!='}')q++;
      const size_t n=q-p-1;
      if(*q=='}' && n>=1 && n<=40){
        JsonVariantConst v;
        if(s.lookup(std::string(p+1,n).c_str(),v))out+=text(v);
        p=q+1;continue;
      }
    }
    out+=*p++;
  }
  return out;
}
// Order: base -> bind (shortcut of binds.value) -> binds -> each matching `when` in order -> visible -> format.
bool resolveElement(JsonObjectConst e,const Scope& s,JsonDocument& out){
  out.set(e); out.remove("binds"); out.remove("when");
  JsonVariantConst v;
  if(e["bind"].is<const char*>()){
    if(s.lookup(e["bind"].as<const char*>(),v))out["value"].set(v); else out.remove("value");
    out.remove("bind");
  }
  for(JsonPairConst p:e["binds"].as<JsonObjectConst>())
    if(p.value().is<const char*>() && s.lookup(p.value().as<const char*>(),v))coerce(p.key(),v,out);
  for(JsonObjectConst r:e["when"].as<JsonArrayConst>())
    if(testRule(r,s))for(JsonPairConst p:r["set"].as<JsonObjectConst>())out[p.key()].set(p.value());
  if(out["visible"].is<bool>() && !out["visible"].as<bool>())return false;
  if(out["format"].is<const char*>())out["value"]=formatText(out["format"].as<const char*>(),s);
  return true;
}
int currentFrame(JsonObjectConst animation,uint32_t elapsed){
  JsonArrayConst frames=animation["frames"];
  const uint32_t repeat=animation["repeat"]|0u;
  uint32_t total=0;
  for(JsonObjectConst f:frames)total+=f["ms"]|0u;
  if(!total)return 0;
  if(repeat>0 && uint64_t(elapsed)>=uint64_t(total)*repeat)return frames.size()-1;
  uint32_t t=elapsed%total; int i=0;
  for(JsonObjectConst f:frames){const uint32_t ms=f["ms"]|0u; if(t<ms)return i; t-=ms; i++;}
  return frames.size()-1;
}
}
