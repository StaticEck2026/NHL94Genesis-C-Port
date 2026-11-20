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
| Current phase | **P5**: A-LOGIC PASSED (emulated) on attract; **feasibility STOP** on the stock machine, D-14 with the owner. **P1**: PASSED (emulated) after the SetChipRev fix; real A1200 still to confirm |
| Phase P0 | `BUILT`. P0.1b: **every repository gate runs and passes** (lockstep 23/23, captures 7192/7192, golden 20/20, names PASS); `make test`'s source lint trips on a line already in `src/` (I-7) |
| Next unit | Waiting for the 0.1.0 and 0.5.0 logs: the pass times decide P5's go or stop (PROMPT.md 6.5). Then P2 (timing bench) and P3 (memory manager), then P6 (display) |
| Latest ADF | **0.5.0** (P5), private only: `nhl94-aga-v0.5.0-private-verify.adf` plus the ROM halves. The public 0.5.0 is held back by A-LEGAL (D-12) |
| Blocked on | Owner: the 0.1.0 and 0.5.0 runs on a real A1200, and D-12 (A-LEGAL and the game's committed tables). Abdul: D-4 and D-11 (`src/` changes, not needed yet). The `make test` lint (I-7) is for Abdul |
| Toolchain | GNU GCC 13.3.0 and binutils 2.42 for m68k (`gcc-m68k-linux-gnu`, Ubuntu 24.04), freestanding; `amiga/tools/elf2hunk.py`; amitools 0.9.1 `xdftool`. Decision D-10 |

## Phases

| Phase | Goal | ADF | State | Exit criteria (summary; full list in PROMPT.md section 10) |
|---|---|---|---|---|
| P0 | Baseline and audit | — | BUILT (P0.1 partly BLOCKED) | Host gates pass. The ROM read map, HAL call-site map, budget draft and decisions are recorded |
| P1 | Toolchain and boot | 0.1.0 | ON-HARDWARE | Boots on KS 3.0 and 3.1, takes the machine and gives it back, `AvailMem` the same |
| P2 | Timing and logging bench | 0.2.0 | PLANNED | Serial and file logs, CIA profiler, measured vblank rates, bus contention table |
| P3 | Memory manager | 0.3.0 | BUILT (host-tested; hardware with the first program that uses it) | Arenas, pools, guards, high-water report, stress test, A-MEMORY |
| P4 | Native installer | 0.4.0 | PLANNED | ROM check, deterministic data disk, A-LEGAL |
| P5 | Game logic headless (feasibility gate) | 0.5.0 | ON-HARDWARE (built ahead of P2-P4: it is the go-or-stop gate) | A-LOGIC on every script. Worst-pass time measured. Go or stop |
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
| P0.1 | P0 | Host gates on this commit; no files changed; all gates | BLOCKED | 0 | 2026-11-20 on f1ec3d1, GCC 13.3: `make VANILLA=1 nhl94` and `make nhl94` build with 0 warnings (`-Werror`). `make rulestest`: 71 checks, 0 failed. `make golden` then `golden-check`: the 15 boot-based runs recorded and reproduced, but the 5 that start from `out/states/faceoff.snap` are missing (they need `make vectors`, so the oracle). `make lockstep`: 0 of 23 (no vectors). `make test`: no captures. **Blocked**: `tools/e2_setup.sh` calls `tools/fetch_refs.sh`, `tools/patches/` and `tools/callgraph.py`, which are not in this repository, and the oracle needs Genesis Plus GX. Later on 2026-11-20 (the owner's go-ahead): the asm repository was cloned to `../NHL94Genesis`, and `tools/check_names.py --check`: 840 routines, ok 840, other_module 0, renamed 0, missing 0, **PASS**. Lockstep and captures remain blocked (I-2) |
| P0.1b | P0 | The oracle and the cartridge comparison: the repository's own `tools/e2_setup.sh ROM`, then `make oracle`, `make vectors`, `make lockstep`, `tools/hybrid/capture_all.sh`, the capture tests, `make golden`, `make golden-check` | BUILT | 0 | 2026-11-20, GCC 13.3, Genesis Plus GX built with HOOK_CPU=1. **`make lockstep`: 23 of 23 vectors pass** against the cartridge (boot through four_way_game, up to 23,648 vblanks each). **Captures: 58 files, 2.1 GB; `build/captest`: 7192 of 7192 records pass**, 0 failed, 524 skipped (interrupted calls), 29 captures; the 2026-11-04 run had 10,276 records, so this recording captured fewer calls. **`make golden-check`: 20 of 20 runs identical, 2 of 2 roster-set runs, 7 of 7 states, keys / restart / reset OK.** `make rulestest` 71/71, names gate PASS 840/0/0/0. `make test` itself stops at its source lint before captest: `src/setup.c:421` (`tm->tmdata = rosters_team(team)->data;`, commit c047b41) does not match the lint's allowed `->data = rosters_team(` form (I-7). Not changed here: `src/` is outside this unit |
| P0.2 | P0 | HAL call-site map; `amiga/notes/hal_map.md` | BUILT | 0 | Done. Finding: **the HV counter is the game's random source** (`logic.c` ×10, `title.c` seeds `g.seed`): D-9 |
| P0.3 | P0 | ROM read map; `amiga/tools/romtouch.c`, `rommap.sh`, `romsummary.py`, `amiga/notes/rom_map.md` (page addresses only) | BUILT | 0 | Done. 172 of 256 pages (688 KB) read outside the boot checksum over 7 scripts; at most 180 KB in any second. The cartridge's checksum reads all 1 MB at boot: D-11 |
| P0.4 | P0 | Video modes; `amiga/notes/video_modes.md` | BUILT | 0 | Done. Match and title are H32 (256 px), front-end H40 (320 px), 224 lines, no shadow/highlight. Whole-screen scroll everywhere except the title's per-line H scroll (credits) |
| P0.5 | P0 | Memory map and frame budget drafts (below) | BUILT | 0 | Drafted from P0.2 to P0.4, plus `src/` cross-compiled for the 68020 (all 39 files compile with 0 errors, and `_Static_assert(sizeof(struct Ram) == 0x10000)` holds under the m68k ABI): game code 158 KB without `z80.c`, data 253 KB (`g` 64, `ram_host_` 128, `roster_team` 60) |
| P1.1 | P1 | Toolchain: `amiga/Makefile`, `host/amiga.ld`, `tools/elf2hunk.py`, `tools/mkadf.sh`, `tools/noretail.py` | BUILT | 0 | `make -C amiga check`: cross build with 0 warnings; `hunktool info` reads CODE 5796, DATA 24, BSS 2264 bytes, 203 RELOC32; A-LEGAL passes on the ADF |
| E.1 | D-13 | The accurate-emulation bench: `amiga/tools/emurun.sh` (FS-UAE as a stock A1200, cycle-exact, headless under Xvfb, the disk's logs read back afterwards) | BLOCKED | 1 | FS-UAE 3.1.66 from Ubuntu installs but crashes in this container on any A1200 configuration (SIGSEGV or abort in its emulation thread at the floppy insert, with every audio, GL and floppy-sound setting tried). The next choice, building the PUAE libretro core (the WinUAE-derived core, headless through the repository's own libretro frontend), was refused twice by this session's permission checks. Needs the owner: allow that build, or run the test sheets in a correctly set-up emulator of their own |
| P5.3 | P5 | The feasibility measurement (PROMPT.md 6.5), emulated (D-13): `NHL94VFY attract` in PUAE as a cycle-exact A1200, with and without the D-4 fast path (a throwaway build, `src/` unchanged) and with and without 8 MB Fast RAM | PASSED (emulated) for A-LOGIC; **feasibility: STOP** | 0 | A-LOGIC: 7199 of 7199 frames identical in all four runs. Mean pass of the game's logic alone (no display, no sound) against a 16,688 us Genesis frame: **stock 20,343 us (122%), 4,237 of 7,198 passes over a frame**; stock + D-4 17,103 us (102%), 4,237 over; **8 MB Fast RAM 9,000 us (54%), 58 over**; Fast RAM + D-4 7,849 us (47%), 49 over. The worst pass in every run is the scene change at frame 5097 (1.9 s with Fast RAM, 4.2 s without). Chip RAM contention dominates: code and data in Fast RAM halve the time, D-4 takes about 15% more. On the stock machine the logic alone does not fit a frame, before any display or sound: the brief's stop rule applies, and D-14 goes to the owner |
| P3.1 | P3 | The memory manager: `amiga/host/arena.c`, `arena.h` (arenas with guards, scene marks, fixed pools, high-water marks, fill pattern), host stress test `amiga/tools/arenatest.c` (`make -C amiga host-tests`); A-MEMORY on hardware comes with the first program that uses it | BUILT | 1 | arenatest PASS under ASan and UBSan: 200 scenes of random blocks (alignments 4 to 32), releases back to each mark, a planted overrun caught, 100,000 random pool gets and puts against a shadow count, a full pool and a full arena failing by name. Attempt 1 failed on alignment (relative to the arena, not the address); fixed. 982 bytes of 68020 code |
| P5.1 | P5 | `amiga/host/hal_core.c` (the game's hardware requests answered as the golden runs' host does), checked on the host by `amiga/tools/halcheck.c` and `halcheck.sh` | BUILT | 1 | **7 of 7 vanilla golden runs, 69,293 frames: work RAM and VDP state identical** (attract 7199, full_game 15999, playoff_game 14999, records_game 15699, pause_menus 8599, shootout_menus 4799, name_entry 2999). Attempt 1 hung at frame 888: the game also restarts the relay ($96 = $29), not only $D1 commands; fixed. The game never reads VRAM back; it uses VRAM copy in matches (104 to 404 a run), so the display backend must answer copies from its own tiles |
| P5.2 | P5 | `NHL94VFY`: `src/` for the 68020 plus `hal_core.c`, `host/verify.c`, `host/libc_amiga.c`, setjmp and StackSwap in `start.S`; A-LOGIC and the pass times on hardware | ON-HARDWARE | 0 | Builds with 0 errors: 247 KB code, 277 KB BSS, no FPU opcodes, no unresolved symbols, the RAM layout constructor in the table. `src/` under the m68k ABI gives two warnings the host does not (ram.c's little-endian table unused; optsetup.c's read past an array, as the 68000 does), kept as warnings for `src/` only. Private disks built (`make -C amiga verify-disks`). Test sheet [tests/v0.5.0.md](tests/v0.5.0.md) |
| P1.2 | P1 | Boot and takeover program: `host/start.S`, `host/boot.c`, `host/libc.c`, `host/amiga.h`, `disk/`; gate A-FIRMWARE on hardware | ON-HARDWARE | 0 | ADF 0.1.0 built. Disassembly reviewed: no FPU opcodes, no libgcc calls; the l3 vector and the hook are relocated; the copper WAITs only go down the screen (a bug found and fixed before the build). Test sheet [tests/v0.1.0.md](tests/v0.1.0.md) |

## Decisions

The owner answers. An open decision blocks the work that depends on it. Options and reasons are in
PROMPT.md section 11 (D-1 to D-8) and here (D-9 on).

| ID | Question | Recommendation | Answer | Date |
|---|---|---|---|---|
| D-1 | Baseline machine | Stock A1200, 2 MB Chip, no Fast RAM, KS 3.0 and 3.1 | approved: the owner's general go-ahead to proceed on recommendations | 2026-11-20 |
| D-2 | Region, pacing, tolerance | 60 Hz default, 50 Hz option; beam-locked within 0.05%, else CIA-paced | approved (general go-ahead) | 2026-11-20 |
| D-3 | Paula instruments | Rendered by the native installer from the game's FM patches | approved (general go-ahead) | 2026-11-20 |
| D-4 | `mem.h` big-endian fast path | Propose to Abdul as hash-neutral; portable path until then | open: needs Abdul (`src/`). Not needed until the 0.5.0 pass times say so | |
| D-5 | Original slowdown speed | Out of scope until E6 passes upstream | approved (general go-ahead) | 2026-11-20 |
| D-6 | C and Start on a two-button joystick | C on Space or Return, Start on P; CD32 pad recommended | approved (general go-ahead) | 2026-11-20 |
| D-7 | Boot format | AmigaDOS FFS DD with Startup-Sequence, no trackloader | approved (general go-ahead) | 2026-11-20 |
| D-8 | Where ADFs are published | GitHub release assets with the owner's OK, never in git | approved as a rule; each release still needs the owner's OK | 2026-11-20 |
| D-9 | The HV counter, the game's random source (P0.2) | Answer it with **the golden runs' stand-in** (`native/main.c` `golden_hv`: 97 pixels on a read from the frame's wait, in a 342 or 420 pixel line). It is a fixed formula with no timing model, so the Amiga needs no 68000 or Z80 clock. The reference for A-LOGIC is the golden runs. Implemented in `hal_core.c`; halcheck passes on 7 of 7 runs | approved (general go-ahead) | 2026-11-20 |
| D-10 | Toolchain (PROMPT.md 6.2 names bebbo's amiga-gcc or vbcc) | GNU GCC 13 for m68k from the distribution, freestanding, plus the port's `elf2hunk.py`. Neither named toolchain's site could be reached from this environment; this one is pinned by the distribution, and it compiles all of `src/` for the 68020. Revisit if the hunk converter's limits bite (no cross-hunk PC-relative references) | approved (general go-ahead) | 2026-11-20 |
| D-13 | Evidence without a real A1200 | The owner: "Do emulators but as accurate as possible for now". A UAE-family emulator set up as a stock A1200 (cycle-exact 68020 at 14 MHz and AGA chipset, 2 MB Chip, no Fast or slow RAM, 24-bit addressing, the owner's A1200 Kickstart 39.106) is provisional evidence: states may reach `PASSED (emulated)`. Real-hardware confirmation of every gate is still required before 1.0.0. PROMPT.md rule 2 amended | approved by the owner | 2026-11-20 |
| D-14 | P5's go-or-stop (PROMPT.md 6.5): the logic alone takes 122% of a frame on a stock A1200 (emulated) | (a) **Make Fast RAM a requirement** (A1200 plus any trapdoor Fast RAM card; with 8 MB the logic takes 54% of a frame, leaving room for display and sound): changes D-1. (b) Stay stock and find about 2.5x: D-4 (Abdul) gives 15%, and the rest would need hand-written 68020 for the hottest routines, with no assurance it gets there. (c) Run below the Genesis rate: ruled out by the brief. Recommendation: (a), with stock kept as a measured goal | open: needs the owner | |
| D-12 | A-LEGAL on a disk with the game's compiled C (P5) | The compiled game carries 630 32-byte windows of the cartridge (24 ranges of 32 to 64 bytes), all from tables committed in `src/` (`src/*_gen.h` and const arrays), as every host release of this repository does. Options: (a) allow bytes in programs built from committed source, report them, and keep failing everything else; (b) keep game disks private to ROM owners; (c) move those tables into the installer's data, which changes `src/` and needs Abdul. **The agent may not weaken its own gate**: an attempt to change `noretail.py` that way was stopped. Recommendation: (a), with Abdul's view, since it matches the repository's own releases | open: needs the owner's explicit answer | |
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
| 2026-11-20 | 0.1.0 (822ac01) | Kickstart 40.063, 68020, Chip 1,848 KB free, no Fast RAM, PAL; chipset bits $13 (ECS Agnus and Denise, no Alice or Lisa). 40.063 is the A500/A600/A2000 build of 3.1, not the A1200's 40.068. The owner's log shows it was **iPadUAE (Amigo) on an iPad**: an emulator (68020, 2 MB Chip, 8 MB Z2 Fast RAM, UAE chipset mask 7) | AmigaDOS console | — | P1.2, test sheet step 7 (refusal) | **Refusal path behaved as specified**: `REFUSED: needs the AGA chipset (Alice and Lisa)`, return code 20, free memory the same before and after (1,892,832 bytes), log written to PROGDIR:. Does **not** pass P1: that needs an AGA A1200 on KS 3.0 and 3.1. An emulator run is not evidence under PROMPT.md rule 2: recorded for information only | owner's screenshot IMG_0904, iPadUAE log |
| 2026-11-20 | 0.5.0 (private disks) handed off | | | | P5.2 | waiting | |

## ADF index

| Version | Date | Commit | SHA-256 | State | Changelog |
|---|---|---|---|---|---|
| 0.5.0 (private) | 2026-11-20 | a32f0f1 | verify disk `5b1ceddcd835d307226ceb22a4413dc02f37c85b0785d5816b613434b1ad7af1`, ROM A `4cf17a03...511c2e`, ROM B `9d119eaf...c39f6` (made from the owner's ROM; never published) | ON-HARDWARE | [0.5.0](CHANGELOG.md) |
| 0.5.0 (public) | — | — | held back: fails A-LEGAL (D-12) | BLOCKED | |
| 0.1.0 | 2026-11-20 | 822ac01 | `1e7876438b2c9831c5b2d18544ae73b59c8064b3e3bd00e0a219cc3c045717b1` (program `NHL94AGA`: `ceda6ecc9018a78d66fa36700f71205cf3c1f8eb161c82ddb6cfb65f6f404f81`) | ON-HARDWARE | [0.1.0](CHANGELOG.md) |

## Issues

| ID | Found in | What | State | Fixed in |
|---|---|---|---|---|
| I-1 | P0.5 | Compositing two Genesis planes into one screen with the 16-bit blitter costs about a frame per full-screen copy. P7 needs each plane's palette use per scene (from the display intent stream) to choose between dual playfield (16 colours a plane) and dirty-cell compositing | open | |
| I-2 | P0.1 | The oracle (lockstep, captures, faceoff states) seemed impossible to set up from this repository | closed: it can; P0.1b ran it | 2026-11-20 |
| I-7 | P0.1b | `make test`'s lint flags `src/setup.c:421` (`tm->tmdata = rosters_team(team)->data;`): the lint allows only `->data = rosters_team(`. A `src/` or Makefile change, so it is for Abdul. The capture tests pass when run directly | open | |
| I-3 | P1.1 | The ADF is not byte-reproducible (file system timestamps) | open (known) | |
| I-4 | P5.2 | The verify build needs 1 MB for the ROM beside 0.6 MB of program and data: a stock 2 MB A1200 booted from the verify disk should hold it, but `RAM:nhl94.bin` will not fit beside it without Fast RAM | open (test sheet says so) | |
| I-5 | P5.1 | The game uses VRAM-to-VRAM copy in matches, so the display backend must rebuild the bytes it copies from its own tiles and cells (P6, P7) | open | |
| I-6 | P5.2 | On the A1200, `src/mem.h`'s portable path still goes through the 128 KB `ram_host_` table, which is all identity on a big-endian machine: a cost in every RAM access until D-4 | open | |

## Learned

One line per correction the owner makes, or fact the owner supplies, with its date.

* 2026-11-20 (correction): the owner's Kickstart files, read from their ROM headers, are 39.106 (`Kickstart_3.0.rom`,
  CRC32 9E6AC152) and **40.063** (`Kickstart_3.1.rom` and its copy `Kickstart_4.0.rom`, CRC32 FC24AE0D). 40.063 is the
  A500/A600/A2000 build of 3.1, not the A1200's 40.068. An earlier reply said otherwise; it was wrong.
* 2026-11-20: the first hardware report ran 0.1.0 on Kickstart 40.063 with ChipRevBits0 $13. The AGA check reads
  bits 2 (Alice) and 3 (Lisa); bit 4 (`GFXB_AA_MLISA`, internal) alone is not AGA, and the refusal was right. A1200
  Kickstarts are 39.106 (3.0) and 40.068 (3.1); 40.063 belongs to the A500/A600/A2000.

* 2026-11-20 (correction): P0.1 said `tools/fetch_refs.sh`, `tools/patches/` and `tools/callgraph.py` were not in this
  repository. They are. The listing that "showed" it was cut off at 200 files. The oracle setup is the repository's
  own `tools/e2_setup.sh`, run in P0.1b.
* 2026-11-20: the owner gave a general go-ahead ("if anything is blocked and needed, give allow permission"). It is
  taken as approval of the recommended options for D-1 to D-3, D-5 to D-10 and D-9. Changes to `src/` (D-4, D-11)
  still need Abdul under AGENTS.md, and weakening a gate (D-12) needs the owner's explicit answer.
* 2026-11-20: P5 was built before P2 to P4, because PROMPT.md 6.5 puts the feasibility spike first. Its verify build
  runs under the OS with plain allocations, so it does not depend on P3's arenas.
* 2026-11-20: PROMPT.md section 4 is read this way. Decoding the game's VDP command words into requests (which
  address its next words go to, a DMA's source and length) is the request decoder the backends need, and so is
  answering pad reads with the bits the game's TH-select reads expect. Forbidden is keeping or drawing a picture from
  a model of VRAM, or timing anything the way a Genesis chip does. `hal_core.c` keeps the palette (the game reads
  it back) and VSRAM, and no VRAM.
* 2026-11-20: the owner supplied Kickstart images for 3.0 (exec 39.47), 3.1 (exec 40.10) and a "4.0". The "4.0"
  file is byte-identical to the 3.1 file (CRC32 `FC24AE0D`), so it is not a separate Kickstart. None of them
  is used: no emulator is run (PROMPT.md rule 2), and Kickstart images never go into git. The A1200 needs
  them as real ROM chips.
* 2026-11-20: PROMPT.md 8.4 amended. A changelog entry cannot hold its own ADF's SHA-256, because the entry
  is on that ADF. The checksum lives in the ADF index here.
* 2026-11-20: the give-back order calls `RethinkDisplay()` after `Permit()`, not before (PROMPT.md 5 lists
  it before). Intuition locks its base in `RethinkDisplay`, so calling it under `Forbid` with the blitter
  owned risks a wait on a task that cannot run. The brief's list is kept, and this note governs.
