#!/usr/bin/env bash
# Build the PC preview of the radar UI (no hardware needed).
#   pio pkg install            # once, fetches LovyanGFX + ArduinoJson into .pio/libdeps
#   tools/host_preview/build.sh
#   tools/host_preview/host_preview data out 40.64 -73.78 1 0 snapshot.json
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
libs="$root/.pio/libdeps/supermini"
lgfx="$libs/LovyanGFX/src"
obj="$here/.obj"
mkdir -p "$obj"

flags=(-std=gnu++17 -O2 -w -DPLANE_RADAR_HOST -DLGFX_LINUX_FB
       -I"$here" -I"$root/include" -I"$lgfx" -I"$libs/ArduinoJson/src")

srcs=("$here/host_main.cpp"
      "$root/src/ui/radar_display.cpp" "$root/src/ui/runway_overlay.cpp"
      "$root/src/ui/strip_canvas.cpp" "$root/src/ui/map_layer.cpp"
      "$root/src/ui/radar_range.cpp" "$root/src/ui/status_screens.cpp"
      "$root/src/services/radar_location.cpp" "$root/src/services/adsb_store.cpp"
      "$root/src/data/large_airports_data.cpp" "$root/src/data/aircraft_icons_data.cpp"
      "$lgfx/lgfx/v1/LGFXBase.cpp" "$lgfx/lgfx/v1/lgfx_fonts.cpp"
      "$lgfx/lgfx/v1/lgfx_v1.cpp" "$lgfx/lgfx/v1/lgfx_v1_platforms.cpp"
      "$lgfx/lgfx/v1/lgfx_v1_panel.cpp")

objs=()
for s in "${srcs[@]}"; do
  o="$obj/$(basename "$s").o"
  if [ ! -f "$o" ] || [ "$s" -nt "$o" ] || [ -n "$(find "$root/include" "$here" -newer "$o" -name '*.h*' -print -quit)" ]; then
    g++ "${flags[@]}" -c "$s" -o "$o"
  fi
  objs+=("$o")
done
for s in "$lgfx"/lgfx/utility/*.c "$lgfx"/lgfx/Fonts/efont/*.c "$lgfx"/lgfx/Fonts/IPA/*.c; do
  o="$obj/$(basename "$s").o"
  [ -f "$o" ] || gcc -O2 -w -I"$lgfx" -c "$s" -o "$o"
  objs+=("$o")
done
g++ "${objs[@]}" -o "$here/host_preview" -lpthread
echo "built $here/host_preview"
