# NHL94AGA changelog

> **Credits:** built on [NHL94Genesis](https://github.com/abdulahmad/NHL94Genesis) by abdulahmad, with chaos,
> StaticEck, McMarkis (Mhopkinsinc) and Drezz, and special thanks to smozoma and kingraph. The C, its runtime
> (pulseman) and its verification tools are by 1nput ([ninety-four](https://github.com/1nputs/ninety-four), MIT).

Every ADF of the native Amiga 1200 AGA port gets an entry here. The entry is copied onto the disk as
`Changelog.txt`, credit included. Versions are `MAJOR.MINOR.PATCH` (PROMPT.md section 8.4). The ADF is
`nhl94-aga-vX.Y.Z.adf` and its `$VER:` is `NHL94AGA X.Y.Z (DD.MM.YYYY)`. ADFs are release assets and
never go into git. No ADF holds any byte of the ROM: the game needs your own cartridge dump, which the
native installer converts on your Amiga.

Entry template (newest first):

```
## X.Y.Z (YYYY-MM-DD), phase Pn, commit abcdef0

ADF: nhl94-aga-vX.Y.Z.adf, SHA-256 <hash>

Added:
Changed:
Fixed:
Known issues:
Gates passed on a stock A1200 (machine, Kickstart): A-...
Tested by, on: <date>, <log name>
```

## Unreleased

* The port's brief (`amiga/PROMPT.md`), its state ledger (`amiga/STATUS.md`) and this changelog. No ADF
  yet. The first ADF, 0.1.0, is phase P1 (toolchain and boot).
