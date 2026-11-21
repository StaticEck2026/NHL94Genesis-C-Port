# NHL94AGA changelog

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos,
> StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. The C, its runtime
> (pulseman) and its verification tools are by 1nput ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

Every ADF of the native Amiga 1200 AGA port gets an entry here. The entry is copied onto the disk as
`Changelog.txt`, credit included. Versions are `MAJOR.MINOR.PATCH` (PROMPT.md section 8.4). The ADF is
`nhl94-aga-vX.Y.Z.adf` and its `$VER:` is `NHL94AGA X.Y.Z (DD.MM.YYYY)`. ADFs are release assets and
never go into git. No ADF holds any byte of the ROM: the game needs your own cartridge dump, which the
native installer converts on your Amiga.

An entry cannot carry its own ADF's SHA-256, because the entry is on that ADF. The checksums are in
STATUS.md's ADF index and in the release notes.

Entry template (newest first):

```
## X.Y.Z (YYYY-MM-DD), phase Pn

ADF: nhl94-aga-vX.Y.Z.adf (its commit is in the program's version string: Version NHL94AGA)

Added:
Changed:
Fixed:
Known issues:
Gates passed on a stock A1200 (machine, Kickstart): A-...
Tested by, on: <date>, <log name>
```

## 0.7.1 (2026-11-21), phases P9 and P7: the sound fixed, faster matches

ADF: `nhl94-aga-v0.7.1.adf` (public; it boots the host menu, then the game). Test sheet: amiga/tests/v0.7.0.md
(the same items; listen to the music and the crowd again).

