#!/usr/bin/env bash
# Build a bootable NHL94AGA ADF (880 KB DD, FFS, label NHL94AGA, a standard bootblock) from the
# program, amiga/disk/ and amiga/CHANGELOG.md (as Changelog.txt). No ROM bytes go on it: the
# A-LEGAL gate (amiga/tools/noretail.py) checks. Usage: mkadf.sh PROGRAM VERSION OUT.adf
set -euo pipefail
cd "$(dirname "$0")/.."
PROG=$1 VERSION=$2 OUT=$3
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
# AmigaOS text files: Latin-1 with LF line ends (the markdown's non-ASCII characters are replaced)
python3 -I - CHANGELOG.md "$tmp/Changelog.txt" <<'PY'
import sys
t = open(sys.argv[1], encoding='utf-8').read()
for a, b in (('—', '-'), ('–', '-'), ('→', '->'), ('’', "'"), ('“', '"'), ('”', '"'), ('≤', '<='), ('≥', '>='), ('×', 'x')):
    t = t.replace(a, b)
open(sys.argv[2], 'w', encoding='latin-1', errors='replace').write(t)
PY
rm -f "$OUT"
xdftool "$OUT" create + format NHL94AGA ffs + boot install \
    + write "$PROG" NHL94AGA + makedir s + write disk/s/Startup-Sequence s/Startup-Sequence \
    + write disk/ReadMe ReadMe + write disk/Credits.txt Credits.txt + write "$tmp/Changelog.txt" Changelog.txt
xdftool "$OUT" protect NHL94AGA rwed
sha256sum "$OUT"
