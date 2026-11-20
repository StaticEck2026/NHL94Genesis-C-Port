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
* A-LEGAL fails on a disk that carries the game's compiled C: it holds 630 32-byte windows of the
  cartridge, all from small tables in `src/` (the committed generated headers and const arrays). Until
  the owner decides D-12, game disks stay private.
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
