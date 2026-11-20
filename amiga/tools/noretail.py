#!/usr/bin/env python3
"""Gate A-LEGAL (amiga/PROMPT.md 8.3): no 32-byte window of the retail ROM appears in a disk image.

Every 32-byte window of the ROM, at every offset, is looked for in every image, at every offset.
Windows of three or fewer distinct byte values (fills, padding) are skipped: they say nothing about
where a disk's bytes came from.

Decision D-12 (the owner, 2026-11-20): --program FILE names a program built from this repository's
committed source (the game's C carries some of the cartridge's tables and strings in src/*_gen.h and
const arrays, as every host release does). Windows found in such a program are counted and reported,
not failed. Anything else from the ROM (art, samples, the ROM itself, data made from it) fails.
Exits 1 and names the first match when a window is found outside the programs; also checks that
Credits.txt is on the disk with the credit. Prints no ROM bytes.
Usage: noretail.py [--program FILE ...] ROM IMAGE.adf [IMAGE.adf ...]"""
import subprocess
import sys

N = 32
CREDIT = 'Credits: built on NHL94Genesis (https://github.com/abdulahmad/NHL94Genesis) by abdulahmad'


def windows(data):
    for i in range(len(data) - N + 1):
        w = data[i:i + N]
        if len(set(w)) > 3:
            yield i, w


def main(rom_path, images, programs):
    rom = open(rom_path, 'rb').read()
    seen = {}
    for i, w in windows(rom):
        seen.setdefault(w, i)
    allowed = set()
    for p in programs:
        found = {w for _, w in windows(open(p, 'rb').read()) if w in seen}
        print("A-LEGAL: %s (built from committed source, D-12): %d windows of the cartridge's tables and strings" % (p, len(found)))
        allowed |= found
    bad = 0
    for img in images:
        data = open(img, 'rb').read()
        hits = [(i, seen[w]) for i, w in windows(data) if w in seen and w not in allowed]
        if hits:
            bad = 1
            print('A-LEGAL FAIL: %s: %d windows of the ROM, first at disk offset $%X (ROM $%06X)' % (img, len(hits), hits[0][0], hits[0][1]))
        else:
            print('A-LEGAL: %s: no %d-byte window of the ROM outside the D-12 programs, %d bytes' % (img, N, len(data)))
        try:
            text = subprocess.run(['xdftool', img, 'type', 'Credits.txt'], capture_output=True, text=True, encoding='latin-1').stdout
        except OSError:
            text = ''
        if CREDIT.split(' (')[0] not in ' '.join(text.split()):
            bad = 1
            print('A-LEGAL FAIL: %s: Credits.txt is missing or lacks the credit' % img)
        else:
            print('A-LEGAL: %s: Credits.txt carries the credit' % img)
    return bad


if __name__ == '__main__':
    args, programs = sys.argv[1:], []
    while len(args) >= 2 and args[0] == '--program':
        programs.append(args[1])
        args = args[2:]
    if len(args) < 2:
        sys.exit(__doc__)
    sys.exit(main(args[0], args[1:], programs))
