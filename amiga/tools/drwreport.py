#!/usr/bin/env python3
"""Read a NAME.drw from NHL94VFY <script> SHOW (each pass's game logic and drawing in raster lines,
big-endian 16-bit pairs) and report: the means, the passes whose logic plus drawing does not fit one
frame of the beam (262 lines NTSC, 312 PAL), the worst passes, and, with a HALCHECK_STATS file of the
same run, the time by kind of screen (the game's vblank routine and width).
Usage: drwreport.py NAME.drw [STATS.csv] [--pal]"""
import collections
import struct
import sys


def main(path, stats, pal):
    d = open(path, 'rb').read()
    rows = [struct.unpack_from('>HH', d, i) for i in range(0, len(d) - 3, 4)]
    lines = 312 if pal else 262
    us = 64.0 if pal else 63.556
    n = len(rows) - 1
    if n <= 0:
        sys.exit('no passes')
    body = rows[1:]
    tot = [a + b for a, b in body]
    print('%s: %d passes; logic mean %.2f ms, drawing mean %.2f ms (max %.1f ms at pass %d); %d passes over one frame (%d lines)' % (
        path, n, sum(a for a, _ in body) * us / 1000 / n, sum(b for _, b in body) * us / 1000 / n,
        max(b for _, b in body) * us / 1000, 2 + max(range(n), key=lambda i: body[i][1]), sum(1 for t in tot if t > lines), lines))
    worst = sorted(range(n), key=lambda i: -tot[i])[:12]
    print('  the longest: ' + ', '.join('pass %d %.1f+%.1f ms' % (i + 2, body[i][0] * us / 1000, body[i][1] * us / 1000) for i in worst))
    if stats:
        kind = {}
        for line in open(stats):
            v = line.strip().split(',')
            kind[int(v[0])] = (v[-1], int(v[1]))
        g = collections.OrderedDict()
        for i, (a, b) in enumerate(body):
            k = kind.get(i + 2)
            if k:
                g.setdefault(k, []).append((a, b))
        for (vb, w), rs in g.items():
            print('  VBint $%s, %d px: %d passes, logic mean %.2f ms, drawing mean %.2f ms max %.1f ms, %d over a frame' % (
                vb, w, len(rs), sum(a for a, _ in rs) * us / 1000 / len(rs), sum(b for _, b in rs) * us / 1000 / len(rs),
                max(b for _, b in rs) * us / 1000, sum(1 for a, b in rs if a + b > lines)))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        sys.exit(__doc__)
    main(args[0], args[1] if len(args) > 1 else None, '--pal' in sys.argv)
