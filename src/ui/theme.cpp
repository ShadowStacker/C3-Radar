#include "ui/theme.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstring>

#include "ui/radar_theme.h"

namespace ui::theme {

//                    background scope     grid      text      aircraft  callsign  type      altitude
//                    airports   water     roads     borders   towns     military
const ThemeSpec kPresets[kPresetCount] = {
    {"Night navy",
     {0x02050E, 0x061026, 0x1A6846, 0xFFFFFF, 0xFFC828, 0xFFFFFF, 0xFFC83C, 0x6ECDFF,
      0x3896AA, 0x5096DC, 0xC88E3E, 0xAA7DC3, 0xBEC8DA, 0xFF4030},
     true},
    {"Classic green",
     {0x000400, 0x001208, 0x0F7A2E, 0x7CFF9B, 0x5CFF7A, 0xB8FFC6, 0x5CFF7A, 0x3FD060,
      0x2FA04A, 0x1F7F4F, 0x2A6A2A, 0x4A8A3A, 0x5FAF6F, 0xFFE040},
     false},
    {"Amber",
     {0x060300, 0x140A00, 0x8A5A10, 0xFFC860, 0xFFB020, 0xFFE0A0, 0xFFB020, 0xE09020,
      0xB07818, 0x7A5A28, 0x6A4410, 0x8A6A30, 0xB89050, 0xFF5030},
     false},
    {"Night red",
     {0x000000, 0x0C0000, 0x6A0C0C, 0xE03030, 0xFF3A3A, 0xFF6A6A, 0xD03030, 0xB02828,
      0x8A2020, 0x5A1818, 0x4A1010, 0x6A2020, 0x8A3030, 0xFF9A30},
     false},
    {"Daylight",
     {0xC8D2DC, 0xF2F5F8, 0x7A9AB0, 0x102030, 0x1050C0, 0x101820, 0x8A4A00, 0x0A5AA0,
      0x2A7A8A, 0x2A78D0, 0xB06A10, 0x8A4AA0, 0x506070, 0xD01818},
     false},
};

namespace {

constexpr char kNamespace[] = "theme";
constexpr char kKeyId[] = "id";
constexpr char kKeyCustom[] = "custom";
constexpr char kKeyCustomAlt[] = "customAlt";

uint8_t s_id = 0;
ThemeSpec s_custom = {"Custom", {}, true};
Palette s_palette;

struct Rgb8 {
  int r, g, b;
};

Rgb8 split(uint32_t rgb) {
  return {static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF),
          static_cast<int>(rgb & 0xFF)};
}

uint16_t to565(Rgb8 c) {
  return radar::rgb565(static_cast<uint8_t>(c.r), static_cast<uint8_t>(c.g),
                       static_cast<uint8_t>(c.b));
}

/** a moved pct percent of the way toward b. */
Rgb8 mix(Rgb8 a, Rgb8 b, int pct) {
  return {a.r + (b.r - a.r) * pct / 100, a.g + (b.g - a.g) * pct / 100,
          a.b + (b.b - a.b) * pct / 100};
}

int luma(Rgb8 c) { return (c.r * 3 + c.g * 6 + c.b) / 10; }

void derive(const ThemeSpec& spec) {
  const Rgb8 bg = split(spec.rgb[kSlotBackground]);
  const Rgb8 scope = split(spec.rgb[kSlotScope]);
  const Rgb8 grid = split(spec.rgb[kSlotGrid]);
  const Rgb8 text = split(spec.rgb[kSlotText]);
  const Rgb8 water = split(spec.rgb[kSlotWater]);
  const Rgb8 roads = split(spec.rgb[kSlotRoads]);
  const Rgb8 towns = split(spec.rgb[kSlotTowns]);
  const Rgb8 mil = split(spec.rgb[kSlotMilitary]);
  const Rgb8 airports = split(spec.rgb[kSlotAirports]);
  const Rgb8 black = {0, 0, 0};
  const Rgb8 white = {255, 255, 255};
  // "Stronger" means away from the scope colour: lighter on dark themes,
  // darker on light ones.
  const bool dark = luma(scope) < 128;
  const Rgb8 strong = dark ? white : black;

  Palette& p = s_palette;
  p.bezel = to565(bg);
  p.scope = to565(scope);
  p.ring = to565(grid);
  p.ring_outer = to565(mix(grid, strong, 38));
  p.cross = to565(mix(grid, scope, 45));
  p.tick = to565(mix(grid, text, 30));
  p.label = to565(text);
  p.north = to565(split(spec.rgb[kSlotType]));
  p.scale = to565(mix(grid, strong, 45));
  p.center = to565(text);
  p.tag_back = to565(dark ? bg : scope);
  p.callsign = to565(split(spec.rgb[kSlotCallsign]));
  p.tag_type = to565(split(spec.rgb[kSlotType]));
  p.tag_alt = to565(split(spec.rgb[kSlotAltitude]));
  p.runway = to565(airports);
  p.runway_label = to565(mix(airports, strong, 35));
  p.town_major = to565(towns);
  p.town_minor = to565(mix(towns, scope, 35));
  p.military = to565(mil);
  p.mil_callsign = to565(mix(mil, strong, 40));
  p.badge_back = to565(mix(mil, black, 55));
  p.badge_text = to565(white);
  p.outline = to565(dark ? mix(bg, black, 60) : mix(scope, white, 60));
  p.aircraft = to565(split(spec.rgb[kSlotAircraft]));
  p.aircraft_unknown = to565(mix(text, scope, 35));
  p.coast = to565(water);
  p.lake = to565(mix(water, scope, 15));
  p.river = to565(mix(water, scope, 30));
  p.border = to565(split(spec.rgb[kSlotBorders]));
  p.motorway = to565(roads);
  p.trunk = to565(mix(roads, scope, 32));
  p.altitude_colors = spec.altitude_colors;
}

const ThemeSpec& activeSpec() {
  return s_id < kPresetCount ? kPresets[s_id] : s_custom;
}

}  // namespace

