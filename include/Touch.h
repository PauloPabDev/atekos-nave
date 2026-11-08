#pragma once
#include <Arduino.h>
#include <Wire.h>
struct Tap {uint16_t x=0,y=0;uint32_t at=0;};
class Touch {
 public:
  bool begin();
  bool poll(Tap &event); // one tap per physical press
 private:
  bool lastDown_=false;
  uint32_t lastTap_=0, lastContact_=0, lastPoll_=0;
  bool readPoint(uint16_t &x,uint16_t &y,bool &down);
};
