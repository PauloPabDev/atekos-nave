#include "Touch.h"
#include "Board.h"
namespace {constexpr uint8_t TOUCH_REG=0x01;}
bool Touch::begin(){
  Wire.begin(board::TP_SDA,board::TP_SCL,400000);
  pinMode(board::TP_INT,INPUT_PULLUP);
  pinMode(board::TP_RST,OUTPUT);
  digitalWrite(board::TP_RST,LOW); delay(30);
  digitalWrite(board::TP_RST,HIGH); delay(180);
  Wire.beginTransmission(board::TOUCH_ADDR);
  return Wire.endTransmission()==0;
}
bool Touch::readPoint(uint16_t &x,uint16_t &y,bool &down){
  down=false;
  Wire.beginTransmission(board::TOUCH_ADDR);
  Wire.write(TOUCH_REG);
  if(Wire.endTransmission(false)!=0) return false;
  if(Wire.requestFrom((uint8_t)board::TOUCH_ADDR,(uint8_t)8)!=8) return false;
  uint8_t data[8];
  for(auto &v:data)v=Wire.read();
  down=(data[1]>0 && data[1]<=5);
  if(!down) return true;
  uint16_t rawx=((data[2]&0x0F)<<8)|data[3];
  uint16_t rawy=((data[4]&0x0F)<<8)|data[5];
  if(rawx>=board::WIDTH || rawy>=board::HEIGHT) return false;
  x=board::TOUCH_MIRROR_X?(board::WIDTH-1-rawx):rawx;
  y=board::TOUCH_MIRROR_Y?(board::HEIGHT-1-rawy):rawy;
  return true;
}
bool Touch::poll(Tap& event){
  const uint32_t now=millis();
  if(now-lastPoll_<30) return false;
  lastPoll_=now;
  uint16_t x=0,y=0;bool down=false;
  if(!readPoint(x,y,down)){
    if(now-lastContact_>350)lastDown_=false;
    return false;
  }
  if(!down){lastDown_=false;return false;}
  lastContact_=now;
  if(lastDown_ || now-lastTap_<130) return false;
  lastDown_=true;lastTap_=now;
  event={x,y,now};
  return true;
}
