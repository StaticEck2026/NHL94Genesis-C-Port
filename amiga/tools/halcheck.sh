#!/usr/bin/env bash
# Build amiga/tools/halcheck and check amiga/host/hal_core.c against every vanilla golden run
# (out/golden, from make golden). Writes out/amiga/ref/<run>.h32, the per-frame work RAM hashes the
# Amiga verify build must reproduce (A-LOGIC), and <run>.i32, the per-pass hashes of the display
# requests (A-DISPLAY-INTENT, amiga/host/intent.h). Both are ROM-derived: never in git or on a released
# disk. Usage: amiga/tools/halcheck.sh ROM
set -euo pipefail
cd "$(dirname "$0")/../.."
ROM="${1:?usage: amiga/tools/halcheck.sh ROM}"
mkdir -p build/amiga-tools out/amiga/ref
SRC=$(ls src/*.c | grep -v '^src/z80.c$')
# shellcheck disable=SC2086
cc -std=gnu11 -O2 -g -Wall -Wextra -Wshadow -Wno-unused-parameter -Werror -Isrc -Iamiga/host \
    -o build/amiga-tools/halcheck $SRC amiga/host/hal_core.c amiga/tools/halcheck.c
S=tools/oracle/scripts
fail=0
check() {   # name frames [script]
    local g=out/golden/$1.golden
    [ -f "$g" ] || { echo "$1: no $g (make golden)"; fail=1; return; }
    ./build/amiga-tools/halcheck -o "out/amiga/ref/$1.h32" -i "out/amiga/ref/$1.i32" "$ROM" "$g" "$(( $2 - 1 ))" ${3:+"$S/$3"} || fail=1
}
check attract 7200
check full_game 16000 full_game_30s.txt
check playoff_game 15000 playoff_game_30s.txt
check records_game 15700 records_game_30s.txt
check pause_menus 8600 pause_menus.txt
check shootout_menus 4800 shootout_menus.txt
check name_entry 3000 name_entry.txt
exit $fail
