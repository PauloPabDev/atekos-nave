#pragma once
#include <Arduino.h>
class Display;
class FirmwareUpdate {
 public:
  bool install(const String& url, const String& expectedSha256Hex,
               const String& signatureBase64, const String& customCa,
               Display& display, String& error);
};
