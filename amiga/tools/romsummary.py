#!/usr/bin/env python3
"""P0.3: summarise amiga/tools/rommap.sh's windows (out/amiga/romtouch/<run>.pages).

A window that reads 200 or more of the 256 pages is the cartridge's own ROM checksum (checksum.c reads
every byte at boot) and is left out of the working sets: on the Amiga the installer checks the ROM
once, and the game's checksum routine reads a stored answer (a decision for P5). Prints, per run, the
pages read outside the checksum, the largest 1-second window, and the union over all runs."""
import os
import sys

PAGE = 4096
CHECKSUM = 200

def load(path):
    rows = []
    for line in open(path):
        parts = line.split()
        if parts:
            rows.append((int(parts[0]), {int(p, 16) for p in parts[1:]}))
    return rows

def main(d):
    union, report = set(), []
    for name in sorted(f[:-6] for f in os.listdir(d) if f.endswith('.pages')):
        rows = load(os.path.join(d, name + '.pages'))
        sums = [f for f, s in rows if len(s) >= CHECKSUM]
        work = [(f, s) for f, s in rows if len(s) < CHECKSUM]
        seen = set().union(*(s for _, s in work)) if work else set()
        peak = max(work, key=lambda r: len(r[1])) if work else (0, set())
        union |= seen
        report.append((name, len(rows), sums, len(seen), peak[0], len(peak[1])))
    print('| Run | Windows | Checksum windows (first frame) | Pages read (KB) | Largest window: frame, KB |')
    print('|---|---|---|---|---|')
    for name, n, sums, seen, pf, pn in report:
        print('| %s | %d | %s | %d (%d) | %d, %d |' % (name, n, ' '.join(map(str, sums)) or '-', seen, seen * 4, pf, pn * 4))
    print('\nUnion over all runs, checksum left out: %d pages of 256 (%d KB of 1024)' % (len(union), len(union) * 4))
    runs = []
    lo = None
    for p in sorted(union) + [None]:
        if p is not None and lo is not None and p == prev + PAGE:
            prev = p
            continue
        if lo is not None:
            runs.append((lo, prev + PAGE))
        lo = prev = p
    print('Ranges: ' + ', '.join('$%06X-$%06X' % r for r in runs))

if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'out/amiga/romtouch')
