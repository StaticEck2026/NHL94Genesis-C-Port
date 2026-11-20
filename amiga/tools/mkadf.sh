#!/usr/bin/env bash
# Build a bootable NHL94AGA ADF (880 KB DD, FFS, label NHL94AGA, a standard bootblock): the boot test
# (NHL94AGA), the P5 verify program (NHL94VFY) with the repository's input scripts, amiga/disk/, and
# amiga/CHANGELOG.md as Changelog.txt. No ROM bytes go on it (A-LEGAL, amiga/tools/noretail.py).
# With REFS=DIR (amiga/tools/mkverify.sh), DIR/*.h32 go on as refs/: a PRIVATE disk, made from the ROM,
# never published or committed. Usage: mkadf.sh BUILD_DIR VERSION OUT.adf
set -euo pipefail
cd "$(dirname "$0")/.."
B=$1 VERSION=$2 OUT=$3
SCRIPTS="full_game_30s.txt playoff_game_30s.txt records_game_30s.txt pause_menus.txt shootout_menus.txt name_entry.txt"
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
# AmigaOS text files: Latin-1 with LF line ends (the markdown's other characters replaced)
python3 -I - CHANGELOG.md "$tmp/Changelog.txt" <<'PY'
import sys
t = open(sys.argv[1], encoding='utf-8').read()
for a, b in (('—', '-'), ('–', '-'), ('→', '->'), ('’', "'"), ('“', '"'), ('”', '"'), ('≤', '<='), ('≥', '>='), ('×', 'x')):
    t = t.replace(a, b)
open(sys.argv[2], 'w', encoding='latin-1', errors='replace').write(t)
PY
rm -f "$OUT"
cmds=(create + format NHL94AGA ffs + boot install
      + write "$B/NHL94AGA" NHL94AGA + write "$B/NHL94VFY" NHL94VFY
      + makedir s + write disk/s/Startup-Sequence s/Startup-Sequence
      + write disk/ReadMe ReadMe + write disk/Credits.txt Credits.txt + write "$tmp/Changelog.txt" Changelog.txt
      + makedir scripts)
for s in $SCRIPTS; do cmds+=(+ write "../tools/oracle/scripts/$s" "scripts/$s"); done
if [ -n "${REFS:-}" ]; then
    cmds+=(+ makedir refs)
    for r in "$REFS"/*.h32; do cmds+=(+ write "$r" "refs/$(basename "$r")"); done
fi
xdftool "$OUT" "${cmds[@]}"
sha256sum "$OUT"
