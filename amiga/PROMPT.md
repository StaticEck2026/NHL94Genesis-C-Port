# Prompt: native Amiga 1200 (AGA) port of NHL '94

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos,
> StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. The C, its runtime
> (pulseman) and its verification tools are by 1nput ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

Give this whole file to the agent that does the port, as its standing brief. The agent keeps
[STATUS.md](STATUS.md) (the plan, the current state and the record of every state) and
[CHANGELOG.md](CHANGELOG.md) (one entry for every ADF). This prompt does not change between ADFs. A change
to it is a decision the owner records in STATUS.md.

---

## 1. Your job

You are porting this repository's game, the hand-written C of EA's 1993 *NHL '94* for the Sega Genesis
(`src/`), to the **Commodore Amiga 1200** as a native AGA program. The result is a bootable Amiga disk
(ADF). It runs on a stock A1200 at the cartridge's game speed, with the cartridge's game logic exactly,
and it never drops a frame.

Three rules come before everything else:

1. **Native means native.** On the Amiga, every cycle goes to the game's own C compiled for the 68020,
   and to drivers that program the A1200's own chips (Alice, Lisa, Paula, the CIAs) to do the job the
   Genesis's chips did. Nothing on the Amiga models a Genesis. Section 4 says exactly what that rules
   out.
2. **Real Amiga hardware is the final test bench.** No emulator (WinUAE, FS-UAE, vAmiga, Amiberry, UAE
   cores) and no FPGA re-implementation (MiSTer Minimig, Vampire, PiStorm) counts as *final* evidence.
   Every state reaches `PASSED` only on a log from a real A1200 (section 9). **Amended 2026-11-20 by the
   owner (D-13):** until a real A1200 is available, a UAE-family emulator set up as a stock A1200 as
   accurately as it allows (cycle-exact 68020 and AGA chipset, 2 MB Chip RAM, no Fast RAM, an A1200
   Kickstart) is *provisional* evidence. It moves a state to `PASSED (emulated)`. A real A1200 must
   confirm every gate before 1.0.0. Nothing emulated ever runs inside the Amiga build itself.
3. **No Overbar.** The Amiga build leaves out Overbar, PulseMan's runtime (`mdrt`), SDL2, the scalers,
   netplay, save states, rewind and the GIF recorder. It is the `VANILLA=1` game (docs/VANILLA.md) with
   an Amiga host around it. The only host controls are Amiga-native ones (section 7.6).

The repository's rules still apply in full: [AGENTS.md](../AGENTS.md), [docs/RULES.md](../docs/RULES.md),
[docs/VANILLA.md](../docs/VANILLA.md). They mean, among other things:

