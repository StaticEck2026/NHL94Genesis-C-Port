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