void init() {
  memcpy(s_custom.rgb, kPresets[0].rgb, sizeof(s_custom.rgb));
  s_custom.altitude_colors = kPresets[0].altitude_colors;

  Preferences prefs;
  if (prefs.begin(kNamespace, true)) {
    const uint8_t id = prefs.getUChar(kKeyId, 0);
    s_id = (id < kPresetCount || id == kCustomId) ? id : 0;
    uint32_t stored[kSlotCount];
    if (prefs.getBytes(kKeyCustom, stored, sizeof(stored)) == sizeof(stored)) {
      memcpy(s_custom.rgb, stored, sizeof(stored));
    }
    s_custom.altitude_colors = prefs.getBool(kKeyCustomAlt, s_custom.altitude_colors);
    prefs.end();
  }
  derive(activeSpec());
}

const Palette& palette() { return s_palette; }

uint8_t current() { return s_id; }

bool select(uint8_t id) {
  if (id >= kPresetCount && id != kCustomId) {
    return false;
  }
  if (id != s_id) {
    s_id = id;
    Preferences prefs;
    if (prefs.begin(kNamespace, false)) {
      prefs.putUChar(kKeyId, s_id);
      prefs.end();
    }
  }
  derive(activeSpec());
  return true;
}

const ThemeSpec& custom() { return s_custom; }

void setCustom(const uint32_t rgb[kSlotCount], bool altitude_colors) {
  for (size_t i = 0; i < kSlotCount; ++i) {
    s_custom.rgb[i] = rgb[i] & 0xFFFFFFu;
  }
  s_custom.altitude_colors = altitude_colors;
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.putBytes(kKeyCustom, s_custom.rgb, sizeof(s_custom.rgb));
    prefs.putBool(kKeyCustomAlt, altitude_colors);
    prefs.end();
  }
  if (s_id == kCustomId) {
    derive(s_custom);
  }
}

}  // namespace ui::theme