* `src/` is the game. The vanilla game's logic never changes. The Amiga host lives in `amiga/`, the way the
  SDL2 host lives in `native/`. Any change under `src/` must leave every frame hash the same, pass the
  repository's gates, and wait for the owner's (Abdul's) approval first (the "wide" lane). Examples are a
  port hook, or a big-endian fast path in `mem.h`.
* The ROM and anything made from it (art, samples, tables, captures, golden files) never goes into git
  and never goes onto a distributed ADF (section 8.3).
* The credit above goes near the top of every README, into every release's notes, and into every text
  file shipped on an ADF (`Credits.txt` at least). Tell the user you carried it over.

## 2. The target machine: stock Amiga 1200, nothing added

Build for the machine as Commodore shipped it. Do not assume anything more.

| Part | Specification you must respect |
|---|---|
| CPU | Motorola **MC68EC020**, **14.18758 MHz** (PAL) / **14.31818 MHz** (NTSC). 24-bit address bus (16 MB space), 256-byte instruction cache, **no data cache, no FPU, no MMU** |
| Memory | **2 MB Chip RAM** on a 32-bit bus, shared with every DMA channel. **No Fast RAM.** The CPU waits whenever display, blitter, copper, audio or disk DMA owns the bus |
| Chipset | **AGA**: Alice (DMA, copper, blitter), Lisa (display), Paula (audio, disk, serial, interrupts) |
| Display | Up to 8 bitplanes (256 of 16.8 M colours), HAM8, 24-bit palette with two 12-bit halves (`BPLCON3` LOCT), `FMODE` 1×/2×/4× bitplane and sprite fetch, 8 hardware sprites up to 64 pixels wide (16 colours if attached), dual playfield with independent palette banks, programmable beam (`BEAMCON0`) |
| Blitter | The **ECS-speed 16-bit blitter**: AGA did not widen it. 4 channels (A, B, C, D), any minterm, line and fill modes |
| Copper | Unchanged from ECS. `WAIT`/`MOVE`/`SKIP` on the beam, writes to every custom register the danger bit allows |
| Audio | Paula: **4 DMA channels, 8-bit signed PCM**, 6-bit volume (0 to 64), period-set rates (at most about 28.8 kHz with normal DMA), channels 0 and 3 on the left, 1 and 2 on the right, the low-pass filter on the power LED's line (`CIAA` PRA bit 1) |
| Timers | Two **8520 CIAs**, each with timers A and B on the E clock (about 709 379 Hz PAL, 715 909 Hz NTSC) and a 24-bit TOD counter (CIA-B counts lines) |
| Firmware | **Kickstart 3.0 (V39.106)**, which the A1200 shipped with, and **3.1 (V40.068)**. Both must work |
| Storage | Internal **880 KB DD floppy** (80 cylinders, 2 heads, 11 sectors of 512 bytes, MFM). The 2.5" IDE port (Gayle) and the PCMCIA Type II slot are original A1200 hardware. You may read from them, but the game must never need them |
| Input | Two 9-pin ports (Atari-style joystick, Amiga mouse, two-button pads on POTGO, the CD32 pad's serial protocol), the keyboard on CIA-A's serial line with its handshake, and the parallel port (the four-player adapter) |
| Video out | 15 kHz RGB (PAL 50 Hz, NTSC 60 Hz, and NTSC timing on a PAL machine through `BEAMCON0`) |

Fast RAM, an accelerator or a 68030/040/060 may be *present*. The program then runs no differently,
except that it may put code and non-DMA data in Fast RAM (`MEMF_ANY` after `MEMF_CHIP` needs are met).
Every gate is passed on the stock machine, and a run on an expanded machine never replaces a stock run.

## 3. What "100% parity" means here, and how it is proved

Parity has four parts. Each part has a gate that a program checks. "It looks right" never passes a gate.

### 3.1 Game-logic parity (exact)

For the same pad input, the 64 KB of work RAM (`struct g`, `src/ram.h`) after every game-loop pass on
the Amiga must be **byte-identical** to the host build's (`make VANILLA=1`). The host build is itself
checked against the cartridge (`make lockstep`, `make golden-check`).

* Build a **verify build** of the Amiga program. It plays the repository's input scripts
  (`tools/oracle/scripts/*.txt`, converted to a compact pad-stream file) with no one at the controls. It
  sends a CRC32 of work RAM after every pass to the serial port (null-modem to a PC that records it). With
  no cable, it writes the stream to a log file when the run ends. The verify build may run slower than
  real time, because hashing 64 KB costs time. The **speed build** does not hash.
* A host tool (`amiga/tools/`) produces the reference stream from the host build. A second tool compares
  the two streams and names the first pass that differs, then the first field that differs (the names
  from `ram.h`).
* Gate **A-LOGIC**: every script, every pass, identical.

### 3.2 Display parity (exact at the intent level, then by eye)

The Genesis drew what the game asked for: tile uploads (the DMA list, `g.DMAList`), name-table writes,
palette (CRAM) writes, scroll (VSRAM and the scroll table) and the sprite table (`g.Satt`). The
Amiga backend gets the same requests at the same point in the pass. It turns them into Amiga display
state (section 6).

* Gate **A-DISPLAY-INTENT**: the verify build hashes the stream of display requests the backend
  receives each pass. The hash must match the host build's hash of the same requests.
* Gate **A-DISPLAY-VISUAL**: the owner photographs or captures the real A1200's output (RGB-to-capture
  or a camera) on the list of reference frames in STATUS.md, next to the host build's screenshot of the
  same pass. Each frame is signed off in STATUS.md. Pixel positions, colours (the Genesis 3-bit levels
  mapped through the same DAC table the host build uses, written into AGA's 24-bit palette), priority and
  sprite ordering must match. The only differences allowed are the ones the analog path brings.

### 3.3 Speed parity (exact game time, zero slowdown)

The vanilla speed is **Smooth**: one game-loop pass per vblank (docs/VANILLA.md). The Genesis's NTSC
vblank is 53 693 175 / (3420 × 262) ≈ **59.923 Hz**. Its PAL vblank is 53 203 424 / (3420 × 313) ≈
**49.701 Hz**.

* The Amiga runs **exactly** that many passes per second of wall-clock time, for the region chosen
  (D-2). A pass never takes more than its frame. There are **zero late passes**, so there is no
  slowdown, and no pass is skipped to catch up.
* An Amiga beam runs at its own rate. Nominally, NTSC is 3 579 545 / (227.5 × 262) and a PAL machine in
  NTSC mode is 3 546 895 / (227 × 262). Measure the real rate on the owner's machine with a CIA timer.
  Then use the pacing D-2 sets: beam-locked when the measured rate is within tolerance, or paced by
  CIA-B timer A at the Genesis rate, showing the newest finished pass at each vblank. Record the
  measured rates and the error in STATUS.md.
* Input-to-screen latency, counted in passes, equals the cartridge's (the sprites of pass *N* appear
  at the vblank after pass *N*, as the VDP's DMA in vblank showed them).
* Gate **A-SPEED**: a soak run on stock hardware. It is three full games (Classic rules, 5-minute
  periods: one 1P against the CPU, one 2P, one 4 Way Play) plus the worst-case scenes (shootout,
  replay, the goal celebration with the crowd, the playoff tree). It is logged by the speed build's
  counters: passes, late passes (must be 0), and the worst pass's raster lines against its budget,
  which must leave at least **10% headroom**. Measured passes per second must be within the D-2
  tolerance of the Genesis rate.

The cartridge's own slowdown ("Original" speed, `native/clock.c`) is parked upstream (E6,
docs/C_PORTS_HANDOFF.md). It is **out of scope** until it passes there. Never add it, or any other speed,
as the default.

### 3.4 Sound parity (exact at the command level, then by ear)

The game's 68000 sound driver (`src/sound.c`) builds a 33-byte command buffer (key off and on, patch,
frequency and volume per channel) and starts PCM samples. The Genesis's Z80 relay (`src/z80.c`) turned
that into YM2612 and SN76489 writes. The Amiga backend takes the **command buffer and PCM requests** at
the handshake and plays them through Paula natively (section 7.3).

