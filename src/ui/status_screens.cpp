#include "ui/status_screens.h"

#include <Arduino.h>

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "config.h"
#include "hardware/display.h"
#include "hardware/display_font.h"
#include "services/device_settings.h"
#include "ui/radar_theme.h"

namespace {

constexpr int kLineGap = 7;
constexpr int kCenterX = config::kDisplayWidth / 2;
constexpr int kCenterY = config::kDisplayHeight / 2;

constexpr int kSpinnerDotCount = 12;
constexpr int kSpinnerRadius = 166;
constexpr int kSpinnerDotRadius = 4;
constexpr int kSpinnerEraseRadius = 7;
constexpr float kSpinnerStepDeg = 6.0f;

constexpr uint16_t kColorGreen = ui::radar::rgb565(90, 225, 140);
constexpr uint16_t kColorDim = ui::radar::rgb565(150, 165, 185);

struct SpinnerDot {
  int x = 0;
  int y = 0;
  bool drawn = false;
};

char s_connecting_ssid[33];
char s_ssid_line[40];
constexpr int kConnectingTextMaxWidthPx = 300;
float s_spinner_angle_deg = -90.0f;
SpinnerDot s_spinner_dots[kSpinnerDotCount];
bool s_connecting_text_drawn = false;

struct TextLine {
  const char* text;
  UiFont font;
  uint16_t color;
};

int lineHeight(UiFont font) {
  tft.setFont(uiFont(font));
  tft.setTextSize(1);
  return tft.fontHeight();
}

/** Largest font, starting from `wanted`, in which `text` fits the round screen. */
UiFont fitFont(const char* text, UiFont wanted) {
  constexpr int kMaxWidth = 322;
  int f = static_cast<int>(wanted);
  while (f > 0) {
    tft.setFont(uiFont(static_cast<UiFont>(f)));
    if (tft.textWidth(text) <= kMaxWidth) {
      break;
    }
    --f;
  }
  return static_cast<UiFont>(f);
}

/** Vertically centred block of centred lines on a solid background. */
void drawTextBlock(uint16_t bg, const TextLine* lines, size_t count) {
  tft.fillScreen(bg);
  tft.setTextDatum(textdatum_t::middle_center);

  int total_h = 0;
  for (size_t i = 0; i < count; ++i) {
    total_h += lineHeight(lines[i].font);
    if (i + 1 < count) {
      total_h += kLineGap;
    }
  }

  int y = (config::kDisplayHeight - total_h) / 2;
  for (size_t i = 0; i < count; ++i) {
    const int h = lineHeight(lines[i].font);
    tft.setTextColor(lines[i].color, bg);
    tft.drawString(lines[i].text, kCenterX, y + h / 2);
    y += h + kLineGap;
  }
}

/** SSID on one line; truncate with an ellipsis if wider than the text area. */
void fitSsidLine() {
  strncpy(s_ssid_line, s_connecting_ssid, sizeof(s_ssid_line) - 1);
  s_ssid_line[sizeof(s_ssid_line) - 1] = '\0';
  tft.setFont(uiFont(UiFont::kBody22));
  if (tft.textWidth(s_ssid_line) <= kConnectingTextMaxWidthPx) {
    return;
  }
  const size_t len = strlen(s_connecting_ssid);
  for (size_t n = len; n > 0; --n) {
    snprintf(s_ssid_line, sizeof(s_ssid_line), "%.*s\xE2\x80\xA6", static_cast<int>(n),
             s_connecting_ssid);
    if (tft.textWidth(s_ssid_line) <= kConnectingTextMaxWidthPx) {
      return;
    }
  }
  strncpy(s_ssid_line, "\xE2\x80\xA6", sizeof(s_ssid_line) - 1);
  s_ssid_line[sizeof(s_ssid_line) - 1] = '\0';
}

void drawConnectingText() {
  const TextLine lines[] = {
      {"Connecting to", UiFont::kBody22, kColorDim},
      {s_ssid_line, UiFont::kTitle26, config::kTextOnBlack},
  };
  drawTextBlock(config::kColorBlack, lines, sizeof(lines) / sizeof(lines[0]));
  s_connecting_text_drawn = true;
}

void eraseSpinnerDots() {
  for (int i = 0; i < kSpinnerDotCount; ++i) {
    if (!s_spinner_dots[i].drawn) {
      continue;
    }
    tft.fillCircle(s_spinner_dots[i].x, s_spinner_dots[i].y, kSpinnerEraseRadius,
                   config::kColorBlack);
    s_spinner_dots[i].drawn = false;
  }
}

void drawSpinnerDots() {
  constexpr float kDegToRad = 0.01745329252f;
  const float head_rad = s_spinner_angle_deg * kDegToRad;

  for (int i = 0; i < kSpinnerDotCount; ++i) {
    const float a = head_rad - static_cast<float>(i) * (6.283185307f / kSpinnerDotCount);
    const int x = kCenterX + static_cast<int>(std::lround(std::cos(a) * kSpinnerRadius));
    const int y = kCenterY + static_cast<int>(std::lround(std::sin(a) * kSpinnerRadius));

    const int fade = 255 - i * 19;
    const uint16_t color = ui::radar::rgb565(static_cast<uint8_t>(fade / 4),
                                             static_cast<uint8_t>(fade),
                                             static_cast<uint8_t>(fade / 2));
    tft.fillSmoothCircle(x, y, kSpinnerDotRadius, color);

    s_spinner_dots[i].x = x;
    s_spinner_dots[i].y = y;
    s_spinner_dots[i].drawn = true;
  }
}

}  // namespace

