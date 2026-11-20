# Amiga 1200 AGA port: status

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos,
> StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. The C, its runtime
> (pulseman) and its verification tools are by 1nput ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

The plan, the current state and the record of every state of the native A1200 port. The brief is
[PROMPT.md](PROMPT.md). The ADF log is [CHANGELOG.md](CHANGELOG.md). Read this file before any work, and
update it before and after every unit (PROMPT.md section 9).

States: `PLANNED` → `IN-PROGRESS` → `BUILT` → `ON-HARDWARE` → `PASSED` / `FAILED`, or `BLOCKED`.
Only a log from a real A1200 makes anything `PASSED`. A unit with no hardware step (the P0 audits) stays
`BUILT` until the owner has read it.

## Now

| | |
|---|---|
| Current phase | **P1, toolchain and boot**: `ON-HARDWARE`. ADF 0.1.0 is with the owner, with test sheet [tests/v0.1.0.md](tests/v0.1.0.md) |
| Phase P0 | `BUILT` (audits done), except P0.1: `BLOCKED` on the oracle setup and the names gate's asm repository |
| Next unit | Waiting for the 0.1.0 logs. Meanwhile, P2.1 (the serial and file logger, the CIA profiler) can be planned, and P0.1's blocks cleared |
| Latest ADF | **0.1.0** (P1): `nhl94-aga-v0.1.0.adf`. Checksum in the ADF index below |
| Blocked on | Owner: 0.1.0 on a real A1200 (KS 3.0 and 3.1), a null-modem cable for P2, decisions D-1 to D-11. Environment: the oracle (P0.1) |
| Toolchain | GNU GCC 13.3.0 and binutils 2.42 for m68k (`gcc-m68k-linux-gnu`, Ubuntu 24.04), freestanding; `amiga/tools/elf2hunk.py`; amitools 0.9.1 `xdftool`. Decision D-10 |

## Phases

| Phase | Goal | ADF | State | Exit criteria (summary; full list in PROMPT.md section 10) |
|---|---|---|---|---|
| P0 | Baseline and audit | — | BUILT (P0.1 BLOCKED) | Host gates pass. The ROM read map, HAL call-site map, budget draft and decisions are recorded |
| P1 | Toolchain and boot | 0.1.0 | ON-HARDWARE | Boots on KS 3.0 and 3.1, takes the machine and gives it back, `AvailMem` the same |
| P2 | Timing and logging bench | 0.2.0 | PLANNED | Serial and file logs, CIA profiler, measured vblank rates, bus contention table |
| P3 | Memory manager | 0.3.0 | PLANNED | Arenas, pools, guards, high-water report, stress test, A-MEMORY |
| P4 | Native installer | 0.4.0 | PLANNED | ROM check, deterministic data disk, A-LEGAL |
| P5 | Game logic headless (feasibility gate) | 0.5.0 | PLANNED | A-LOGIC on every script. Worst-pass time measured. Go or stop |
| P6 | Display: palette, tiles, front-end | 0.6.0 | PLANNED | A-DISPLAY-INTENT and A-DISPLAY-VISUAL on the front-end |
| P7 | Display: rink, scroll, objects | 0.7.0 | PLANNED | A-DISPLAY on every script. No late pass in a 1P game |
| P8 | Input | 0.8.0 | PLANNED | Joystick, CD32 pad, keyboard, 4 Way Play |
| P9 | Sound | 0.9.0 | PLANNED | A-AUDIO-CMD, A-AUDIO-EAR, no late pass |
| P10 | Save RAM, regions, host menu, data disk flow | 0.10.0 | PLANNED | Saves survive a power cycle. Floppy-only and hard-disk installs work |
| P11 | Speed hardening and soak | 0.11.x | PLANNED | A-SPEED with 10% headroom at 60 Hz and 50 Hz |
| P12 | Release | 1.0.0 | PLANNED | Every gate on the release ADF. Owner sign-off |

## Units

One row per unit when it is planned. Keep it after it is done. Attempts count failures (3 means the owner is
asked).