* Gate **A-AUDIO-CMD**: the verify build hashes the command stream per pass. It must match the host's.
* Gate **A-AUDIO-EAR**: the owner plays a listening checklist (every song, every effect, the organ, the
  crowd, the speech samples) on the real A1200, and signs off each item in STATUS.md. Paula cannot make
  FM. "Parity" by ear means the same notes, timing, envelopes and mix decisions, using instruments that
  come from the game's own patches (D-3).

## 4. "No emulation": what is forbidden and what is native

**Forbidden on the Amiga, at run time or install time:**

* Any CPU interpreter, recompiler or JIT for 68000 or Z80 code. The cartridge's code is never run, and
  never even loaded. The game is the C in `src/`.
* Any model of Genesis hardware state rendered per frame. This includes a VRAM, CRAM or VSRAM mirror
  that a rasteriser walks, a VDP command decoder that keeps the VDP's state, and a scanline renderer.
* Any YM2612 or SN76489 model run in real time, and any cycle-timed Z80 relay (`src/z80.c` is a host
  file and is **not** linked into the Amiga build).
* PulseMan (`mdrt`), Genesis Plus GX, or any Genesis core, in any role on the Amiga.
* Emulators or FPGA cores as a test bench (rule 2).

**Native, and expected:**

* The game's C compiled for the 68020. Hand-written 68020 assembly (vasm) for measured hot spots.
* A display backend that takes the game's display requests at the point the game makes them, and
  answers them with Amiga resources at once. A tile upload becomes a planar tile in a Chip RAM tile cache
  (blitter or CPU). A name-table write blits that tile into the playfield bitmap. A palette write becomes
  a copper `MOVE` into the next copper list. Scroll becomes bitplane pointers and `BPLCON1` in the copper
  list. The sprite table becomes blitter objects and hardware sprites. Nothing is re-drawn from a model
  of the Genesis.
* A sound backend that plays the command stream on Paula's four channels with its own voice allocator.
* Converting the user's ROM into Amiga formats **on the Amiga itself**, by a native installer (section
  8.2): decompression with the game's own routines, chunky-to-planar, and PCM sign conversion.
  Converting is data processing, not running the cartridge.

When something is unclear, ask: "Does this code know what a Genesis chip would do, and do it again?" If
yes, it is emulation, and it is out. If it only knows what the game asked for, and asks the Amiga's own
chips to do it, it is native.

## 5. Respecting the firmware: system-legal from boot to exit

* **Boot**: an AmigaDOS bootable DD disk (FFS or OFS, D-7) with a standard bootblock and
  `S:Startup-Sequence`. There is no custom trackloader unless D-7 changes, so the game also installs to a
  hard disk and runs from Workbench or the Shell. An icon (`.info`), `$VER:` version strings in every
  executable (the `Version` command reads them), and `ReadMe`, `Credits.txt` and `Changelog.txt` on the
  disk.
* **Start-up checks**: refuse to run, with a clear message (an `EasyRequest` or a CLI line), when
  `GfxBase->ChipRevBits0` lacks AGA (`GFXF_AA_ALICE | GFXF_AA_LISA`), when the CPU is below a 68020
  (`SysBase->AttnFlags`), when Kickstart is below V39, or when there is not enough Chip RAM for the
  memory map. Never test hardware by poking registers when a library tells you.
