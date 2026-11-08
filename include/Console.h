#pragma once
#include <Arduino.h>
#include "Display.h"
#include "Config.h"
#include "WssClient.h"
#include "Runtime.h"
// Serial console, one command per line. Command list: docs/comandos-consola.md.
class Console {
 public:
  void begin(Display& display,Config& config,WssClient& wss,Runtime& runtime);
  void poll();
 private:
  static constexpr size_t MAX_LINE=1024; // fits a pasted token or a `send` JSON
  Display* display_=nullptr; Config* config_=nullptr; WssClient* wss_=nullptr; Runtime* runtime_=nullptr;
  String line_;
  bool overflow_=false;
  void run(const String& cmd,const String& arg);
  void credentialsChanged(bool portal);
};
