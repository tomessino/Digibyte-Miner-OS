#!/bin/bash
# Erzeugt Assets, baut den PC-Test, rendert alle Seiten und legt PNGs in tools/design/out/ ab.
set -e
cd "$(dirname "$0")/../.."
S=firmware/tomessino_dgb
O=tools/design/out
"${PY:-python3}" tools/design/gen_assets.py
g++ -std=c++17 -DUI_HOST -O1 -Wall -Wextra -I"$S" -I"$HOME/Arduino/libraries/ArduinoJson/src" tools/design/host_preview.cpp "$S/ui.cpp" "$S/net_parse.cpp" "$S/ui_assets_bg.cpp" "$S/ui_assets_fonts.cpp" -lm -o "$O/host_preview"
rc=0; "$O/host_preview" "$O" || rc=$?
"${PY:-python3}" tools/design/check_parse.py || rc=$?
"${PY:-python3}" tools/design/montage.py
rm -f "$O"/*.ppm "$O/parse.txt" "$O/host_preview"
exit $rc
