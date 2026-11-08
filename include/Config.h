#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include "Display.h"
struct Settings {String url, token, caPem, portalPass;};
class Config {
 public:
  bool begin(Display &display);
  static String deviceId(); // md-<MAC>: the name to register on the server
  const Settings& get() const {return settings_;}
  bool portal(Display &display);
  bool stagePrivateCA(const String &base64Der);
  bool activateStagedCA();
  void confirmTrial();
  void rollbackTrialIfNeeded(uint32_t startedAt);
  void factoryReset(); // never returns: reboots into the setup portal
 private:
  Settings settings_;
  Preferences prefs_;
  bool save(const String &url,const String &token,const String &caBody);
};
