#!/usr/bin/env python3
"""Never later than the cartridge: compare the Amiga's long passes (NAME.over from NHL94VFY) with the
cartridge's own (the lockstep vector of the same script, read as amiga/tools/cartlag.py does).

A pass may take more than one frame only where the cartridge's pass does (a screen being built), and
then no more frames than the cartridge took. The two timelines count frames the same way up to the
drift a late pass causes, so a cartridge stretch within WINDOW frames of the Amiga's pass is taken as
the same pass. Prints the passes that break the rule and exits 1 if there are any.
Usage: speedcheck.py NAME.over NAME.lockstep [WINDOW]"""
import struct
import sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from cartlag import records  # noqa: E402

FRAME_US = 16688


def main(over_path, lockstep_path, window):
    d = open(over_path, 'rb').read()
    amiga = [struct.unpack_from('>II', d, i) for i in range(0, len(d) - 7, 8)]
    cart, start, n = [], None, 0                      # (first late frame, frames late)
    for frame, flags, _ in records(lockstep_path):
        if flags & 1:
            if n:
                cart.append((start, n))
            n = 0
        else:
            if not n:
                start = frame
            n += 1
    last_cart = max((s + k for s, k in cart), default=0)
    alone, slower, fine, beyond = [], [], 0, 0
    for f, us in amiga:
        late = -(-us // FRAME_US) - 1                   # frames late: a pass of 1.2 frames is 1 late
        if f > last_cart + window:
            beyond += 1
            continue
        near = [k for s, k in cart if s - window <= f <= s + k + window]
        if not near:
            alone.append((f, us))
        elif late > max(near):
            slower.append((f, us, late, max(near)))
        else:
            fine += 1
    print('%s: %d passes over a frame; %d where the cartridge is late too and takes at least as long' % (over_path, len(amiga), fine))
    if beyond:
        print('  %d past the end of the cartridge recording (not judged)' % beyond)
    print('  %d where the cartridge is late but the Amiga takes longer:' % len(slower))
    for f, us, late, c in slower[:40]:
        print('    frame %d: Amiga %.0f ms (%d frames late), cartridge %d frames late' % (f, us / 1000.0, late, c))
    print('  %d where the cartridge is on time and the Amiga is not:' % len(alone))
    for f, us in alone[:40]:
        print('    frame %d: Amiga %.1f ms' % (f, us / 1000.0))
    return 1 if alone or slower else 0


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 4))
