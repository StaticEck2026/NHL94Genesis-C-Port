#!/usr/bin/env bash
# P0.3: build the VANILLA=1 game with the ROM page tracker (amiga/tools/romtouch.c) and run it over
# the boot-based scripts. Writes out/amiga/romtouch/<run>.pages (per 60-vblank window, the 68k addresses of
# the 4 KB pages read; no ROM bytes) and prints amiga/tools/romsummary.py's tables. Usage: amiga/tools/rommap.sh ROM
set -euo pipefail
cd "$(dirname "$0")/../.."
ROM="${1:?usage: amiga/tools/rommap.sh ROM}"
B=build/amiga-tools
OUT=out/amiga/romtouch
mkdir -p "$B" "$OUT"
[ -x build/nhl94-vanilla ] || make VANILLA=1 nhl94
cc -std=gnu11 -O2 -Wall -Wextra -Werror -Isrc -c amiga/tools/romtouch.c -o "$B/romtouch.o"
objs=$(ls build/obj-vanilla/src/*.o build/obj-vanilla/native/*.o build/obj-vanilla/native/vanilla/*.o build/obj-vanilla/native/overbar/*.o)
# shellcheck disable=SC2086
cc -o "$B/romtouch" $objs "$B/romtouch.o" -Wl,--wrap=rom_load -Wl,--wrap=hal_wait_vblank ext/pulseman/build/release/libmdrt.a -lSDL2 -lm -lstdc++

run() {   # name frames [script]
    local name=$1 frames=$2 args=(--rom "$ROM" --headless --boot --frames "$2")
    [ $# -ge 3 ] && args+=(--input "tools/oracle/scripts/$3")
    ROMTOUCH_OUT="$OUT/$name.pages" "$B/romtouch" "${args[@]}" >/dev/null 2>&1 || echo "$name: exit $?"
    echo "$name: $(wc -l < "$OUT/$name.pages") windows"
}
run attract 7200
run full_game 16000 full_game_30s.txt
run playoff_game 15000 playoff_game_30s.txt
run records_game 15700 records_game_30s.txt
run pause_menus 8600 pause_menus.txt
run shootout_menus 4800 shootout_menus.txt
run name_entry 3000 name_entry.txt
python3 -I amiga/tools/romsummary.py "$OUT"