* **Taking the machine**: open `graphics.library` V39, `dos.library` and `lowlevel.library` only through
  their LVOs. Never jump to a fixed ROM address. Load everything first. Then `LoadView(NULL)`,
  `WaitTOF()` twice, `OwnBlitter()`/`WaitBlit()`, save `INTENAR`, `DMACONR`, `ADKCONR` and the vectors you
  replace (through the VBR, read with `GetVBR` from supervisor mode, never assumed to be 0), `Forbid()`,
  then your interrupts and copper list. Use the AGA registers (`FMODE`, `BPLCON3`/`BPLCON4`, `BEAMCON0`)
  only after the AGA check, and set every one you depend on, because Kickstart leaves them in an unknown
  state.
* **Giving it back**: restore every register, vector and DMA and interrupt bit you saved. Then
  `LoadView(GfxBase->ActiView)`, `WaitTOF()` twice, `RethinkDisplay()`, `DisownBlitter()`, `Permit()`, free
  every allocation, close the libraries and return to the Shell or Workbench. Gate **A-FIRMWARE** checks
  that `AvailMem(MEMF_CHIP)` and `AvailMem(MEMF_ANY)` are the same before and after a run. It also
  checks that Workbench and the disk drive work afterwards.
* **Caches**: after loading or relocating code, `CacheClearU()`. No self-modifying code.
* **Disk access only with the OS running**: no `trackdisk.device` or `dos.library` call while the
  machine is taken. Scene changes that need data hand the machine back briefly, load through
  `dos.library`, and take it again (section 6.4). Nothing loads during a match.
* **Save RAM**: the cartridge's battery RAM (`src/sram.c`, `sram_read8`/`sram_write16`) is kept in RAM,
  and written to a file (`NHL94Data:NHL94.sram`) through `dos.library` only at safe points with the OS
  given back (after the game writes it, at the next scene change, or at exit). It is never written
  during a match. Write to a temp file and rename it, so a reset never loses records.

## 6. Architecture

### 6.1 Repository layout

```
amiga/
  PROMPT.md  STATUS.md  CHANGELOG.md   this brief, the state ledger, the ADF log
  Makefile                              cross build: make -C amiga (speed), VERIFY=1 (verify build)
  host/       startup, takeover/restore, interrupts, timing, serial log, memory manager
  gfx/        display backend: copper lists, playfield, tile cache, BOBs, sprites, palette
  sfx/        sound backend: Paula driver, voice allocator, instrument bank
  input/      ports, CD32 pad, keyboard, four-player adapter
  install/    the native installer: ROM check, extraction, conversion, data disk
  asm/        vasm 68020 hot paths, each with its C reference and a test that the two agree
  tools/      host-side tools: ADF builder, reference streams, stream compare, ROM-free check
  disk/       ADF contents that are not built: Startup-Sequence, icons, ReadMe, Credits.txt
```

The Amiga build compiles the same `src/` files as `make VANILLA=1` (minus `z80.c`), with
`amiga/host/hal_amiga.c` behind `src/hal.h`, and `native/vanilla/stubs.c`'s role taken by an Amiga
equivalent.

### 6.2 Toolchain

