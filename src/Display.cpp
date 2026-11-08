#include "Display.h"
#include "Board.h"
#include <stdlib.h>

// JD9853 Waveshare panel uses ST7789-compatible drawing with vendor register init.
// Register sequence references the vendor's public JD9853 example.
namespace {
const uint8_t panelInit[] = {
  BEGIN_WRITE, WRITE_COMMAND_8, 0x11, END_WRITE, DELAY, 120,
  BEGIN_WRITE,
  WRITE_C8_D16, 0xDF, 0x98, 0x53,
  WRITE_C8_D8, 0xB2, 0x23,
  WRITE_COMMAND_8, 0xB7, WRITE_BYTES, 4, 0x00,0x47,0x00,0x6F,
  WRITE_COMMAND_8, 0xBB, WRITE_BYTES, 6, 0x1C,0x1A,0x55,0x73,0x63,0xF0,
  WRITE_C8_D16, 0xC0, 0x44,0xA4,
  WRITE_C8_D8, 0xC1, 0x16,
  WRITE_COMMAND_8, 0xC3, WRITE_BYTES, 8, 0x7D,0x07,0x14,0x06,0xCF,0x71,0x72,0x77,
  WRITE_COMMAND_8, 0xC4, WRITE_BYTES, 12, 0x00,0x00,0xA0,0x79,0x0B,0x0A,0x16,0x79,0x0B,0x0A,0x16,0x82,
  WRITE_COMMAND_8, 0xC8, WRITE_BYTES, 32,
  0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,0x04,0x00,
  0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,0x04,0x00,
  WRITE_COMMAND_8, 0x21,
  END_WRITE, DELAY, 10,
  BEGIN_WRITE, WRITE_COMMAND_8, 0x29, END_WRITE
};
}

bool Display::begin(){
  pinMode(board::SD_CS, OUTPUT); digitalWrite(board::SD_CS, HIGH);
  pinMode(board::LCD_BL, OUTPUT); digitalWrite(board::LCD_BL, LOW);
  bus_ = new Arduino_ESP32SPI(board::LCD_DC, board::LCD_CS, board::LCD_SCK, board::LCD_MOSI, -1);
  gfx_ = new Arduino_ST7789(bus_, board::LCD_RST, 0, true, board::WIDTH, board::HEIGHT, 34,0,34,0);
  if(!gfx_->begin(20000000)) return false;
  bus_->batchOperation(panelInit, sizeof(panelInit));
  gfx_->setRotation(0);
  gfx_->invertDisplay(true);
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
void Display::render(JsonVariantConst layout,JsonVariantConst values,bool online){
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
  gfx_->fillCircle(board::WIDTH-7,7,3,online?0x07E0:0xF800);
  if(feedbackActive_) serviceFeedback();
}
void Display::feedback(const String& message,uint16_t col,uint32_t durationMs){
  if(!gfx_) return;
  feedbackUntil_=millis()+durationMs; feedbackActive_=true;
  gfx_->fillRoundRect(5,board::HEIGHT-40,board::WIDTH-10,35,5,0x2104);
  gfx_->setTextColor(col);gfx_->setTextSize(1);gfx_->setCursor(11,board::HEIGHT-27);
  gfx_->print(message.substring(0,24));
}
bool Display::serviceFeedback(){
  if(feedbackActive_ && (int32_t)(millis()-feedbackUntil_)>=0){feedbackActive_=false;return true;}
  return false;
}