| Unit | Phase | Plan (goal; files; gate) | State | Attempts | Result (gate output, numbers, commit) |
|---|---|---|---|---|---|
| P0.1 | P0 | Host gates on this commit; no files changed; all gates | BLOCKED | 0 | 2026-11-20 on f1ec3d1, GCC 13.3: `make VANILLA=1 nhl94` and `make nhl94` build with 0 warnings (`-Werror`). `make rulestest`: 71 checks, 0 failed. `make golden` then `golden-check`: the 15 boot-based runs recorded and reproduced, but the 5 that start from `out/states/faceoff.snap` are missing (they need `make vectors`, so the oracle). `make lockstep`: 0 of 23 (no vectors). `make test`: no captures. **Blocked**: `tools/e2_setup.sh` calls `tools/fetch_refs.sh`, `tools/patches/` and `tools/callgraph.py`, which are not in this repository, and the oracle needs the asm and Genesis Plus GX repositories. The names gate needs `../NHL94Genesis` (attaching it was refused in this session). Needs the owner: a machine with the oracle set up, or those repositories allowed |
| P0.2 | P0 | HAL call-site map; `amiga/notes/hal_map.md` | BUILT | 0 | Done. Finding: **the HV counter is the game's random source** (`logic.c` ×10, `title.c` seeds `g.seed`): D-9 |
| P0.3 | P0 | ROM read map; `amiga/tools/romtouch.c`, `rommap.sh`, `romsummary.py`, `amiga/notes/rom_map.md` (page addresses only) | BUILT | 0 | Done. 172 of 256 pages (688 KB) read outside the boot checksum over 7 scripts; at most 180 KB in any second. The cartridge's checksum reads all 1 MB at boot: D-11 |
| P0.4 | P0 | Video modes; `amiga/notes/video_modes.md` | BUILT | 0 | Done. Match and title are H32 (256 px), front-end H40 (320 px), 224 lines, no shadow/highlight. Whole-screen scroll everywhere except the title's per-line H scroll (credits) |
| P0.5 | P0 | Memory map and frame budget drafts (below) | BUILT | 0 | Drafted from P0.2 to P0.4, plus `src/` cross-compiled for the 68020 (all 39 files compile with 0 errors, and `_Static_assert(sizeof(struct Ram) == 0x10000)` holds under the m68k ABI): game code 158 KB without `z80.c`, data 253 KB (`g` 64, `ram_host_` 128, `roster_team` 60) |
| P1.1 | P1 | Toolchain: `amiga/Makefile`, `host/amiga.ld`, `tools/elf2hunk.py`, `tools/mkadf.sh`, `tools/noretail.py` | BUILT | 0 | `make -C amiga check`: cross build with 0 warnings; `hunktool info` reads CODE 5796, DATA 24, BSS 2264 bytes, 203 RELOC32; A-LEGAL passes on the ADF |
| P1.2 | P1 | Boot and takeover program: `host/start.S`, `host/boot.c`, `host/libc.c`, `host/amiga.h`, `disk/`; gate A-FIRMWARE on hardware | ON-HARDWARE | 0 | ADF 0.1.0 built. Disassembly reviewed: no FPU opcodes, no libgcc calls; the l3 vector and the hook are relocated; the copper WAITs only go down the screen (a bug found and fixed before the build). Test sheet [tests/v0.1.0.md](tests/v0.1.0.md) |

## Decisions

The owner answers. An open decision blocks the work that depends on it. Options and reasons are in
PROMPT.md section 11 (D-1 to D-8) and here (D-9 on).

