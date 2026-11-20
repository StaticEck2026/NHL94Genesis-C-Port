#!/usr/bin/env python3
"""Turn the Amiga program's linked ELF (amiga/host/amiga.ld, linked with --emit-relocs) into an
AmigaDOS load file (hunk format: The AmigaDOS Manual, 3rd edition, chapter 10).

Each allocated ELF section becomes one hunk, in the linker script's order: .text (HUNK_CODE),
.data (HUNK_DATA), .bss (HUNK_BSS). Sections are linked at bases far apart (0, $10000000,
$20000000), so the value an absolute 32-bit relocation left in place says which hunk it points into;
it is rewritten as an offset in that hunk and listed in HUNK_RELOC32. A PC-relative relocation that
crosses hunks cannot be loaded, and stops the conversion.

Memory: a section whose name ends in '.chip' gets MEMF_CHIP in the header (bit 30); the rest load
anywhere. Usage: elf2hunk.py IN.elf OUT"""
import struct
import sys
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection

HUNK_HEADER, HUNK_CODE, HUNK_DATA, HUNK_BSS, HUNK_RELOC32, HUNK_END = 0x3F3, 0x3E9, 0x3EA, 0x3EB, 0x3EC, 0x3F2
R_68K_32, R_68K_PC32, R_68K_PC16, R_68K_PC8 = 1, 4, 5, 6
ORDER = ['.text', '.data', '.bss']


def fail(msg):
    sys.exit('elf2hunk: ' + msg)


def main(src, dst):
    elf = ELFFile(open(src, 'rb'))
    if elf['e_machine'] != 'EM_68K':
        fail('not a 68k ELF')
    secs = [s for s in elf.iter_sections() if s['sh_flags'] & 2 and s['sh_size'] > 0]   # SHF_ALLOC
    names = [s.name for s in secs]
    for n in names:
        if n not in ORDER and not n.endswith('.chip'):
            fail('section %s has no hunk (add it to amiga.ld)' % n)
    secs.sort(key=lambda s: ORDER.index(s.name) if s.name in ORDER else len(ORDER))
    index = {s.name: i for i, s in enumerate(secs)}
    base = [s['sh_addr'] for s in secs]
    size = [s['sh_size'] for s in secs]
    data = [bytearray(s.data()) if s['sh_type'] != 'SHT_NOBITS' else None for s in secs]
    relocs = [dict() for _ in secs]          # per hunk: target hunk -> [offsets]

    def hunk_of(value):
        for i, s in enumerate(secs):
            if base[i] <= value <= base[i] + size[i]:
                return i
        return None

    for rs in elf.iter_sections():
        if not isinstance(rs, RelocationSection):
            continue
        target = elf.get_section(rs['sh_info'])
        if target.name not in index:
            continue
        h = index[target.name]
        if data[h] is None:
            fail('relocations in BSS')
        for r in rs.iter_relocations():
            t, off = r['r_info_type'], r['r_offset'] - base[h]
            if t == R_68K_32:
                (v,) = struct.unpack_from('>I', data[h], off)
                th = hunk_of(v)
                if th is None:
                    fail('%s+$%X: $%08X points into no section' % (target.name, off, v))
                struct.pack_into('>I', data[h], off, v - base[th])
                relocs[h].setdefault(th, []).append(off)
            elif t in (R_68K_PC32, R_68K_PC16, R_68K_PC8):
                pass                         # (resolved by the linker; checked below)
            elif t == 0:
                pass
            else:
                fail('%s+$%X: relocation type %d is not supported' % (target.name, off, t))
        # a PC-relative reference must stay inside its own hunk
        symtab = elf.get_section(rs['sh_link'])
        for r in rs.iter_relocations():
            if r['r_info_type'] in (R_68K_PC32, R_68K_PC16, R_68K_PC8):
                sym = symtab.get_symbol(r['r_info_sym'])
                dest = sym['st_value'] + r['r_addend']
                if sym['st_shndx'] not in ('SHN_UNDEF', 'SHN_ABS') and hunk_of(dest) not in (h, None):
                    fail('%s+$%X: PC-relative reference into another hunk (%s)' % (target.name, r['r_offset'] - base[h], sym.name))

    out = bytearray()
    longs = lambda n: (n + 3) // 4
    out += struct.pack('>IIIII', HUNK_HEADER, 0, len(secs), 0, len(secs) - 1)
    for s, n in zip(secs, size):
        flags = 0x40000000 if s.name.endswith('.chip') else 0
        out += struct.pack('>I', longs(n) | flags)
    for i, s in enumerate(secs):
        if data[i] is None:
            out += struct.pack('>II', HUNK_BSS, longs(size[i]))
        else:
            kind = HUNK_CODE if s.name == '.text' else HUNK_DATA
            body = bytes(data[i]) + b'\0' * (longs(size[i]) * 4 - size[i])
            out += struct.pack('>II', kind, longs(size[i])) + body
            if relocs[i]:
                out += struct.pack('>I', HUNK_RELOC32)
                for th in sorted(relocs[i]):
                    offs = sorted(relocs[i][th])
                    out += struct.pack('>II', len(offs), th) + b''.join(struct.pack('>I', o) for o in offs)
                out += struct.pack('>I', 0)
        out += struct.pack('>I', HUNK_END)
    open(dst, 'wb').write(out)
    nrel = sum(len(v) for r in relocs for v in r.values())
    print('elf2hunk: %s: %s, %d relocations, %d bytes' % (dst, ', '.join('%s %d' % (s.name, n) for s, n in zip(secs, size)), nrel, len(out)))


if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
