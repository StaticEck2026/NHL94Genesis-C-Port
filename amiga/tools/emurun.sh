#!/usr/bin/env bash
# Provisional evidence (decision D-13, STATUS.md): run an NHL94AGA disk in FS-UAE configured as a stock
# A1200 as accurately as it allows (68020 at 14 MHz, cycle-exact CPU and chipset, AGA, 2 MB Chip RAM,
# no Fast or slow RAM, 24-bit addressing), headless under Xvfb. A real A1200 still has to confirm
# every result before 1.0.0. The Kickstart image is the owner's and never goes into git.
#
# Usage: emurun.sh KICKSTART ADF SECONDS "COMMAND" OUTDIR [ROMDIR]
#   COMMAND   the Startup-Sequence line to run (e.g. "NHL94AGA 20", "NHL94VFY attract")
#   OUTDIR    receives run.adf (the disk after the run), the logs found on it, shots and fs-uae.log
#   ROMDIR    optional: a directory mounted as volume NHL94ROM: (the cartridge as nhl94.bin), lowest boot priority
set -euo pipefail
KICK=$(realpath "$1") ADF=$(realpath "$2") SECS=$3 CMD=$4 OUT=$5 ROMDIR=${6:-}
mkdir -p "$OUT"
OUT=$(realpath "$OUT")
cp "$ADF" "$OUT/run.adf"
printf 'Echo "emurun: %s"\n%s\nEcho "emurun: done"\n' "$CMD" "$CMD" > "$OUT/startup"
xdftool "$OUT/run.adf" delete s/Startup-Sequence + write "$OUT/startup" s/Startup-Sequence >/dev/null
cfg="$OUT/run.fs-uae"
cat > "$cfg" <<EOF
[fs-uae]
amiga_model = A1200
kickstart_file = $KICK
floppy_drive_0 = $OUT/run.adf
writable_floppy_images = 1
chip_memory = 2048
fast_memory = 0
slow_memory = 0
accuracy = 1
floppy_drive_speed = 100
uae_chipset = aga
uae_cpu_model = 68020
uae_cpu_24bit_addressing = true
uae_cpu_compatible = true
uae_cpu_cycle_exact = true
uae_cycle_exact = true
uae_cpu_multiplier = 4
uae_cpu_speed = real
uae_fastmem_size = 0
uae_bogomem_size = 0
uae_z3mem_size = 0
fullscreen = 0
window_width = 1024
window_height = 768
video_sync = 0
base_dir = $OUT/fs-uae-base
log_file = $OUT/fs-uae.log
EOF
if [ -n "$ROMDIR" ]; then
    printf 'hard_drive_0 = %s\nhard_drive_0_label = NHL94ROM\nhard_drive_0_priority = -128\n' "$(realpath "$ROMDIR")" >> "$cfg"
fi
mkdir -p "$OUT/fs-uae-base"
export SDL_AUDIODRIVER=dummy
display=:$((90 + RANDOM % 9))
Xvfb "$display" -screen 0 1024x768x24 >/dev/null 2>&1 &
xvfb=$!
sleep 2
DISPLAY=$display fs-uae "$cfg" > "$OUT/fs-uae.stdout" 2>&1 &
emu=$!
# screenshots through the run (the test screen at the start, more along the way)
( n=0; for t in 15 30 60 120; do [ "$t" -lt "$SECS" ] || break; sleep $((t - n)); n=$t;
    DISPLAY=$display import -window root "$OUT/shot-${t}s.png" 2>/dev/null || true; done ) &
shots=$!
for ((i = 0; i < SECS; i++)); do kill -0 "$emu" 2>/dev/null || break; sleep 1; done
DISPLAY=$display import -window root "$OUT/shot-end.png" 2>/dev/null || true
kill -TERM "$emu" 2>/dev/null || true
for ((i = 0; i < 15; i++)); do kill -0 "$emu" 2>/dev/null || break; sleep 1; done
kill -KILL "$emu" 2>/dev/null || true
kill "$shots" 2>/dev/null || true
kill "$xvfb" 2>/dev/null || true
wait 2>/dev/null || true
# the logs the programs wrote on the disk
for f in NHL94AGA.log NHL94VFY.log; do
    xdftool "$OUT/run.adf" read "$f" "$OUT/$f" >/dev/null 2>&1 && echo "== $f" && cat "$OUT/$f" || true
done
for f in $(xdftool "$OUT/run.adf" list 2>/dev/null | awk '/\.h32 /{print $1}'); do xdftool "$OUT/run.adf" read "$f" "$OUT/$f" >/dev/null 2>&1 || true; done
