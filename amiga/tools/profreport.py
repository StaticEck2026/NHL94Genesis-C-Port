#!/usr/bin/env python3
"""Read a NAME.prof from NHL94VFY's profiler (amiga/host/prof.c) and name where the time went, by the
program's ELF symbols (build/amiga/NHL94VFY.elf: code hunk offset 0 is ELF address 0). Prints the
functions with the most samples, their share, and the source file each comes from (by the link map).
Usage: profreport.py NAME.prof [NHL94VFY.elf] [TOP]"""
import bisect
import struct
import sys
from elftools.elf.elffile import ELFFile


def main(prof, elf_path, top):
    data = open(prof, 'rb').read()
    magic, n, other = struct.unpack('>III', data[:12])
    if magic != 0x50524F46:
        sys.exit('not a profile')
    counts = struct.unpack('>%dI' % n, data[12:12 + 4 * n])
    elf = ELFFile(open(elf_path, 'rb'))
    syms = sorted((s['st_value'], s.name) for s in elf.get_section_by_name('.symtab').iter_symbols()
                  if s['st_info']['type'] == 'STT_FUNC' and s['st_shndx'] != 'SHN_UNDEF')
    addrs = [a for a, _ in syms]
    per = {}
    for i, c in enumerate(counts):
        if not c:
            continue
        k = bisect.bisect_right(addrs, i * 16) - 1
        name = syms[k][1] if k >= 0 else '?'
        per[name] = per.get(name, 0) + c
    total = sum(counts) + other
    print('%d samples, %d outside the program (the OS, interrupts)' % (total, other))
    acc = 0
    for name, c in sorted(per.items(), key=lambda kv: -kv[1])[:top]:
        acc += c
        print('%6.2f%%  %6.2f%% cumulative  %7d  %s' % (100.0 * c / total, 100.0 * acc / total, c, name))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'build/amiga/NHL94VFY.elf', int(sys.argv[3]) if len(sys.argv) > 3 else 40)