| ID | Question | Recommendation | Answer | Date |
|---|---|---|---|---|
| D-1 | Baseline machine | Stock A1200, 2 MB Chip, no Fast RAM, KS 3.0 and 3.1 | open | |
| D-2 | Region, pacing, tolerance | 60 Hz default, 50 Hz option; beam-locked within 0.05%, else CIA-paced | open | |
| D-3 | Paula instruments | Rendered by the native installer from the game's FM patches | open | |
| D-4 | `mem.h` big-endian fast path | Propose to Abdul as hash-neutral; portable path until then | open | |
| D-5 | Original slowdown speed | Out of scope until E6 passes upstream | open | |
| D-6 | C and Start on a two-button joystick | C on Space or Return, Start on P; CD32 pad recommended | open | |
| D-7 | Boot format | AmigaDOS FFS DD with Startup-Sequence, no trackloader | open (0.1.0 is built this way) | |
| D-8 | Where ADFs are published | GitHub release assets with the owner's OK, never in git | open | |
| D-9 | The HV counter, the game's random source (P0.2) | Answer `vdp_hv16`/`vdp_hv8` with the host build's deterministic function (pulseman's `beam_hv`: the frame's clock from counted accesses, plus 97 a read), ported as a small table-free function in `amiga/host/hal_amiga.c`. It models no hardware the Amiga draws with; it reproduces the reference's random numbers. Without it A-LOGIC cannot pass. Needs the owner's OK under PROMPT.md section 4 | open | |
| D-10 | Toolchain (PROMPT.md 6.2 names bebbo's amiga-gcc or vbcc) | GNU GCC 13 for m68k from the distribution, freestanding, plus the port's `elf2hunk.py`. Neither named toolchain's site could be reached from this environment; this one is pinned by the distribution, and it already compiles all of `src/` for the 68020. Revisit if the hunk converter's limits bite (no cross-hunk PC-relative references) | chosen by the agent for 0.1.0; owner may veto | 2026-11-20 |
| D-11 | The cartridge's boot checksum reads all 1 MB (P0.3) | The installer checks the ROM's CRC32 once. The checksum routine gets the cartridge's stored sum through a hash-neutral host hook, so the whole ROM never needs to be resident. It is a `src/` touch, so it needs Abdul | open | |

## Memory map (stock A1200, 2 048 KB Chip RAM, no Fast RAM)

The budgets are drafts (P0.5), from sizes measured on the host. P3 replaces them with the Amiga build's own
figures. "Measured" is a high-water mark from a real A1200's verify-build report.

| Arena | Lifetime | Budget (KB) | Measured (KB) | Notes |
|---|---|---|---|---|
| OS, free on entry minus our total | — | ≥ 64 reserve | | `AvailMem(MEMF_CHIP)` at start: the 0.1.0 log reports it |
| `CODE` | program | 200 | | Game 158 KB (68020 `-Os`, without `z80.c`) plus the host, an estimate of 40 |
| `CORE` | program | 300 | | `g` 64, `ram_host_` 128 (0 after D-4), `roster_team` 60, save RAM 8, resident ROM tables (P4 splits the 688 KB read set), stacks |
| `DISPLAY` | program | 320 | | Two plane mirrors, 512×256 at 6 planes (96 KB each); a double-buffered screen, 256×224×6 (43 KB each); copper lists 2×8 KB. P7 decides between compositing and dual playfield (see Issues) |
| `BLITSRC` | scene | 400 | | The tile cache: VRAM's 2048 tiles as 6-plane planar is 96 KB, plus flips. BOB images and masks from the sprite art, and the pristine rink |
| `AUDIO` | scene | 200 | | PCM (crowd, organ, speech) plus the instrument bank (D-3) |
| `SCENE` | scene | 300 | | Decompressed per-scene data |
| **Total** | | **1 720 + reserve** | | About 260 KB left for the OS and for error. D-4 gives back 128 |

## Frame budget (one pass, in raster lines, stock A1200)

Targets come from P2's measurements. NTSC has 262 lines a frame and PAL 312. The worst pass must leave 10%
of the frame.

| Item | Budget (lines) | Worst measured | Scene of the worst | Notes |
|---|---|---|---|---|
| Game logic (`src/`) | | | | P5 measures it first |
| Display backend, CPU side | | | | |
| Blitter (overlapped) | | | | Must finish before the next vblank swap |
| Sound driver | | | | |
| Input | | | | |
| Copper list build | | | | |
| Slack (≥ 10%) | | | | |

## Measured hardware facts

Filled in by P2 on the owner's machine. Nominal values are in PROMPT.md section 2.

| Fact | Value | Machine | How measured | Date |
|---|---|---|---|---|
| Vblank rate, PAL 50 Hz | | | | |
| Vblank rate, NTSC 60 Hz | | | | |
| Vblank rate, PAL machine in 60 Hz mode | | | | |
| Error against the Genesis rate (59.923 / 49.701 Hz) | | | | |
| CPU slowdown per bitplane count and `FMODE` | | | | |
| Blitter throughput (words per line), BLTPRI on and off | | | | |

## Reference frames and listening checklist

Chosen in P6 and P9. One row per frame or sound. It is signed off by the owner on the real machine.

| ID | What | Script and pass | Host screenshot or recording | A1200 capture | Signed |
|---|---|---|---|---|---|

## Hardware test log

One row per ADF hand-off. The log files are kept by the owner, out of git, and named here.

| Date | ADF | Machine (model, Kickstart, RAM, expansions) | Display | Controllers | Units tested | Result | Log |
|---|---|---|---|---|---|---|---|
| 2026-11-20 | 0.1.0 handed off | | | | P1.2 | waiting | |

## ADF index

| Version | Date | Commit | SHA-256 | State | Changelog |
|---|---|---|---|---|---|
| 0.1.0 | 2026-11-20 | see the program's `$VER:` | recorded when built from its commit (next row update) | ON-HARDWARE | [0.1.0](CHANGELOG.md) |

## Issues

| ID | Found in | What | State | Fixed in |
|---|---|---|---|---|
| I-1 | P0.5 | Compositing two Genesis planes into one screen with the 16-bit blitter costs about a frame per full-screen copy. P7 needs each plane's palette use per scene (from the display intent stream) to choose between dual playfield (16 colours a plane) and dirty-cell compositing | open | |
| I-2 | P0.1 | The oracle (lockstep, captures, faceoff states) cannot be set up from this repository alone, so A-LOGIC's reference for the faceoff scripts is missing | open | |
| I-3 | P1.1 | The ADF is not byte-reproducible (file system timestamps) | open (known) | |

## Learned

One line per correction the owner makes, or fact the owner supplies, with its date.

* 2026-11-20: the owner supplied Kickstart images for 3.0 (exec 39.47), 3.1 (exec 40.10) and a "4.0". The "4.0"
  file is byte-identical to the 3.1 file (CRC32 `FC24AE0D`), so it is not a separate Kickstart. None of them
  is used: no emulator is run (PROMPT.md rule 2), and Kickstart images never go into git. The A1200 needs
  them as real ROM chips.
* 2026-11-20: PROMPT.md 8.4 amended. A changelog entry cannot hold its own ADF's SHA-256, because the entry
  is on that ADF. The checksum lives in the ADF index here.
* 2026-11-20: the give-back order calls `RethinkDisplay()` after `Permit()`, not before (PROMPT.md 5 lists
  it before). Intuition locks its base in `RethinkDisplay`, so calling it under `Forbid` with the blitter
  owned risks a wait on a task that cannot run. The brief's list is kept, and this note governs.
