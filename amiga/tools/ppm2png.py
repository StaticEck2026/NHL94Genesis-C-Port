#!/usr/bin/env python3
"""Turn halcheck's HALCHECK_PPM pictures (binary PPM) into PNG files beside them, doubled in size.
Usage: ppm2png.py FILE.ppm [...]"""
import os
import sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'oracle'))
import retro  # noqa: E402


def main(paths):
    for p in paths:
        d = open(p, 'rb').read()
        head, rest = d.split(b'\n', 1)
        _, w, h, _ = head.split()
        w, h = int(w), int(h)
        rows = []
        for y in range(h):
            line = rest[y * w * 3:(y + 1) * w * 3]
            big = b''.join(line[i:i + 3] * 2 for i in range(0, w * 3, 3))
            rows += [b'\0' + big] * 2
        open(p[:-4] + '.png', 'wb').write(retro._png(2 * w, 2 * h, b''.join(rows)))


if __name__ == '__main__':
    main(sys.argv[1:])
