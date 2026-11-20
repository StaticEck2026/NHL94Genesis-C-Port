#!/usr/bin/env python3
"""Gate A-LEGAL (amiga/PROMPT.md 8.3): no 32-byte window of the retail ROM appears in a disk image.

Every 32-byte window of the ROM, at every offset, is looked for in every image, at every offset.
Windows of three or fewer distinct byte values (fills, padding) are skipped: they say nothing about
where a disk's bytes came from. Exits 1 and names the first matches when any window is found; also
checks that Credits.txt is on the disk with the credit's opening line. Prints no ROM bytes.
Usage: noretail.py ROM IMAGE.adf [IMAGE.adf ...]"""
import subprocess
import sys

N = 32
CREDIT = 'Credits: built on NHL94Genesis (https://github.com/abdulahmad/NHL94Genesis) by abdulahmad'


def windows(data):
    for i in range(len(data) - N + 1):
        w = data[i:i + N]
        if len(set(w)) > 3:
            yield i, w


def main(rom_path, images):
    rom = open(rom_path, 'rb').read()
    seen = {}
    for i, w in windows(rom):
        seen.setdefault(w, i)
    bad = 0
    for img in images:
        data = open(img, 'rb').read()
        hits = [(i, seen[w]) for i, w in windows(data) if w in seen]
        if hits:
            bad = 1
            print('A-LEGAL FAIL: %s: %d windows of the ROM, first at disk offset $%X (ROM $%06X)' % (img, len(hits), hits[0][0], hits[0][1]))
        else:
            print('A-LEGAL: %s: no %d-byte window of the ROM in %d bytes' % (img, N, len(data)))
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
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2:]))
