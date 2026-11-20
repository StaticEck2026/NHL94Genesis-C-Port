# Amiga 1200 AGA port: status

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos,
> StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. The C, its runtime
> (pulseman) and its verification tools are by 1nput ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

The plan, the current state and the record of every state of the native A1200 port. The brief is
[PROMPT.md](PROMPT.md). The ADF log is [CHANGELOG.md](CHANGELOG.md). Read this file before any work, and
update it before and after every unit (PROMPT.md section 9).

States: `PLANNED` → `IN-PROGRESS` → `BUILT` → `ON-HARDWARE` → `PASSED` / `FAILED`, or `BLOCKED`.
Only a log from a real A1200 makes anything `PASSED`.

## Now

| | |
|---|---|
| Current phase | **P0, baseline and audit**: `PLANNED` |
| Next unit | P0.1, check that the host gates pass on this commit (`make VANILLA=1 nhl94`, `make test`, `make lockstep`, `make golden-check`, `tools/check_names.py --check`) and record the output |
| Latest ADF | none yet |
| Blocked on | D-1 to D-8 (owner); a real A1200 and a null-modem cable for P1 onwards |
| Toolchain | not chosen (P1.1) |

## Phases

| Phase | Goal | ADF | State | Exit criteria (summary; full list in PROMPT.md section 10) |
|---|---|---|---|---|
| P0 | Baseline and audit | — | PLANNED | Host gates pass. The ROM read map, HAL call-site map, budget draft and decisions are recorded |
| P1 | Toolchain and boot | 0.1.0 | PLANNED | Boots on KS 3.0 and 3.1, takes the machine and gives it back, `AvailMem` the same |
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

Add one row per unit when it is planned, and keep it after it is done. Attempts count failures (3 means
the owner is asked).

| Unit | Phase | Plan (goal; files; gate) | State | Attempts | Result (gate output, numbers, commit) |
|---|---|---|---|---|---|
| P0.1 | P0 | Host gates on this commit; no files changed; all four gates | PLANNED | 0 | |
| P0.2 | P0 | HAL call-site map: every `hal.h` call by module and purpose (display, sound, input, SRAM, system); `amiga/notes/hal_map.md` | PLANNED | 0 | |
| P0.3 | P0 | ROM read map: every range `rom_ptr()` reads at run time, by scene, from an instrumented host run over all scripts plus a source audit; `amiga/notes/rom_map.md` (ranges and sizes only, never bytes) | PLANNED | 0 | |
| P0.4 | P0 | Video modes the game sets (H32/H40, lines, planes, window, scroll modes, shadow/highlight use) and where; `amiga/notes/video_modes.md` | PLANNED | 0 | |
| P0.5 | P0 | First memory map and frame budget drafts (tables below) from P0.2 to P0.4 | PLANNED | 0 | |

## Decisions

The owner answers. An open decision blocks the work that depends on it. Options and reasons are in
PROMPT.md section 11.

| ID | Question | Recommendation | Answer | Date |
|---|---|---|---|---|
| D-1 | Baseline machine | Stock A1200, 2 MB Chip, no Fast RAM, KS 3.0 and 3.1 | open | |
| D-2 | Region, pacing, tolerance | 60 Hz default, 50 Hz option; beam-locked within 0.05%, else CIA-paced | open | |
| D-3 | Paula instruments | Rendered by the native installer from the game's FM patches | open | |
| D-4 | `mem.h` big-endian fast path | Propose to Abdul as hash-neutral; portable path until then | open | |
| D-5 | Original slowdown speed | Out of scope until E6 passes upstream | open | |
| D-6 | C and Start on a two-button joystick | C on Space or Return, Start on P; CD32 pad recommended | open | |
| D-7 | Boot format | AmigaDOS FFS DD with Startup-Sequence, no trackloader | open | |
| D-8 | Where ADFs are published | GitHub release assets with the owner's OK, never in git | open | |

## Memory map (stock A1200, 2 048 KB Chip RAM, no Fast RAM)

The budgets are drafts until P0.5 and P3 replace them with figures from the code. "Measured" is a
high-water mark from a real A1200's verify-build report.

| Arena | Lifetime | Budget (KB) | Measured (KB) | Notes |
|---|---|---|---|---|
| OS, free on entry minus our total | — | ≥ 64 reserve | | Measured with `AvailMem(MEMF_CHIP)` at start |
| `CODE` | program | TBD (P1) | | The executable's size from the linker map |
| `CORE` | program | TBD (P0.5) | | `struct g` 64, `ram_host_` 128 until D-4, save RAM image, resident ROM tables (P0.3) |
| `DISPLAY` | program | TBD (P0.5) | | Copper lists ×2, playfield ring buffers ×2, overlay planes, sprite data |
| `BLITSRC` | scene | TBD (P0.5) | | Tile cache, BOB images and masks, pristine rink |
| `AUDIO` | scene | TBD (P9) | | PCM, instrument bank, mix buffers |
| `SCENE` | scene | TBD (P0.5) | | Decompressed per-scene data |
| **Total** | | ≤ 2 048 | | |

## Frame budget (one pass, in raster lines, stock A1200)

Targets come from P2's measurements. NTSC has 262 lines a frame and PAL 312. The worst pass must leave
10% of the frame.

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

## ADF index

| Version | Date | Commit | SHA-256 | State | Changelog |
|---|---|---|---|---|---|

## Issues

| ID | Found in | What | State | Fixed in |
|---|---|---|---|---|

## Learned

One line per correction the owner makes, with its date.
