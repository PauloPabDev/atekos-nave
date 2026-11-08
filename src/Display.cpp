#include "Display.h"
#include "Board.h"
#include <stdlib.h>

bool Display::begin(const String& deviceId){
  deviceId_=deviceId;
  pinMode(board::SD_CS, OUTPUT); digitalWrite(board::SD_CS, HIGH);
  pinMode(board::LCD_BL, OUTPUT); digitalWrite(board::LCD_BL, LOW);
  bus_ = new Arduino_HWSPI(board::LCD_DC, board::LCD_CS, board::LCD_SCK, board::LCD_MOSI);
  // IPS=true turns on color inversion, which this panel needs. begin() is true even with
  // wrong pins (SPI has no reply), so only looking at the screen confirms it.
  gfx_ = new Arduino_ST7789(bus_, board::LCD_RST, 0, true, board::WIDTH, board::HEIGHT, 34,0,34,0);
  if(!gfx_->begin(20000000)) return false;
  setBrightness(board::BACKLIGHT_PERCENT);
  status("MetaDisplay C6", "Iniciando...");
  return true;
}
void Display::setBrightness(uint8_t percent){
  percent = constrain(percent,0,50); // cap intentionally conservative
  analogWrite(board::LCD_BL, (uint8_t)(percent * 255 / 100));
}
uint16_t Display::color(const char* value, uint16_t fallback){
  if(!value || strlen(value)!=7 || value[0]!='#') return fallback;
  for(int i=1;i<7;i++) if(!isxdigit(value[i])) return fallback;
  uint32_t v=strtoul(value+1,nullptr,16);
  return gfx_->color565((v>>16)&255,(v>>8)&255,v&255);
}
void Display::status(const String& line1, const String& line2){
  if(!gfx_) return;
  gfx_->fillScreen(0x0809); gfx_->setTextColor(0xffff); gfx_->setTextSize(2);
  gfx_->setCursor(8,42); gfx_->print("MetaDisplay");
  gfx_->setTextSize(1); gfx_->setCursor(8,102); gfx_->print(line1);
  gfx_->setCursor(8,121); gfx_->print(line2);
  drawDeviceId(250);
}
// Label plus the ID in size 2. Size 2 is 12 px per char (15 chars = 180 px > 172 px),
// so chars go 11 px apart to keep the ID big and on one line.
void Display::drawDeviceId(int y){
  gfx_->setTextSize(1); gfx_->setTextColor(0xC618); gfx_->setCursor(8,y); gfx_->print("ID para tu servidor:");
  gfx_->setTextSize(2); gfx_->setTextColor(0xFFE0);
  const int x0=max(0,(board::WIDTH-(int)deviceId_.length()*11)/2);
  for(size_t i=0;i<deviceId_.length();i++){gfx_->setCursor(x0+i*11,y+14); gfx_->print(deviceId_[i]);}
}
void Display::portalInfo(const String& ssid,const String& pass,const String& ip,bool clientJoined){
  if(!gfx_) return;
  gfx_->fillScreen(0x0809);
  // Text above size 1 is centered; the built-in font is 6px wide per size step.
  auto line=[&](int y,uint8_t size,uint16_t c,const String& s){
    gfx_->setTextSize(size); gfx_->setTextColor(c);
    gfx_->setCursor(size>1?max(0,(board::WIDTH-(int)s.length()*6*size)/2):8,y); gfx_->print(s);
  };
  // 192.168.4.1 is the ESP32 soft-AP address when no static IP is set.
  const String ipNote=ip=="192.168.4.1"?"   (IP predeterminada)":"";
  line(14,2,0xffff,"MetaDisplay");
  drawDeviceId(262);
  if(clientJoined){
    line(38,1,0xC618,"Modo configuracion");
    line(84,2,0x07E0,"Alguien se");
    line(104,2,0x07E0,"conecto");
    line(150,1,0xffff,"Abre en el navegador:");
    line(164,2,0x07FF,ip);
    line(186,1,0xC618,ipNote);
    line(220,1,0xffff,"Si la pagina no se abre");
    line(232,1,0xffff,"sola, escribe esa IP.");
    return;
  }
  line(38,1,0xC618,"Sin red: modo configuracion");
  line(62,1,0xffff,"1. Conecta tu movil a");
  line(74,1,0xffff,"   la red WiFi:");
  line(88,1,0x07FF,"   "+ssid);
  line(112,1,0xffff,"2. Clave de la red:");
  line(126,3,0xFFE0,pass);
  line(162,1,0xffff,"3. Abre en el navegador:");
  line(176,2,0x07FF,ip);
  line(198,1,0xC618,ipNote);
  line(222,1,0xffff,"4. Elige tu WiFi, pon el");
  line(234,1,0xffff,"   servidor y el token,");
  line(246,1,0xffff,"   y pulsa Save.");
  line(304,1,0xC618,"Esperando conexion...");
}
String Display::text(JsonObjectConst el, JsonVariantConst data){
  const char* bind=el["bind"] | "";
  if(*bind){
    JsonVariantConst v=data[bind];
    if(v.is<const char*>()) return String(v.as<const char*>());
    if(v.is<bool>()) return v.as<bool>()?"true":"false";
    if(v.isNull()) return "";
    String output; serializeJson(v,output); return output;
  }
  return String(el["value"] | "");
}
float Display::number(JsonObjectConst el,JsonVariantConst data){
  const char* bind=el["bind"] | "";
  if(*bind) return data[bind].as<float>();
  return el["value"] | 0.f;
}
void Display::render(JsonVariantConst layout,JsonVariantConst values,bool online,int selectedAction){
  if(!gfx_) return;
  gfx_->fillScreen(color(layout["background"] | "#090D18",0x0000));
  for(JsonObjectConst e:layout["elements"].as<JsonArrayConst>()){
    const String kind=e["type"] | "";
    int x=e["x"]|0, y=e["y"]|0, w=e["w"]|0,h=e["h"]|0;
    uint16_t c=color(e["color"] | "#FFFFFF");
    if(kind=="text"){
      int size=constrain((int)(e["size"]|1),1,4);
      gfx_->setTextSize(size); gfx_->setTextColor(c); gfx_->setTextWrap(false);
      gfx_->setCursor(x,y); gfx_->print(text(e,values));
    } else if(kind=="rect" || kind=="button"){
      uint16_t fill=color(e["fillColor"] | "#151E36", c);
      if(e["filled"] | (kind=="button")) gfx_->fillRoundRect(x,y,w,h,constrain((int)(e["radius"]|0),0,10),fill);
      else gfx_->drawRect(x,y,w,h,c);
      if(kind=="button"){
        gfx_->setTextSize(constrain((int)(e["size"]|1),1,3)); gfx_->setTextColor(c);
        gfx_->setCursor(x+6,y+(h/2)-5); gfx_->print(text(e,values));
      }
    } else if(kind=="line"){
      gfx_->drawLine(x,y,e["x2"]|x,e["y2"]|y,c);
    } else if(kind=="progress"){
      if(w>=4 && h>=4){
        gfx_->drawRect(x,y,w,h,c);
        gfx_->fillRect(x+2,y+2,(int)((w-4)*constrain(number(e,values),0.f,100.f)/100.f),h-4,color(e["fillColor"]|"#9F78FA",c));
      }
    }
  }
  // Outline marks the action the button runs when the template has more than one.
  if(selectedAction>=0){
    JsonObjectConst a=layout["actions"][selectedAction];
    if(!a.isNull()) gfx_->drawRoundRect((a["x"]|0)-2,(a["y"]|0)-2,(a["w"]|0)+4,(a["h"]|0)+4,4,0xFFE0);
  }
  gfx_->fillCircle(board::WIDTH-7,7,3,online?0x07E0:0xF800);
  // A re-render would otherwise erase a toast that is still showing.
  if(feedbackActive_) drawFeedback();
}
void Display::feedback(const String& message,uint16_t col,uint32_t durationMs){
  if(!gfx_) return;
  feedbackUntil_=millis()+durationMs; feedbackActive_=true;
  feedbackText_=message.substring(0,24); feedbackColor_=col;
  drawFeedback();
}
void Display::drawFeedback(){
  gfx_->fillRoundRect(5,board::HEIGHT-40,board::WIDTH-10,35,5,0x2104);
  gfx_->setTextColor(feedbackColor_);gfx_->setTextSize(1);gfx_->setCursor(11,board::HEIGHT-27);
  gfx_->print(feedbackText_);
}
bool Display::serviceFeedback(){
  if(feedbackActive_ && (int32_t)(millis()-feedbackUntil_)>=0){feedbackActive_=false;return true;}
  return false;
}
