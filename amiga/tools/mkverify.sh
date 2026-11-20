#!/usr/bin/env bash
# The PRIVATE P5 test disks, made from your own ROM (never published, never committed; *.adf is ignored):
#   build/amiga/private/nhl94-aga-vX.Y.Z-private-verify.adf  the public disk plus refs/*.h32 (halcheck's work RAM hashes: A-LOGIC)
#   build/amiga/private/nhl94-aga-vX.Y.Z-private-intent.adf  the same with refs/*.i32 (the display requests' hashes:
#                                                            A-DISPLAY-INTENT, run as NHL94VFY <script> INTENT)
#   build/amiga/private/nhl94-rom-a.adf, nhl94-rom-b.adf     the ROM in two halves, volumes NHL94ROMA and
#                                                            NHL94ROMB, for an A1200 with only floppies
# Usage: mkverify.sh ROM BUILD_DIR VERSION  (run from amiga/, by make -C amiga verify-disks ROM=...)
set -euo pipefail
cd "$(dirname "$0")/.."
ROM=$1 B=$2 VERSION=$3
P=../build/amiga/private
mkdir -p "$P"
[ -f ../out/golden/attract.golden ] || make -C .. golden
(cd .. && amiga/tools/halcheck.sh "$ROM")
REFS=../out/amiga/ref tools/mkadf.sh "$B" "$VERSION" "$P/nhl94-aga-v$VERSION-private-verify.adf"
REFS=../out/amiga/ref REFEXT=i32 tools/mkadf.sh "$B" "$VERSION" "$P/nhl94-aga-v$VERSION-private-intent.adf"
tmp=$(mktemp -d)
trap 'rm -r "$tmp"' EXIT
head -c 524288 "$ROM" > "$tmp/nhl94.rom.1"
tail -c +524289 "$ROM" > "$tmp/nhl94.rom.2"
rm -f "$P/nhl94-rom-a.adf" "$P/nhl94-rom-b.adf"
xdftool "$P/nhl94-rom-a.adf" create + format NHL94ROMA ffs + write "$tmp/nhl94.rom.1" nhl94.rom.1
xdftool "$P/nhl94-rom-b.adf" create + format NHL94ROMB ffs + write "$tmp/nhl94.rom.2" nhl94.rom.2
sha256sum "$P"/*.adf
