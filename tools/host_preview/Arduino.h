// Minimal Arduino shim for the PC preview build (tools/host_preview).
#pragma once

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

unsigned long millis();
inline void delay(unsigned long) {}
inline unsigned long micros() { return millis() * 1000UL; }
void hostSetMillis(unsigned long ms);

struct HostSerial {
  void println(const char* s = "") { std::printf("%s\n", s); }
  void printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
  }
};
extern HostSerial Serial;
