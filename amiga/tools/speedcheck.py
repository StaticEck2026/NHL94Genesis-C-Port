#!/usr/bin/env python3
"""Never later than the cartridge: compare the Amiga's long passes (NAME.over from NHL94VFY) with the
cartridge's own (the lockstep recording of the same script, read as amiga/tools/cartlag.py does).

The Amiga's run and the cartridge's recording are not the same game frame for frame (a script's
buttons land on passes in one and on vblanks in the other, and the random source differs), so passes
are not paired by frame number. The rule is by rank: the Amiga may have no more late passes than the
cartridge has late stretches, and its k-th longest pass may be no more frames late than the
cartridge's k-th longest stretch. A script with no recording is judged on the second figure only.

Printed beside it, because Smooth speed (PROMPT.md 3.3) is the real target: the passes over a frame
while a picture is shown (not a screen being built in the dark). These must reach zero.
Exit status 1 when the rank rule is broken. Usage: speedcheck.py NAME.over [NAME.lockstep]"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cartlag import records  # noqa: E402
from overreport import passes  # noqa: E402

FRAME_US = 16688


def cart_stretches(path):
    out, n = [], 0
    for _, flags, _ in records(path):
        if flags & 1:
            if n:
                out.append(n)
            n = 0
        else:
            n += 1
    return sorted(out, reverse=True)


def main(over_path, lockstep_path):
    rows = passes(over_path)
    late = sorted((-(-us // FRAME_US) - 1 for _, us, _ in rows), reverse=True)   # a pass of 1.2 frames is 1 late
    shown = [(f, us) for f, us, dark in rows if not dark]
    name = os.path.basename(over_path)
    print('%s: %d passes over a frame, %d frames late in all, the longest %d frames late' % (name, len(late), sum(late), late[0] if late else 0))
    print('  with a picture shown: %d (Smooth speed wants 0)%s' % (
        len(shown), ', worst %.1f ms at frame %d' % max((us / 1000.0, f) for f, us in shown) if shown else ''))
    if not lockstep_path:
        return 0
    cart = cart_stretches(lockstep_path)
    print('  the cartridge, same script: %d late stretches, %d frames late in all, the longest %d frames late' % (len(cart), sum(cart), cart[0] if cart else 0))
    bad = [(k, a, cart[k] if k < len(cart) else 0) for k, a in enumerate(late) if k >= len(cart) or a > cart[k]]
    if bad:
        print('  LATER THAN THE CARTRIDGE at %d ranks; the first: rank %d, Amiga %d frames late, cartridge %d' % ((len(bad),) + bad[0]))
        return 1
    print('  never later than the cartridge (by rank)')
    return 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None))
