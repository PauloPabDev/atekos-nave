#pragma once
#include <ArduinoJson.h>
#include <string>
// Dynamic layer of a template (binds, when, format, animation): a translation of render.ts in atekos
// (packages/shared/src/render.ts). Only declarative comparisons and substitutions, never code.
namespace dyn {
// A variable is the received value, even null, or else the `default` of template.params.<key> (withDefaults).
struct Scope {
  JsonObjectConst values, params;
  bool lookup(const char* key,JsonVariantConst& out) const;
};
bool has(JsonObjectConst o,const char* key); // present, even if null
bool truthy(JsonVariantConst v);             // a null variant also stands for a missing variable
bool testRule(JsonObjectConst rule,const Scope& s);
std::string formatText(const char* format,const Scope& s);
// Value as drawn text: strings as is, true/false, null empty, numbers as serializeJson prints them.
std::string text(JsonVariantConst v);
// Copies e into out with bind, binds, when and format applied; false if it ends up hidden (visible: false).
bool resolveElement(JsonObjectConst e,const Scope& s,JsonDocument& out);
// Frame shown elapsedMs after the start; repeat 0 (or absent) = forever, after the last lap it stays on the last.
int currentFrame(JsonObjectConst animation,uint32_t elapsedMs);
}
