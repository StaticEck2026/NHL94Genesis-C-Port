#!/usr/bin/env python3
"""D-4 for the Amiga build only, without changing src/: copy src/*.c and src/*.h to OUTDIR and rewrite
src/mem.h's work RAM accessors for a big-endian machine, where struct g already sits at the 68000's
offsets in the machine's own byte order (ram_host_ is the identity there, src/ram.c ram_map).

  ram_byte_  &g + offset, without the 128 KB ram_host_ table lookup
  r16, r32   one load each instead of two or four byte loads
  w16, w32   one store each

The game's code is the same; only how a RAM access is carried out differs. Gate A-LOGIC (every pass's
work RAM hash against the reference) checks the result on every build. Each rewrite must match
src/mem.h exactly, so a change upstream stops the build instead of being silently missed.
Usage: mem_be.py SRCDIR OUTDIR"""
import glob
import os
import shutil
import sys

REWRITES = [
    ("static inline uint8_t *ram_byte_(rp p, int32_t off) { return (uint8_t *)&g + ram_host_[(uint16_t)(p.o + off)]; }",
     "static inline uint8_t *ram_byte_(rp p, int32_t off) { return (uint8_t *)&g + (uint16_t)(p.o + off); }   /* (amiga/tools/mem_be.py: big-endian identity) */"),
    ("static inline uint16_t r16(rp p, int32_t off) { return (uint16_t)(r8(p, off) << 8 | r8(p, off + 1)); }",
     "static inline uint16_t r16(rp p, int32_t off) { uint16_t v; __builtin_memcpy(&v, ram_byte_(p, off), 2); return v; }"),
    ("static inline uint32_t r32(rp p, int32_t off) { return (uint32_t)r16(p, off) << 16 | r16(p, off + 2); }",
     "static inline uint32_t r32(rp p, int32_t off) { uint32_t v; __builtin_memcpy(&v, ram_byte_(p, off), 4); return v; }"),
    ("static inline void w16(rp p, int32_t off, uint16_t v) { w8(p, off, (uint8_t)(v >> 8)); w8(p, off + 1, (uint8_t)v); }",
     "static inline void w16(rp p, int32_t off, uint16_t v) { __builtin_memcpy(ram_byte_(p, off), &v, 2); }"),
    ("static inline void w32(rp p, int32_t off, uint32_t v) { w16(p, off, (uint16_t)(v >> 16)); w16(p, off + 2, (uint16_t)v); }",
     "static inline void w32(rp p, int32_t off, uint32_t v) { __builtin_memcpy(ram_byte_(p, off), &v, 4); }"),
]


def main(src, out):
    os.makedirs(out, exist_ok=True)
    for f in glob.glob(os.path.join(src, '*.c')) + glob.glob(os.path.join(src, '*.h')):
        dst = os.path.join(out, os.path.basename(f))
        data = open(f).read()
        if os.path.basename(f) == 'mem.h':
            for old, new in REWRITES:
                if data.count(old) != 1:
                    sys.exit('mem_be.py: src/mem.h no longer has: ' + old[:70])
                data = data.replace(old, new)
        if not os.path.exists(dst) or open(dst).read() != data:
            open(dst, 'w').write(data)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