Fixed:
* The music and the crowd: the crowd, the organ and speech never stopped when the game stopped them
  and could crawl at a very low pitch, which made a loud rumble under everything (87% of the sound's
  energy was below 20 Hz in an emulated run; now 0.2%, the cartridge's 0.3%). The PCM now follows the
  game's rate the way the cartridge's sound chip does. Eight of the instruments' rendered waveforms
  were off centre, which made a thump at every note; they are centred now.
* When your cartridge dump is not found, the program now says where it looked (NHL94ROM:, beside the
  program, RAM: as `nhl94.bin`) before it asks for the two ROM floppies.

Changed:
* Matches draw faster: sprites showing the same frame share one image, the rink's new rows and plane
  A's changes take fewer blits, and the display backend skips the parts of the game's video uploads
  that did not change. The title's logic went from 18 ms to 4 ms a pass.

Known issues:
* A match in active play is still about twice too slow for a frame on an A1200 with Fast RAM (I-9):
  the next work. Stoppages, menus and the title fit far more often.
* The sound is still the first version otherwise: an instrument's tone does not change over a note,
  Paula has four voices for the Genesis's ten (notes are dropped when more play), and each voice is on
  one side (Paula's left or right) where the cartridge is mono.
* All of it is from emulated A1200s (D-13).

## 0.7.0 (2026-11-21), phases P6, P7 and P10: speed, the Genesis's own pace, the host menu

ADFs: `nhl94-aga-v0.7.0.adf` (public; it boots the host menu, then the game). For the owner of the ROM,
made by `make -C amiga verify-disks` and never published: the verify, intent, show and audio check disks
and `nhl94-rom-a.adf`, `nhl94-rom-b.adf`. Test sheet: amiga/tests/v0.7.0.md.

Added:
* The host menu before the title: 60 Hz (NTSC) or 50 Hz (PAL), which joystick port is player 1, which
  player the keyboard is, the 4 Way Play adapter on or off, the audio filter (and power LED) on or off,
  Start and Quit. Your choices are kept in `NHL94.prefs` beside the program.
* The Genesis's own pace: the game runs at the cartridge's 59.923 passes a second (49.702 on PAL), timed
  by CIA-B timer A, not at the Amiga's screen rate (which is 0.2% to 1% different on every A1200). The
  timer is asked for from the system and given back when you quit.
* When a picture cannot be drawn in its frame, the game keeps its time and that one picture is left out
  (D-18): the game itself never slows down for the drawing. The checks still draw every picture.

Changed:
* Matches draw much faster: a match pass's drawing went from about 33 ms to about 10 ms on average
  (emulated A1200 with Fast RAM). The players partly off the left of the screen, the score boxes and the
  clock no longer make the whole overlay draw again.
* The title and its scrolling credits draw only what moved (I-8): its passes went from about 30 ms to
  about 14 ms on average; its game logic from 18 ms to 4 ms a pass.

Fixed:
* A timing error that could make one pass read as 4 seconds in the logs (the vertical blank arrives a
  few lines before line 0 on the emulated A1200).

Known issues:
* Not every pass is drawn in its frame yet: about one match pass in eight, the title while its credits
  scroll, and the in-match roster screen while its background moves (that one is limited by the
  blitter itself). In those, a picture is left out; the game's speed is unaffected.
* Screen changes (a fade to black, then the new screen) still take up to 3.5 seconds.
* The sound is as in 0.6.0 (mono, fixed instrument timbres).
* All of it is from a cycle-exact emulated A1200 (D-13). No real A1200 has run it yet.

## 0.6.0 (2026-11-20), phases P6 to P10: the game on screen, with sound and controllers

ADFs: `nhl94-aga-v0.6.0.adf` (public; it boots the game). For the owner of the ROM, made by
`make -C amiga verify-disks` and never published: the verify, intent, show and audio check disks and
`nhl94-rom-a.adf`, `nhl94-rom-b.adf`. Test sheet: amiga/tests/v0.6.0.md.

Added:
* The game itself: `NHL94VFY PLAY` (what the disk boots). The picture on the A1200's own display (one
  six-plane low-resolution playfield, 64 colours of 24 bits, 256 or 320 pixels wide, 60 Hz), the
  sound on Paula, and the Amiga's controllers.
* The match picture: the rink scrolled by the hardware, the players, puck, overlays and clock drawn
  by the blitter in the Genesis's priority order, two buffers shown in turn.
* Sound: the game's FM instruments rendered on your Amiga from your cartridge into looped samples, the
  crowd, organ and speech through the game's own volume table, the PSG as square waves and noise.
* Controllers: a joystick in port 2 (fire 1 = B, fire 2 = A), a CD32 pad (red B, blue C, yellow and
  green A, play Start), the keyboard (cursor keys, Z = A, X = B, C / Space / Return = C, P = Start,
  Esc quits), a joystick in port 1 for player 2, and a parallel-port four-player adapter.
* Records and playoffs are kept in `NHL94.sav` beside the program (written when you quit with Esc).
* Checks: `NHL94VFY <script> SHOW` (the six bitplanes' hash per pass, gate A-DISPLAY-PLANES) and
  `NHL94VFY <script> AUDIO` (the sound commands' hash per pass, gate A-AUDIO-CMD).

Known issues:
* A match does not run at full speed yet: a pass's drawing takes about 33 ms on average with 8 MB of
  Fast RAM (emulated), so play runs at roughly half speed. Menus run at full speed.
* The title stops for about 0.7 s about once a second while its credits move (I-8).
* The sound is a first version: each instrument's timbre is fixed (the FM modulators' envelopes are
  left out) and it is mono. It has not been listened to on a real A1200 yet.
* Screen changes (a fade to black, then the new screen) take up to 4 seconds.
* All of it is from a cycle-exact emulated A1200 (D-13). No real A1200 has run it yet.

Gates passed (emulated A1200, Kickstart 39.106, 8 MB Fast RAM; provisional under D-13): A-LOGIC (7
scripts), A-DISPLAY-INTENT (7 scripts), A-DISPLAY-PLANES (name_entry, attract, pause_menus), A-LEGAL on
the public ADF (D-12). On the host: the drawn picture equals the reference on all 69,293 frames of the
seven golden runs.
Tested by, on: the agent, 2026-11-20, in PUAE. Real hardware: not yet.

## 0.5.1 (2026-11-20), phases P5 and P6: faster logic, and the display requests checked

ADFs: `nhl94-aga-v0.5.1.adf` (public). For the owner of the ROM, made by `make -C amiga verify-disks`
and never published: `nhl94-aga-v0.5.1-private-verify.adf`, `nhl94-aga-v0.5.1-private-intent.adf`,
`nhl94-rom-a.adf` and `nhl94-rom-b.adf`. Test sheet: amiga/tests/v0.5.1.md. There is still no picture
or sound.

Added:
* `NHL94VFY <script> INTENT` (gate A-DISPLAY-INTENT): hashes every display request the game makes in
  a pass (tiles, cells, fills and copies in VRAM, palette, scroll, layout registers) and checks it
  against the reference (`refs/NAME.i32` on the private intent disk; without it the hashes are written
  beside the program for `amiga/tools/h32compare.py`).
* `NHL94VFY <script> PROFILE FIRST LAST`: the sampling profiler for a window of passes, 30 samples a
  frame, none taken during the program's own per-frame check.
* `NAME.over` (every pass longer than one frame) marks the passes whose screen is black afterwards (a
  screen being built). `amiga/tools/overreport.py` and `speedcheck.py` read it; `speedcheck.py` compares
  the long passes with the cartridge's own, by rank.

Changed:
* The game's logic is faster (same results, checked on every pass). Emulated A1200 with 8 MB Fast RAM,
  0.5.0 -> 0.5.1: mean pass 4 to 10 ms -> 1.0 to 3.9 ms of a 16.7 ms frame; the longest pass (a rink
  being loaded) 1.9 s -> 0.3 s, where the cartridge takes 0.9 s. What changed: DMAs go to the display
  side as blocks; a VDP word, a 32-bit command and a fill each take a short path; link-time
  optimisation; reads of the cartridge and of RAM by 68000 address take one load; the game's tile
  colour remap is replaced by a table-driven one for the 68020 (`amiga/host/hot.c`; `make HOT=0`
  builds without it).
* A VRAM copy reaches the display side as one request, and vertical-scroll entries are handed over.
* The disk's ReadMe names the machine the port now targets: an A1200 with Fast RAM (D-14).

Fixed:
* Nothing reported from hardware yet.

Known issues:
* All numbers are from a cycle-exact emulated A1200 (D-13). No real A1200 has run this yet.
* With Fast RAM, a few passes still take longer than a frame with a picture on screen: the in-game
  roster screen as its list scrolls (15 to 17 ms a pass), the save RAM check at power-on (about
  0.1 s, once), and a handful before fades.
* On a stock 2 MB A1200 the logic alone averages 9 ms a pass and about one pass in seven is over a
  frame (records_game, emulated).

Gates passed (emulated A1200, Kickstart 39.106, 8 MB Fast RAM; provisional under D-13): A-LOGIC, 7
scripts, 69,293 frames, 0 differing. A-DISPLAY-INTENT, 7 scripts, 69,293 passes, 0 differing. A-LEGAL
on the public ADF (D-12).
Tested by, on: the agent, 2026-11-20, in PUAE (amiga/tools/puaerun.py). Real hardware: not yet.

## 0.5.0 (2026-11-20), phase P5: the game's logic on the A1200

ADFs: `nhl94-aga-v0.5.0-private-verify.adf` with `nhl94-rom-a.adf` and `nhl94-rom-b.adf`. These are
private test disks for the owner of the ROM; they are not a release. Test sheet: amiga/tests/v0.5.0.md.
The public `nhl94-aga-v0.5.0.adf` is held back: it fails A-LEGAL (see Known issues).

Added:
* `NHL94VFY` (68020, about 290 KB): the game's whole C (`src/`, without the Z80 relay), compiled for the
  68020 with `amiga/host/hal_core.c` behind `src/hal.h`, with no display or sound yet. It:
  * plays the repository's seven boot-based input scripts;
  * checks work RAM after every pass against the reference hashes (gate A-LOGIC);
  * times every pass with the E clock (the feasibility measurement, PROMPT.md 6.5);
  * powers the game on again between scripts by restoring its data and clearing its BSS;
  * reads the ROM from one file or from two floppy halves, and checks its CRC32;
  * logs to the Shell, the serial port (9600 baud) and `NHL94VFY.log`.
* `hal_core.c` answers the game's hardware requests as the reference host build does in its golden runs:
  * the VDP's request state: registers, the command latch, DMA, fill and copy, the palette, VSRAM;
  * the I/O chip: 3-button pads, the 4 Way Play adaptor, the version byte;
  * the Z80's RAM as a mailbox, so no Z80 code runs;
  * save RAM;
  * the golden runs' stand-in for the HV counter, which is the game's random source (D-9).
* On the host, `amiga/tools/halcheck` checks it against every vanilla golden run. All 69,293 frames of
  the seven runs match, work RAM and VDP state both.
* `setjmp`/`longjmp`, a 64 KB stack through `StackSwap`, and a small C library over dos.library and
  exec (`amiga/host/libc_amiga.c`).

Changed:
* The boot disk's `Startup-Sequence` runs `NHL94VFY`. `NHL94AGA` (the 0.1.0 boot test) is still on the
  disk.

Known issues:
* The game's compiled C holds some 32-byte windows of the cartridge, all from small tables committed in
  `src/`. The owner allowed that on 2026-11-20 (D-12): A-LEGAL reports them and fails on anything else.
* No display, sound or input yet (P6 to P9).
* P2 (the timing bench), P3 (the memory manager) and P4 (the installer) are not done. P5 came first
  because it is the feasibility gate.

Gates passed on the host: the cross build (`-Werror`; two `src/`-only warnings kept as warnings,
STATUS.md), and halcheck on 7 of 7 runs. Gates on a stock A1200: waiting for the test sheet (A-LOGIC
and the pass times).

## 0.1.0 (2026-11-20), phase P1: toolchain and boot

ADF: nhl94-aga-v0.1.0.adf. This is a boot and takeover test with no game yet. Its test sheet is
amiga/tests/v0.1.0.md.

Added:
* A bootable 880 KB FFS disk, `NHL94AGA`, with a standard bootblock and an `S:Startup-Sequence`
  that uses only the Shell's built-in commands. It also runs from a Shell or a Workbench of your own.
* The program `NHL94AGA` (68020, about 6.6 KB, `$VER:` string):
  * it checks for Kickstart V39 or newer, a 68020 or better, and AGA (Alice and Lisa), and refuses
    with a message otherwise (a requester from Workbench);
  * it draws its report with graphics.library while the OS still owns the screen;
  * it stops the floppy motors through trackdisk.device;
  * it takes the machine the system-legal way: `LoadView(NULL)`, `OwnBlitter`, `Forbid`, the VBR read
    through `Supervisor`, and `INTENA`/`DMACON`/`ADKCON` saved;
  * it shows a copper screen: a 64-line 24-bit AGA gradient and a raster bar moved by the vblank
    interrupt;
  * it gives everything back on the left mouse button or fire;
  * it checks that Chip and total free memory are the same before and after (gate A-FIRMWARE), and
    writes its log to the Shell and `NHL94AGA.log`.
* `ReadMe`, `Credits.txt` (the credit in full) and this changelog as `Changelog.txt`.
* The build: GNU GCC 13 and binutils for m68k (freestanding, `-mcpu=68020 -msoft-float`), the port's
  own ELF-to-hunk converter (`amiga/tools/elf2hunk.py`), and amitools' `xdftool` for the disk.

Known issues:
* No Workbench icon yet.
* The ADF is not byte-reproducible: the file system stores the build's timestamps.

Gates passed on the host: the cross build (`-Werror`), the hunk structure (`hunktool info`), and A-LEGAL
(no 32-byte window of the ROM on the disk; the credit present). Gates on a stock A1200: waiting for the
test sheet (A-FIRMWARE, boot on KS 3.0 and 3.1).
