#!/usr/bin/env python3
"""What the picture is made of, scene by scene: summarise the per-frame lines amiga/tools/halcheck
writes with HALCHECK_STATS=FILE (one golden run through amiga/host/hal_core.c). Frames are grouped
by the game's vblank routine and the picture's width, which together name a kind of screen. For each
group: how many frames, the planes' cells that are not empty and their palette lines and priority,
the window, the sprites (how many, their area, the most on a line, palette lines, priority), whether
the planes scroll, and the VRAM bytes a pass sends by kind (worst and mean).
Usage: dispstats.py STATS.csv [...]"""
import collections
import sys

F = ('frame width disp lit r11 pw ph a_n a_pal a_pri b_n b_pal b_pri wrows wcols s_n s_area s_line s_pal s_pri '
     'hsa vsa hsb vsb cell_b tile_b sat_b hs_b vbint').split()


def lines_of(pal):
    return ''.join(str(i) for i in range(4) if pal >> i & 1) or '-'


def main(paths):
    groups = collections.OrderedDict()
    for path in paths:
        prev = None
        for line in open(path):
            v = line.strip().split(',')
            r = {k: (x if k == 'vbint' else int(x)) for k, x in zip(F, v)}
            r['scroll'] = prev is not None and (r['vsa'], r['hsa'], r['vsb'], r['hsb']) != prev
            prev = (r['vsa'], r['hsa'], r['vsb'], r['hsb'])
            if r['lit']:
                groups.setdefault((r['vbint'], r['width']), []).append(r)
    for (vb, width), rs in groups.items():
        n = len(rs)

        def mx(k):
            return max(r[k] for r in rs)

        def mean(k):
            return sum(r[k] for r in rs) / n

        def pals(k):
            p = 0
            for r in rs:
                p |= r[k]
            return lines_of(p)
        print('VBint $%s, %d px: %d lit frames (%d-%d), plane %dx%d cells, scroll mode %d' % (
            vb, width, n, rs[0]['frame'], rs[-1]['frame'], rs[0]['pw'], rs[0]['ph'], mx('r11')))
        print('  plane A: up to %d cells not empty (mean %.0f), palette lines %s, high priority in %d frames (up to %d cells)' % (
            mx('a_n'), mean('a_n'), pals('a_pal'), sum(1 for r in rs if r['a_pri']), mx('a_pri')))
        print('  plane B: up to %d cells not empty (mean %.0f), palette lines %s, high priority in %d frames (up to %d cells)' % (
            mx('b_n'), mean('b_n'), pals('b_pal'), sum(1 for r in rs if r['b_pri']), mx('b_pri')))
        print('  window: rows up to %d, columns up to %d (in %d frames)' % (mx('wrows'), mx('wcols'), sum(1 for r in rs if r['wrows'] or r['wcols'])))
        print('  sprites: up to %d on screen (mean %.1f), area up to %d px (mean %.0f), up to %d on a line, palette lines %s, high priority in %d frames' % (
            mx('s_n'), mean('s_n'), mx('s_area'), mean('s_area'), mx('s_line'), pals('s_pal'), sum(1 for r in rs if r['s_pri'])))
        print('  scroll changes in %d frames; a pass sends up to %d cell bytes (mean %.0f), %d tile bytes (mean %.0f), %d sprite table bytes' % (
            sum(1 for r in rs if r['scroll']), mx('cell_b'), mean('cell_b'), mx('tile_b'), mean('tile_b'), mx('sat_b')))
        busy = sorted(rs, key=lambda r: -r['cell_b'])[:3]
        print('  the passes with the most cell bytes: ' + ', '.join('frame %d (%d)' % (r['frame'], r['cell_b']) for r in busy))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1:])