* Pin one cross toolchain and write its exact version into STATUS.md. The choice is either
  `m68k-amigaos-gcc` (bebbo's amiga-gcc, with libnix and no ixemul) or vbcc with vasm and vlink. Use
  `-m68020` (no FPU), optimise for size in cold code and for speed in measured hot code, and turn
  warnings into errors as the repository's `make` does. Use the NDK 3.2 headers or the NDK 3.1 headers
  (both free from their publishers).
* ADFs are built on the host with a disk-image tool (`xdftool` from amitools, or an equivalent). This
  formats an image and installs a bootblock. It is not emulation.
* The Amiga build and the host build come from the same commit. The repository's gates (`make test &&
  make lockstep && make golden-check`, `tools/check_names.py --check`) still pass on every commit that
  touches anything outside `amiga/`.

### 6.3 The big-endian fact

The A1200 is a big-endian 68k, like the Genesis. The host build keeps the 68000's view of RAM through
`ram_host_` (a 128 KB table) and byte-wise `r16`/`r32`. On the Amiga, `struct g` can sit at the 68000's
offsets in the machine's own byte order. Then `ram_host_[i] == i`, the table can go (128 KB of Chip RAM
back), and `r16`/`r32` become single loads. Propose this as a hash-neutral `mem.h` fast path (D-4). Until
Abdul approves it, the Amiga build keeps the portable path, and its cost is in the CPU budget.

### 6.4 Memory management: zero allocations in a match, zero slowdown from the bus

Everything competes for the same 2 MB of Chip RAM and the same bus. Memory is planned the way the frame
is planned: in advance, measured, and enforced.

**Arenas.** At start-up, after the checks, make one `AllocMem` per arena, sized from the memory map in
STATUS.md:

| Arena | Memory | Lifetime | Holds |
|---|---|---|---|
| `CODE` | `MEMF_ANY` (Chip on a stock machine) | program | The executable, loaded by `dos.library` |
| `CORE` | `MEMF_ANY` | program | `struct g` (64 KB), the `ram_host_` table until D-4, the save RAM image, the resident ROM tables the game reads at run time (section 8.2), input and audio state |
| `DISPLAY` | `MEMF_CHIP` | program | Copper lists (double-buffered), the playfield ring buffers, the scoreboard and overlay bitplanes, sprite DMA data. Each buffer is aligned to the fetch width `FMODE` needs (64-bit for 4×) |
| `BLITSRC` | `MEMF_CHIP` | scene | The planar tile cache, BOB images and masks (interleaved planes so one blit moves every plane), the pristine rink for background restore |
| `AUDIO` | `MEMF_CHIP` | scene | PCM samples, the instrument bank, and Paula's double buffers if any channel is mixed |
| `SCENE` | `MEMF_ANY` | scene | Per-scene decompressed data (front-end art, the arena's art, team logos), with a stack (mark and release) allocator |
| `RESERVE` | none | — | The Chip RAM that must stay free for the OS when it is given back (measured, at least 64 KB) |

**Rules.**

1. **No `AllocMem`/`FreeMem` and no `malloc` after the match starts.** Arenas are carved by bump or
   stack allocators with scene marks, and by fixed-size pools (BOB slots, blit-queue nodes, voices). An
   allocation that does not fit is a build-time or load-time failure with the arena's name. It is never a
   run-time fallback.
2. **Scenes.** The game's scenes (boot and EA logo, title, front-end menus, team select, match, period
   intermission and stats, playoffs, credits) map to a few **overlays**: front-end and match. Each
   overlay has a fixed footprint, loaded only at a scene boundary with the OS given back, and only if not
   already resident. The match overlay is resident for the whole match, period breaks and replays
   included.
3. **The memory map is a table in STATUS.md**: every arena's budget, its measured high-water mark, and
   the free Chip RAM that is left. The verify build fills arenas with a pattern, and checks guard words
   at both ends of every arena and pool block. At exit it reports high-water marks and any guard
   damage. Gate **A-MEMORY**: within budget on stock 2 MB, no guard damage, nothing allocated in a
   match, `AvailMem` the same after exit.
4. **The bus.** Display DMA steals bus cycles from the CPU and the blitter. Keep the display as cheap as
   the picture allows. That means the fewest bitplanes that hold the game's colours, `FMODE` 4× fetch so
   bitplanes and sprites take fewer slots per line, and no display DMA in the lines outside the picture.
   Set `BLTPRI` (blitter-nasty) deliberately, per phase of the frame, and measure what it costs the CPU.
   Keep hot code small enough for the 256-byte instruction cache. Long-align every hot structure.
5. **Overlap.** The CPU runs pass *N+1*'s logic while the blitter, driven from the blitter-done
   interrupt by a queue the CPU filled during pass *N*, draws pass *N*'s objects into the back buffer.
   The copper list for the finished buffer is swapped at the vblank. This keeps the cartridge's one-pass
   latency (3.3). The verify build checks that no pass writes a buffer the blitter still reads.

### 6.5 Frame budget

Keep a budget table in STATUS.md, measured on the stock machine in raster lines (CIA and `VHPOSR`
marks, plus colour-bar markers in debug builds). It has one row each for the game logic, the display
backend's CPU side, blitter time, audio, input, copper list build and slack. Do the feasibility spike
(phase P5) **before** any display work. Find the worst pass of game logic alone on a stock A1200, from
Chip RAM, over every script. If it, plus a realistic estimate of the display's CPU share, does not fit
in a frame with 10% to spare, **stop and report to the owner** with the numbers and the options. The
options are D-4, hot-spot assembly, a cheaper display layout, and Fast RAM as a *requirement* (which
changes the target machine, so only the owner can choose it). Never lower the speed, skip passes, or
drop game work quietly.

## 7. The backends, mapped to A1200 hardware

Treat each mapping below as the starting design. Measure it in its phase. If a measurement says
otherwise, replace it with a recorded decision.

### 7.1 Display (Lisa, Alice, copper, blitter)

* **Mode**: low-res, non-interlaced, the Genesis's picture size (H32 is 256 px, H40 is 320 px; find in P0
  which the game sets, where, through `setvideo` and the front-end's register writes, and follow it) ×
  224 lines, centred with `DIWSTRT`/`DIWSTOP` and
  `DDFSTRT`/`DDFSTOP`. NTSC or PAL beam per D-2.
* **Colour**: the Genesis has four 16-colour palette lines. Use **6 bitplanes (64 colours)**: planes 0 to
  3 are the tile's pixel, and planes 4 and 5 are its palette line (constant per tile, so cheap to blit).
  Colour 0 of each line follows the Genesis's transparency rules. Palette writes become copper `MOVE`s to
  `COLORxx` with the two `BPLCON3` LOCT halves, effective at the same pass as on the cartridge. Use the
  host build's DAC table for 3-bit to 8-bit levels.
* **Planes A and B, and the window**: the rink is plane A (a large scrolling map); the scoreboard, clock
  and overlays use plane B or the window. Starting design: the rink is a **ring-buffer playfield** a
  little larger than the screen. Hardware scroll is bitplane pointers per frame plus `BPLCON1` fine
  scroll. Newly exposed rows and columns are blitted from the tile cache, and a copper split is placed
  at the wrap line. The fixed overlays go in a copper-split band, or in dual playfield if the colour
  budget allows. Measure, then decide.
* **Sprites** (up to 80 Genesis sprites, 20 per line): the players, goalies, referee, puck and shadows
  are **blitter objects** (cookie-cut, minterm `$CA`, interleaved planes). They are restored from the
  pristine rink with one blit per object, and drawn in the Genesis's link order, so the overlaps are the
  same. Genesis priority bits are honoured by drawing order plus masks for the high-priority tiles (net
  front, boards). The **8 AGA hardware sprites** (64 px wide with `FMODE`) take what never overlaps the
  playfield's priority problems: the puck and its arrow, the cursor, the clock digits. Choose by
  measurement.
* **Tiles**: a tile upload (`dodmalist`, the DMA list) converts 4-bit chunky Genesis tiles to planar,
  once, into the tile cache, keyed by VRAM tile index. The conversion runs on the CPU with an `asm/` C2P
  routine, or on the blitter. Name-table writes mark cells dirty. Dirty cells are blitted from the
  cache at the end of the pass. Horizontal and vertical flip are pre-made in the cache when they are used.
* **Fades and the crowd**: the game's palette fades are palette writes (copper only). Crowd animation
  is tile uploads (dirty cells only).

### 7.2 Timing (CIAs, vblank, copper interrupts)

* The level 3 interrupt (vblank, copper, blitter-done) is the heartbeat. It runs the cartridge's
  `VBlank` work in the same order relative to the pass as on the Genesis.
* CIA-B timer A paces passes when D-2 picks clock-locked pacing. CIA-A timer B and `VHPOSR` are the
  profiler. Never use a busy loop as a clock.

### 7.3 Sound (Paula)

* **Input**: the 33-byte command buffer and the PCM start, end and rate the game writes for the relay.
  These are read at the handshake point in `src/sound.c` (through the HAL's Z80-RAM writes, captured as
  events), never by running `z80.c`.
* **Voices**: the Genesis has 6 FM channels (the sixth is the PCM channel when the DAC is on) and 3 tone
  channels plus noise. Paula has 4. Write a voice allocator with fixed, documented rules. Starting rule:
  PCM effects and speech get their own channel. Music keeps its lead, bass and drums. Effects take
  channels from the oldest music voice and give them back on key-off. Fold the PSG into the music or
  effect channels. If (and only if) the frame budget shows room, mix two voices into one Paula channel
  in a double-buffered DMA loop. Record the rules and every exception in STATUS.md.
* **Instruments** (D-3): Paula plays samples. Each FM patch the game uses becomes a looped 8-bit sample
  (attack and loop segments) plus an envelope that the driver steps on volume, per vblank. Pitch is
  Paula's period from the game's frequency number.
* **PCM**: the game's PCM samples (crowd, organ, speech) are 8-bit unsigned DAC data played at the rate
  the game sets at `$8E`. They become signed 8-bit in the `AUDIO` arena and play at the matching Paula
  period, with no resampling.
* **Filter**: set the LED filter to a fixed state chosen in the listening test, and restore it at exit.

### 7.4 Input (CIAs, ports, POTGO)

* Read the pads once a pass at the point the game reads them (`pad_read`/`pad_write` on the TH select
  lines map to the Amiga's own port reads, not to an imitation of the Genesis's protocol). Debounce
  nothing the cartridge did not.
* The **CD32 pad** (7 buttons, read by its shift register on the port's lines, natively and with the
  machine taken) maps to the 3-button Genesis pad: A, B, C and Start. A **two-button** joystick uses fire
  1 and fire 2 (POTGO), and the third button and Start follow D-6. The **keyboard** is read from CIA-A's
  serial register with the correct handshake. It is a pad for player 1 or 2, and Esc quits to the OS.
* **4 Way Play** maps to the Amiga's parallel-port four-player adapter (two extra one-button joysticks),
  plus the two 9-pin ports.

### 7.5 Storage (floppy, IDE, PCMCIA)

* The **game disk** (the ADF) holds the program, the installer, the docs and nothing from the ROM.
* The **data disk** (`NHL94Data:`, a disk the installer writes, or a directory on a hard disk) holds the
  converted assets and the save RAM file. Floppy-only play must work: the game disk boots, then asks
  for the data disk when it needs it, with the floppy's motor off before the machine is taken.

### 7.6 The Amiga host menu (instead of Overbar)

A small native screen (drawn by the game's own display backend, or by Intuition before the takeover)
before the title. It offers region and speed (60 Hz NTSC or 50 Hz PAL, per D-2), the controller for each
player, the LED filter, and Quit. It sets nothing inside the game's own menus.

## 8. Data, installation and ADFs

### 8.1 What the game reads from the ROM

`src/` reads the cartridge through `rom_ptr()` (192 call sites) for tables, rosters, compressed art,
samples and data. In phase P0, list every ROM range the game reads at run time, by scene. Use a
host-build instrumentation pass over all the scripts, plus a source audit of the call sites. That list
decides the `CORE` resident set and each overlay's set. The goal is not to keep the whole 1 MB resident.

### 8.2 The installer (native, on the Amiga)

`NHL94Install` runs from the game disk under AmigaDOS. It:

1. Asks for the user's own ROM dump, from any AmigaDOS path: a hard disk, a PCMCIA CompactFlash card,
   `RAM:` or a ROM disk. It checks the size and the CRC32 against the retail cartridge (US/Europe) and
   refuses anything else.
2. Extracts the ranges from 8.1, decompresses with the game's own `decompress.c`, converts tiles to
   planar, PCM to signed 8-bit, and FM patches to instruments (D-3). All of this runs natively on the
   68020, deterministically (the same ROM gives the same bytes on every machine).
3. Writes the asset pack and an index (offset, size, CRC32) to `NHL94Data:`. The pack must fit on one DD
   floppy (880 KB, minus the file system).
4. Verifies everything it wrote, and prints the time it took. It is a one-time step, and it may take
   minutes on a stock machine.

### 8.3 Keeping the ROM out of ADFs and git

* Gate **A-LEGAL**: a host tool scans every ADF before release. It checks that no 32-byte window of the
  retail ROM, and none of its decompressed art or samples, appears in the ADF. The same "no retail"
  idea as `.github/workflows/smoke.yml`, applied to disk images. `Credits.txt` must be present and carry
  the credit verbatim.
* Converted assets, logs that contain RAM dumps, and photos of the screen stay out of git (`.gitignore`).

### 8.4 ADF versions and the changelog

* **Versions**: `MAJOR.MINOR.PATCH`, starting at `0.1.0`. MINOR goes up with each phase milestone, and
  PATCH with fixes inside a phase. `1.0.0` is the first release that passes every gate in section 10.
  Each ADF also carries the commit's short hash.
* **File name**: `nhl94-aga-vX.Y.Z.adf`. The disk label is `NHL94AGA`. The `$VER:` string is
  `$VER: NHL94AGA X.Y.Z (DD.MM.YYYY)`, AmigaOS's date form.
* **Every ADF** gets:
  * an entry in [CHANGELOG.md](CHANGELOG.md) (added, changed, fixed, known issues, gates passed),
    copied onto the disk as `Changelog.txt`. The ADF's SHA-256 goes in STATUS.md's ADF index and in
    the release notes, since an entry on the disk cannot hold its own disk's checksum (amended
    2026-11-20, recorded in STATUS.md);
  * a state record in [STATUS.md](STATUS.md) (section 9);
  * a hardware test record (machine, Kickstart, RAM, monitor, controllers, result, the log file).
* ADFs are built into `build/amiga/` (ignored by git) and published as release assets with the
  changelog entry as their notes. A release needs the owner's OK, and is never pushed upstream without
  its owner's OK. Never commit an ADF or a data disk to git.

## 9. How you work: STATUS.md is the source of truth

1. **Before any work**, read STATUS.md. Start from the state it records, the open decisions, and the
   next unit. Never redo a decided question.
2. **One unit at a time**: one phase step, one lens, a scope line ("these files only"). Before you
   start, write the unit's plan into STATUS.md (goal, files, gate, exit criteria). When it is done,
   record the result: the gate output, measurements, what changed, the commit.
3. **States** a unit or a phase moves through, and nothing skips one:

   `PLANNED` → `IN-PROGRESS` → `BUILT` (cross build and host gates pass) → `ON-HARDWARE` (ADF handed to
   the owner with its test sheet) → `PASSED` (the hardware log meets every exit criterion) or `FAILED`
   (back to `IN-PROGRESS` with the log and the attempt number). `BLOCKED` (waiting on a decision or the
   owner), with the reason.

   You can bring a unit to `BUILT` yourself. Only a log from a real A1200 takes it to `PASSED`. After 3
   failed attempts at the same unit, stop and ask the owner (the advisor rule in AGENTS.md).
4. **Every ADF hand-off** comes with a test sheet: what to boot, what to press, what to look and listen
   for, which log to send back. Keep it short enough to run in one sitting.
5. **Decisions** go in STATUS.md's decision table with the options, your recommendation, the owner's
   answer and the date. Never act on an open decision. Pick the recommended option only where the table
   says it is the default, and say so.
6. **Learned**: every correction the owner makes becomes one line under "Learned" in STATUS.md (and in
   docs/RULES.md if it concerns `src/`).
7. **Commit** after each finished unit, with STATUS.md and (for an ADF) CHANGELOG.md in the same commit.

## 10. Phases (the plan STATUS.md tracks)

| Phase | Goal | ADF | Exit criteria (on a stock A1200) |
|---|---|---|---|
| P0 | Baseline and audit | — | Host gates pass. The ROM read map (8.1), the HAL call-site map, the RAM and ROM budget draft, and the open decisions are in STATUS.md |
| P1 | Toolchain and boot | 0.1.0 | Boots from DF0: on KS 3.0 and 3.1. AGA and CPU checks work. Takes the machine, shows a copper test screen and a raster bar, returns to the OS with `AvailMem` the same. A-FIRMWARE |
| P2 | Timing and logging bench | 0.2.0 | Serial and file logs. The CIA profiler. The measured vblank rates (PAL, NTSC, PAL machine in 60 Hz mode). Chip-bus contention measured per bitplane count and `FMODE`, written into STATUS.md |
| P3 | Memory manager | 0.3.0 | Arenas, pools, scene marks, guards, the high-water report. A stress test passes. A-MEMORY on the draft map |
| P4 | Native installer | 0.4.0 | Verifies the ROM, writes the data disk, deterministic (two machines or two runs give identical packs). A-LEGAL on the game ADF |
| P5 | Game logic, headless (feasibility gate) | 0.5.0 | A-LOGIC on every script. The worst pass's logic time measured. Go or stop decision recorded (6.5) |
| P6 | Display: palette, tiles, front-end screens | 0.6.0 | A-DISPLAY-INTENT on the front-end scripts. A-DISPLAY-VISUAL on the front-end reference frames |
| P7 | Display: the rink, scrolling, objects | 0.7.0 | A-DISPLAY-INTENT on every script. A-DISPLAY-VISUAL on the match reference frames. No late pass in a 1P game |
| P8 | Input: ports, CD32 pad, keyboard, 4 Way Play | 0.8.0 | Every controller path plays a match. The scripted-input path still gives A-LOGIC |
| P9 | Sound | 0.9.0 | A-AUDIO-CMD. A-AUDIO-EAR checklist signed. Still no late pass |
| P10 | Save RAM, region modes, host menu, data disk flow | 0.10.0 | Records and playoffs survive a power cycle. Floppy-only and hard-disk installs both work |
| P11 | Speed hardening and soak | 0.11.x | A-SPEED soak passes with 10% headroom on stock hardware, at 60 Hz and 50 Hz |
| P12 | Release | 1.0.0 | Every gate passes on the release ADF. Credits, ReadMe, Changelog. The owner signs off |

## 11. Decisions the owner makes (recorded in STATUS.md)

| ID | Question | Recommendation |
|---|---|---|
| D-1 | Baseline machine | Stock A1200: 68EC020/14 MHz, 2 MB Chip, no Fast RAM, KS 3.0 and 3.1. Expansions are used if present, never needed |
| D-2 | Region, pacing and its tolerance | 60 Hz output by default (the NTSC cartridge), 50 Hz as a host-menu choice (the PAL cartridge). Beam-locked when the measured vblank is within 0.05% of the Genesis rate, otherwise CIA-paced at the Genesis rate |
| D-3 | Where Paula's instruments come from | Rendered by the native installer from the game's own FM patch data, into looped samples plus envelopes. No real-time FM ever |
| D-4 | `src/mem.h` big-endian fast path (identity layout, drops `ram_host_`) | Propose it as a hash-neutral, wide-lane change for Abdul. Keep the portable path until approved |
| D-5 | "Original" slowdown speed | Out of scope until E6 passes upstream |
| D-6 | The C button and Start on a two-button joystick | C on the space bar or Return, Start on the P key, with CD32 pads recommended for full play |
| D-7 | Boot format | AmigaDOS FFS DD disk with Startup-Sequence (installable to a hard disk). No trackloader |
| D-8 | Where ADFs are published | GitHub release assets on this repository, with the owner's OK. Never in git |

## 12. Never

* Never put an emulator, an interpreter, a Genesis core or a real-time chip model into the Amiga build,
  and never use one as evidence.
* Never change the vanilla game's logic, art, text or speed, and never make a frame hash change without
  Abdul's approval.
* Never ship ROM bytes, or anything made from them, on an ADF, and never commit them.
* Never mark a state `PASSED` without a log from a real A1200.
* Never allocate memory in a match, never load from disk in a match, and never skip or stretch a pass to
  hide slowness.
* Never leave the machine in a state the OS cannot take back.
* Never drop or shorten the credit.
