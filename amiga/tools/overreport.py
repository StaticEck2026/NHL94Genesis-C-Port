#!/usr/bin/env python3
"""Read a NAME.over from NHL94VFY (the passes that took longer than one Genesis frame: frame number and
microseconds, big-endian pairs; bit 31 of the time says the screen was black after the pass, a screen
being built) and group them: how many with the screen black and how many with a picture shown, a
histogram by size, and runs of neighbouring frames. Usage: overreport.py NAME.over [FRAME_US]"""
import struct
import sys

DARK = 0x80000000


def passes(path):
    """-> [(frame, microseconds, dark)]"""
    d = open(path, 'rb').read()
    out = []
    for i in range(0, len(d) - 7, 8):
        f, us = struct.unpack_from('>II', d, i)
        out.append((f, us & ~DARK, bool(us & DARK)))
    return out


def main(path, frame_us):
    rows = passes(path)
    dark = [r for r in rows if r[2]]
    shown = [r for r in rows if not r[2]]
    print('%s: %d passes over %d us' % (path, len(rows), frame_us))
    for name, part in (('screen black after the pass (a screen being built)', dark), ('a picture shown', shown)):
        if part:
            print('  %s: %d, worst %.1f ms at frame %d' % ((name, len(part)) + max((us / 1000.0, f) for f, us, _ in part)))
    bins = [(1, 1.25), (1.25, 1.5), (1.5, 2), (2, 3), (3, 6), (6, 30), (30, 1e9)]
    for lo, hi in bins:
        n = sum(1 for _, us, _ in rows if lo * frame_us < us <= hi * frame_us)
        if n:
            print('  %5.2fx to %s frames: %d' % (lo, ('%.2fx' % hi) if hi < 1e9 else 'more', n))
    runs = []
    for f, us, _ in rows:
        if runs and f - runs[-1][1] <= 8:
            runs[-1][1] = f
            runs[-1][2].append(us)
        else:
            runs.append([f, f, [us]])
    print('  %d groups (frames within 8 of each other):' % len(runs))
    for a, b, uss in runs[:60]:
        print('    frames %d-%d: %d passes, worst %.1f ms, total %.1f ms' % (a, b, len(uss), max(uss) / 1000.0, sum(uss) / 1000.0))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 16688)
