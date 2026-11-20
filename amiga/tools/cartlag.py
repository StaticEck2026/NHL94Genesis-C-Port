#!/usr/bin/env python3
"""How long the cartridge itself takes: read a lockstep vector (out/vectors/NAME.lockstep, the format in
tools/hybrid/oracle_shim.c) and list the stretches of vblanks where the cartridge's 68000 was not in a
vblank wait loop, that is, where its game code ran longer than one frame. The Amiga's long passes
(amiga/tools/overreport.py) are judged against these: a screen load may take as long as the
cartridge's does. Usage: cartlag.py NAME.lockstep [MIN_FRAMES]"""
import struct
import sys


def records(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'MDLS' and struct.unpack_from('<I', d, 4)[0] == 8, 'not a version 8 lockstep vector'
    p = 16
    while p < len(d) and d[p] == 1:
        flags = d[p + 1]
        frame, pc = struct.unpack_from('<II', d, p + 2)
        p += 2 + 8 + 8 + 4 + 4
        nhv = struct.unpack_from('<H', d, p)[0]
        p += 2 + 3 * nhv + 16
        if flags & 2:
            p += 65536 + 24 + 65536 + 128 + 80 + 64 + 8192 + 8 + 8192 + 4 + 8192 + 16 + 16
        yield frame, flags, pc


def main(path, least):
    runs, start, n, total, waits = [], None, 0, 0, 0
    for frame, flags, pc in records(path):
        total += 1
        if flags & 1:
            waits += 1
            if n:
                runs.append((start, n))
            n = 0
        else:
            if not n:
                start = frame
            n += 1
    lag = sum(k for _, k in runs)
    print('%s: %d vblanks, %d at a wait, %d inside game code (%d stretches)' % (path, total, waits, lag, len(runs)))
    for lo, hi in ((1, 1), (2, 2), (3, 5), (6, 29), (30, 59), (60, 10**9)):
        k = [r for r in runs if lo <= r[1] <= hi]
        if k:
            print('  %s frames late: %d stretches' % (('%d' % lo) if lo == hi else ('%d to %d' % (lo, hi) if hi < 10**9 else '%d or more' % lo), len(k)))
    print('  the longest (first frame, frames, time at 59.92 Hz):')
    for s, k in sorted(runs, key=lambda r: -r[1])[:25]:
        if k >= least:
            print('    frame %6d: %3d frames, %.0f ms' % (s, k, k * 16.688))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3)