void statusScreenConnectingBegin(const char* ssid) {
  const char* name = (ssid != nullptr && ssid[0] != '\0') ? ssid : "network";
  strncpy(s_connecting_ssid, name, sizeof(s_connecting_ssid) - 1);
  s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
  fitSsidLine();
  s_spinner_angle_deg = -90.0f;
  for (auto& dot : s_spinner_dots) {
    dot.drawn = false;
  }
  s_connecting_text_drawn = false;
  drawConnectingText();
  drawSpinnerDots();
}

void statusScreenConnectingTick() {
  if (!s_connecting_text_drawn) {
    drawConnectingText();
  }
  eraseSpinnerDots();
  s_spinner_angle_deg += kSpinnerStepDeg;
  if (s_spinner_angle_deg >= 270.0f) {
    s_spinner_angle_deg -= 360.0f;
  }
  drawSpinnerDots();
}

void statusScreenConnected(const char* ssid, const char* ip, uint16_t http_port) {
  char url[40];
  char host[64];
  if (http_port == 80) {
    snprintf(url, sizeof(url), "%s", ip);
    snprintf(host, sizeof(host), "%s", services::device::hostUrl());
  } else {
    snprintf(url, sizeof(url), "%s:%u", ip, static_cast<unsigned>(http_port));
    snprintf(host, sizeof(host), "%s:%u", services::device::hostUrl(),
             static_cast<unsigned>(http_port));
  }
  strncpy(s_connecting_ssid, (ssid != nullptr && ssid[0] != '\0') ? ssid : "Wi-Fi",
          sizeof(s_connecting_ssid) - 1);
  s_connecting_ssid[sizeof(s_connecting_ssid) - 1] = '\0';
  fitSsidLine();

  const TextLine lines[] = {
      {"Connected", UiFont::kTitle26, kColorGreen},
      {s_ssid_line, UiFont::kBody22, config::kTextOnBlack},
      {"", UiFont::kSmall14, kColorDim},
      {"Web page:", UiFont::kCardinal20, kColorDim},
      {url, UiFont::kTitle26, config::kTextOnBlack},
      {host, fitFont(host, UiFont::kCardinal20), kColorDim},
  };
  drawTextBlock(config::kColorBlack, lines, sizeof(lines) / sizeof(lines[0]));
  s_connecting_text_drawn = false;
}

void statusScreenPortal() {
  const uint16_t fg = config::kTextOnYellow;
  const TextLine lines[] = {
      {"Wi-Fi setup", UiFont::kTitle26, fg},
      {"1. Join network:", UiFont::kBody22, fg},
      {services::device::setupApName(), UiFont::kTitle26, fg},
      {"2. Open in browser:", UiFont::kBody22, fg},
      {"192.168.4.1", UiFont::kTitle26, fg},
  };
  drawTextBlock(config::kColorYellow, lines, sizeof(lines) / sizeof(lines[0]));
}

void statusScreenConnectFailed() {
  const uint16_t fg = config::kTextOnYellow;
  const TextLine lines[] = {
      {"Could not connect", UiFont::kTitle26, fg},
      {"Check Wi-Fi password", UiFont::kBody22, fg},
      {"and signal strength.", UiFont::kBody22, fg},
      {"Hold BOOT 3 sec", UiFont::kBody22, fg},
      {"to reset Wi-Fi", UiFont::kBody22, fg},
  };
  drawTextBlock(config::kColorYellow, lines, sizeof(lines) / sizeof(lines[0]));
}

void statusScreenWifiReset() {
  const uint16_t fg = config::kTextOnYellow;
  const TextLine lines[] = {
      {"Wi-Fi reset", UiFont::kTitle26, fg},
      {"Restarting...", UiFont::kBody22, fg},
  };
  drawTextBlock(config::kColorYellow, lines, sizeof(lines) / sizeof(lines[0]));
}
