# P0.3: what the game reads from the ROM at run time

Measured with `amiga/tools/rommap.sh ROM` (the `VANILLA=1` host build with `amiga/tools/romtouch.c`, a
page tracker linked in with `--wrap`; `src/` unchanged) over the seven boot-based scripts, in windows of 60
vblanks. Granularity is 4 KB pages, so every figure is an upper bound. No ROM bytes are recorded, only
page addresses. The four scripts that start from an oracle state (`faceoff` and its rule variants) could
not run here (P0.1), so their scenes are covered only where the boot scripts reach them.

| Run (frames) | 1-s windows | Pages read, boot checksum left out | Largest 1-s window |
|---|---|---|---|
| attract (7200) | 120 | 44 (176 KB) | 100 KB |
| full_game (16000) | 267 | 122 (488 KB) | 180 KB |
| playoff_game (15000) | 250 | 141 (564 KB) | 180 KB |
| records_game (15700) | 262 | 150 (600 KB) | 180 KB |
| pause_menus (8600) | 144 | 105 (420 KB) | 172 KB |
| shootout_menus (4800) | 80 | 86 (344 KB) | 160 KB |
| name_entry (3000) | 50 | 53 (212 KB) | 104 KB |
| **Union of all runs** | | **172 of 256 pages (688 KB)** | |

Ranges in the union: `$000000-$001000`, `$005000-$007000`, `$008000-$00A000`, `$010000-$012000`,
`$013000-$014000`, `$016000-$02D000`, `$035000-$037000`, `$045000-$04A000`, `$04B000-$087000`,
`$088000-$090000`, `$091000-$093000`, `$09E000-$0C4000`, `$0C6000-$0C7000`, `$0D1000-$0D3000`,
`$0D4000-$0DA000` (in parts), `$0E6000-$0EB000`, `$0ED000-$0EE000`, `$0EF000-$0F0000`, `$0F3000-$100000`.

Findings:

* **The cartridge's own checksum reads all 1 MB at boot** (`checksum.c`; the first window of every run
  reads all 256 pages). On the Amiga the installer checks the ROM once (CRC32), so this read must be
  answered without the whole ROM resident. That needs a hash-neutral way for the checksum routine to
  get its answer. It goes in P5, with Abdul's approval if it touches `src/`.
* About 336 KB of the cartridge (the 68000 code, among other things) is not read by the C game in these
  runs. Once the faceoff scripts confirm it, it needs no place on the Amiga.
* The match reads up to 600 KB over a game and 180 KB in any one second. Much of it is compressed art the
  game decompresses into VRAM (`decompress.c`). The Amiga keeps the **converted** form (planar tiles,
  BOB images), not the raw bytes. So the resident set is the decompressed working set per scene, which
  the P6 and P7 intent streams will measure.
* Next step (P4, the installer): split the union into tables (resident in `CORE`), art (converted per
  overlay) and PCM (the `AUDIO` arena), using the asm rebuild's symbols (`src/symbols_gen.h`).
