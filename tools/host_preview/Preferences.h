// In-memory no-op Preferences for the PC preview build: nothing is stored.
#pragma once

#include <cstdint>

class Preferences {
 public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool isKey(const char*) { return false; }
  bool remove(const char*) { return true; }
  bool clear() { return true; }
  uint8_t getUChar(const char*, uint8_t d = 0) { return d; }
  uint16_t getUShort(const char*, uint16_t d = 0) { return d; }
  bool getBool(const char*, bool d = false) { return d; }
  double getDouble(const char*, double d = 0) { return d; }
  size_t putUChar(const char*, uint8_t) { return 1; }
  size_t putUShort(const char*, uint16_t) { return 2; }
  size_t putBool(const char*, bool) { return 1; }
  size_t putDouble(const char*, double) { return 8; }
};
