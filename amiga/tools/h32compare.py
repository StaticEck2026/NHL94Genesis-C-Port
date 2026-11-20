#!/usr/bin/env python3
"""A-LOGIC and A-DISPLAY-INTENT on the host for a disk without references: compare the NAME.h32 files
(work RAM, amiga/host/hash.h) and NAME.i32 files (display requests, amiga/host/intent.h; from
NHL94VFY <script> INTENT) that NHL94VFY wrote on the A1200, 4 bytes a frame, big-endian, with
halcheck's (out/amiga/ref). Usage: h32compare.py AMIGA_DIR [REF_DIR]  -> exit 0 when every frame of
every file matches."""
import os
import sys


def main(amiga_dir, ref_dir):
    status = 0
    for ext, gate in (('.h32', 'A-LOGIC'), ('.i32', 'A-DISPLAY-INTENT')):
        names = sorted(f for f in os.listdir(amiga_dir) if f.endswith(ext))
        if names or ext == '.h32':
            status |= compare(amiga_dir, ref_dir, names, gate)
    return status


def compare(amiga_dir, ref_dir, names, gate):
    bad = files = 0
    for name in names:
        ref_path = os.path.join(ref_dir, name)
        if not os.path.exists(ref_path):
            print('%s: no reference in %s' % (name, ref_dir))
            bad = 1
            continue
        a = open(os.path.join(amiga_dir, name), 'rb').read()
        r = open(ref_path, 'rb').read()
        n = min(len(a), len(r)) // 4
        diff = [i for i in range(n) if a[4 * i:4 * i + 4] != r[4 * i:4 * i + 4]]
        files += 1
        if diff or len(a) // 4 < len(r) // 4:
            bad = 1
        print('%s: %d frames from the Amiga of %d, %d differ%s' % (name, len(a) // 4, len(r) // 4, len(diff),
              ', first at frame %d' % (diff[0] + 1) if diff else ''))
    print('%s: %s (%d files)' % (gate, 'PASS' if files and not bad else 'FAIL', files))
    return 0 if files and not bad else 1


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'out/amiga/ref'))
