/* src/hal.h for the Amiga port: the game's hardware requests answered the way the reference host
 * build answers them (native/hal_native.c over pulseman, in its golden-run mode), so that the same
 * pad input gives the same work RAM after every pass (gate A-LOGIC, PROMPT.md 3.1).
 *
 * This file is portable C with no Amiga or host calls. It is linked into the Amiga program and into
 * amiga/tools/halcheck (an x86 build that checks it against out/golden, frame by frame). What it
 * keeps is only what the game reads back, or what the backends need to know about a request:
 *
 *   the VDP's request state: its 24 registers, the command latch (code, address, the half-written
 *     flag), the palette (CRAM, which the game reads back for its fades), VSRAM and the last four
 *     data words. VRAM bytes are handed to the display backend as the game writes them
 *     (plat_vram_byte); nothing here draws or keeps a picture (PROMPT.md section 4);
 *   the I/O chip's registers (pads through TH select, the 4 Way Play adaptor, the version byte);
 *   the Z80's RAM as a mailbox: the bytes the game writes, its command marked done when the game
 *     releases the bus (the sound backend's input, plat_sound_command). No Z80 code runs;
 *   battery save RAM (8 KB, $FF at power on);
 *   the 68000's interrupt mask;
 *   the HV counter: the golden runs' stand-in (native/main.c golden_hv: 97 pixels on at each read
 *     from the start of the frame's wait, in a 342 or 420 pixel line and a 262-line frame). It is the
 *     game's random source (amiga/notes/hal_map.md, decision D-9). */
#include <string.h>
#include "game.h"
#include "hal.h"
#include "mem.h"
#include "hal_core.h"

struct core core;

/* ---- power on (native_power_on, mdrt_vdp_init, mdrt_pads_init, mdrt_sram_init) ---------------- */

void core_power_on(int pal)
{
    memset(&core, 0, sizeof core);
    core.pal = pal ? 1 : 0;
    core.sr = 0x2700;
    core.io[0] = (uint8_t)(0x80 | (pal ? 0x40 : 0) | 0x20);   /* overseas, no expansion unit */
    core.io[7] = 0xFF;
    core.io[10] = 0xFF;
    core.io[13] = 0xFB;
    memset(core.sram, 0xFF, sizeof core.sram);
    elapsed = 0;
    assign_d2 = ASSIGN_D2_START;
    le_player = le_attr = 0;
}

/* ---- the VDP's requests ---------------------------------------------------------------------- */

int core_vdp_width(void) { return (core.reg[12] & 0x81) ? 320 : 256; }

/* wmode after anything it depends on changed: 1 while a data word is a plain VRAM write the fast paths
 * can take (the code says VRAM write, no fill waits, no command is half-written, and the address and
 * the auto-increment are both even, so the address stays even). A data word goes into the FIFO's
 * history, a ring of 256 (the entry a read takes its undriven bits from is the oldest of the last four) */
static inline void wmode_sync(void)
{
    core.wmode = ((core.code & 0x0F) == 0x01 && !core.fill && !core.pending && !((core.addr | core.reg[15]) & 1)) ? 1 : 0;
}
static inline void hist_word(uint16_t w) { core.hist[core.hpos++] = w; }
#define FIFO_NEXT (core.hist[(uint8_t)(core.hpos - 4)])

static void bus_write(uint16_t w)
{
    struct core *c = &core;
    hist_word(w);
    switch (c->code & 0x0F) {
    case 0x01:                                            /* VRAM: an odd address swaps the bytes */
        if (!(c->addr & 1)) {
            plat_vram_word(c->addr, w);
        } else {
            uint16_t a = c->addr & 0xFFFE;
            plat_vram_byte(a, (uint8_t)w);
            plat_vram_byte((uint16_t)(a + 1), (uint8_t)(w >> 8));
        }
        break;
    case 0x03: {                                          /* CRAM: 64 words of 9 bits */
        int i = (c->addr >> 1) & 0x3F;
        c->cram[i] = w & 0x0EEE;
        plat_cram(i, c->cram[i]);
        break;
    }
    case 0x05:                                            /* VSRAM: 40 words of 11 bits */
        if ((c->addr & 0x7E) < 0x50) {
            int i = (c->addr & 0x7E) >> 1;
            c->vsram[i] = w & 0x07FF;
            plat_vsram(i, c->vsram[i]);
        }
        break;
    }
    c->addr = (uint16_t)(c->addr + c->reg[15]);
}

static uint32_t dma_length(void)
{
    uint32_t n = (uint32_t)(core.reg[20] << 8 | core.reg[19]);
    return n ? n : 0x10000;
}

static void dma_end(void)
{
    uint16_t end = (uint16_t)(core.reg[21] + (core.reg[22] << 8) + core.reg[19] + (core.reg[20] << 8));
    core.reg[21] = (uint8_t)end;
    core.reg[22] = (uint8_t)(end >> 8);
    core.reg[19] = core.reg[20] = 0;
}

/* the bytes at a 68000 address as one run the DMA can copy in a block: the cartridge's (as stored), or
 * work RAM's where the machine is big-endian (struct g at the 68000's offsets, src/ram.c); NULL
 * elsewhere (a little-endian host's RAM, art past the cartridge), which goes word by word */
static const uint8_t *dma_source(uint32_t a, uint32_t *bytes_left)
{
    a &= 0xFFFFFF;
    if (a < ROM_SIZE) { *bytes_left = ROM_SIZE - a; return rom_ptr(a); }
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    if (a >= 0xFF0000) { *bytes_left = 0x1000000 - a; return (const uint8_t *)&g + (a & 0xFFFF); }
#endif
    return 0;
}

/* a DMA from the 68000's memory: the words the game names, cartridge or work RAM. VRAM targets with an
 * auto-increment of 2 at an even address go to the display backend as blocks (plat_vram_block); the
 * rest word by word, as the VDP takes them */
static void dma_68k(void)
{
    uint32_t n = dma_length();
    uint32_t bank = (uint32_t)(core.reg[23] & 0x7F) << 17;
    uint32_t src = (uint32_t)(core.reg[22] << 8 | core.reg[21]) << 1;
    core.dmas++;
    while (n) {
        uint32_t avail = 0;
        const uint8_t *p = ((core.code & 0x0F) == 0x01 && core.reg[15] == 2 && !(core.addr & 1)) ? dma_source(bank | src, &avail) : 0;
        if (p) {
            uint32_t words = n;
            if (words > (0x20000 - src) / 2) words = (0x20000 - src) / 2;          /* (the source wraps in its bank) */
            if (words > avail / 2) words = avail / 2;
            if (words > (0x10000u - core.addr) / 2) words = (0x10000u - core.addr) / 2;   /* (VRAM wraps) */
            if (words) {
                plat_vram_block(core.addr, p, words);
                for (uint32_t k = words > 4 ? words - 4 : 0; k < words; k++)            /* (the FIFO's last words) */
                    hist_word((uint16_t)(p[2 * k] << 8 | p[2 * k + 1]));
                core.addr = (uint16_t)(core.addr + 2 * words);
                src = (src + 2 * words) & 0x1FFFF;
                n -= words;
                core.dma_blocks++;
                continue;
            }
        }
        uint16_t w = (uint16_t)(m8(bank | src) << 8 | m8(bank | (src + 1)));
        src = (src + 2) & 0x1FFFF;
        bus_write(w);
        n--;
    }
    dma_end();
}

static void dma_copy(void)
{
    if (!(core.code & 0x10)) return;
    uint32_t n = dma_length();
    uint16_t src = (uint16_t)(core.reg[22] << 8 | core.reg[21]);
    core.copies++;
    plat_vram_copy(core.addr, src, n, core.reg[15]);      /* one request: byte k goes from (src + k) ^ 1 to (addr + k * reg15) ^ 1 */
    core.addr = (uint16_t)(core.addr + n * core.reg[15]);
    dma_end();
}

static void dma_fill(uint16_t w)
{
    uint32_t n = dma_length();
    core.fills++;
    if ((core.code & 0x0F) == 0x01) {                     /* one request: byte k goes to (addr + k * reg15) ^ 1 */
        plat_vram_fill(core.addr, (uint8_t)(w >> 8), n, core.reg[15]);
        core.addr = (uint16_t)(core.addr + n * core.reg[15]);
    }
    dma_end();
}

static void ctrl16(uint16_t w)
{
    if (!core.pending) {
        core.addr = (uint16_t)(core.addr_hi | (w & 0x3FFF));
        core.code = (uint8_t)((core.code & 0x3C) | (w >> 14));
        if ((w & 0xC000) == 0x8000) {
            int r = (w >> 8) & 0x1F;
            if (r < 24) { core.reg[r] = (uint8_t)w; plat_reg(r, (uint8_t)w); }
            core.inc = core.reg[15];
        } else {
            core.pending = (core.reg[1] & 4) ? 1 : 0;
        }
        wmode_sync();
        return;
    }
    core.pending = 0;
    core.addr_hi = (uint16_t)((w & 3) << 14);
    core.addr = (uint16_t)(core.addr_hi | (core.addr & 0x3FFF));
    core.code = (uint8_t)((core.code & 0x03) | ((w >> 2) & 0x3C));
    if ((core.code & 0x20) && (core.reg[1] & 0x10)) {
        switch (core.reg[23] >> 6) {
        case 2: core.fill = 1; break;
        case 3: dma_copy(); break;
        default: dma_68k(); break;
        }
    }
    wmode_sync();
}

static void data16(uint16_t w)
{
    core.pending = 0;
    bus_write(w);
    if (core.fill) {
        core.fill = 0;
        dma_fill(w);
    }
    wmode_sync();
}

void vdp_ctrl16(uint16_t v) { ctrl16(v); }

/* a command as one long (move.l to the control port), the usual way the game sets an address: both
 * halves at once when no command is half-written, the first half is not a register write, and the VDP
 * is in mode 5; otherwise word by word, as the port takes them */
void vdp_ctrl(uint32_t v)
{
    struct core *c = &core;
    if (!c->pending && (c->reg[1] & 4) && (v & 0xC00000F0u) == 0x40000000u) {   /* "write VRAM from here", no DMA: most of them */
        uint16_t hi = (uint16_t)((v & 3) << 14), a = (uint16_t)(hi | ((v >> 16) & 0x3FFF));
        c->addr_hi = hi;
        c->addr = a;
        c->code = 0x01;
        c->wmode = (!c->fill && !((a | c->inc) & 1)) ? 1 : 0;
        return;
    }
    if (!c->pending && (v & 0xC0000000u) != 0x80000000u && (c->reg[1] & 4)) {
        c->addr_hi = (uint16_t)((v & 3) << 14);
        c->addr = (uint16_t)(c->addr_hi | ((v >> 16) & 0x3FFF));
        c->code = (uint8_t)((v >> 30) | ((v >> 2) & 0x3C));
        if ((c->code & 0x20) && (c->reg[1] & 0x10)) {
            switch (c->reg[23] >> 6) {
            case 2: c->fill = 1; break;
            case 3: dma_copy(); break;
            default: dma_68k(); break;
            }
        }
        wmode_sync();
        return;
    }
    ctrl16((uint16_t)(v >> 16));
    ctrl16((uint16_t)v);
}

/* a data word: straight to the display backend when it is a plain VRAM write at an even address
 * (name-table cells, tile words, the clears: wmode 1), otherwise the general path */
void vdp_data(uint16_t v)
{
    struct core *c = &core;
    if (c->wmode == 1) {
        uint16_t a = c->addr;
        c->hist[c->hpos++] = v;
        c->addr = (uint16_t)(a + c->inc);
        plat_vram_word(a, v);
        return;
    }
    data16(v);
}

void vdp_data32(uint32_t v)
{
    struct core *c = &core;
    if (c->wmode == 1) {
        uint16_t a = c->addr;
        c->hist[c->hpos++] = (uint16_t)(v >> 16);
        c->hist[c->hpos++] = (uint16_t)v;
        c->addr = (uint16_t)(a + 2 * c->inc);
        if (c->inc == 2) {
            plat_vram_long(a, v);
        } else {
            plat_vram_word(a, (uint16_t)(v >> 16));
            plat_vram_word((uint16_t)(a + c->inc), (uint16_t)v);
        }
        return;
    }
    data16((uint16_t)(v >> 16));
    data16((uint16_t)v);
}

uint16_t vdp_read(void)
{
    uint16_t w = 0;
    core.pending = 0;
    switch (core.code & 0x1F) {
    case 0x00: {
        uint16_t a = core.addr & 0xFFFE;
        w = (uint16_t)(plat_vram_read(a) << 8 | plat_vram_read((uint16_t)(a + 1)));
        core.vram_reads++;
        break;
    }
    case 0x04: {
        int i = core.addr & 0x7E;
        if (i >= 0x50) i = 0;
        w = (uint16_t)((core.vsram[i >> 1] & 0x07FF) | (FIFO_NEXT & ~0x07FF));
        break;
    }
    case 0x08:
        w = (uint16_t)(core.cram[(core.addr >> 1) & 0x3F] | (FIFO_NEXT & ~0x0EEE));
        break;
    }
    core.addr = (uint16_t)(core.addr + core.reg[15]);
    wmode_sync();
    return w;
}

uint16_t vdp_status(void)
{
    core.pending = 0;
    wmode_sync();
    uint16_t s = (uint16_t)(0x3400 | 0x0200 | core.pal);
    if (core.vblank || !(core.reg[1] & 0x40)) s |= 0x0008;
    return s;
}

/* the HV counter: the golden runs' stand-in (native/main.c golden_hv) */
uint16_t vdp_hv16(void)
{
    uint32_t width = core_vdp_width() == 320 ? 420 : 342, lines = core.pal ? 313 : 262;
    core.beam += 97;
    uint32_t line = (224 + core.beam / width) % lines, h = core.beam % width;
    return (uint16_t)((line & 0xFF) << 8 | (h >> 1));
}
uint8_t vdp_hv8(void) { return (uint8_t)(vdp_hv16() >> 8); }

/* ---- the I/O chip: pads, the 4 Way Play adaptor, the version byte (mdrt pad.c) ---------------- */

static int th(int port) { return (core.io[4 + port] & 0x40) ? (core.io[1 + port] >> 6) & 1 : 1; }

static uint8_t lines3(uint16_t b, int t)
{
    uint8_t d = (uint8_t)((t << 6) | 0x3F);
    if (t) return (uint8_t)(d & ~(b & 0x3F));                                /* ?1CBRLDU */
    return (uint8_t)(d & ~(((b >> 2) & 0x30) | (b & 0x03) | 0x0C));          /* ?0SA00DU */
}

static uint8_t pad_lines(int port)
{
    int t = th(port);
    if (core.four_way) {
        if (port == 1) return 0x7F;
        uint8_t sel = core.io[2] & core.io[5];
        if (sel & 0x40) return (uint8_t)((t << 6) | 0x3C);
        return lines3(core.buttons[(sel >> 4) & 3], t);
    }
    return lines3(core.buttons[port], t);
}

static uint8_t io_read(uint32_t addr)
{
    int r = (int)((addr & 0x1F) >> 1);
    if (r >= 1 && r <= 3) {
        uint8_t mask = (uint8_t)(0x80 | core.io[r + 3]);
        uint8_t lines = r <= 2 ? pad_lines(r - 1) : 0x7F;
        return (uint8_t)((core.io[r] & mask) | (lines & ~mask));
    }
    return core.io[r & 15];
}

uint8_t pad_read(uint32_t port) { return io_read(port); }
void pad_write(uint32_t port, uint8_t v)
{
    int r = (int)((port & 0x1F) >> 1);
    if (r == 0) return;
    core.io[r & 15] = v;
}
static uint16_t io16(uint32_t addr) { uint8_t b = io_read(addr | 1); return (uint16_t)(b << 8 | b); }
uint16_t io_read16(uint32_t addr) { return io16(addr); }
uint32_t io_read32(uint32_t addr) { return (uint32_t)io16(addr) << 16 | io16(addr + 2); }
void tmss_write(uint32_t v) { (void)v; }

/* ---- the Z80's RAM, a mailbox ---------------------------------------------------------------- */

void z80_busreq(uint16_t v)
{
    /* the bus given back with the relay's jump target ($96-$97) moved off its streaming loop ($7D):
     * $D1 runs a command (the buffer at $02), $29 starts the relay again (and the PCM sample its
     * pointers at $23-$28 name). The sound backend takes it, and the target is back at $7D, as the Z80
     * leaves it when it is done (src/z80.c's header) */
    if (v == 0 && core.z80_running && core.z80[0x97] == 0 && core.z80[0x96] != 0x7D) {
        uint8_t what = core.z80[0x96];
        if (what == 0xD1) { plat_sound_command(core.z80); core.commands++; }
        else if (what == 0x29) { plat_sound_restart(core.z80); core.restarts++; }
        else core.odd_targets++;
        core.z80[0x96] = 0x7D;
    }
}
uint8_t z80_busack(void) { return 0; }                    /* granted at once */
void z80_reset(uint16_t v)
{
    core.z80_running = v != 0;
    plat_sound_reset(core.z80_running);
}
uint8_t z80_ram_read8(uint16_t off) { return core.z80[off & 0x1FFF]; }
void z80_ram_write8(uint16_t off, uint8_t v) { core.z80[off & 0x1FFF] = v; }
void psg_write(uint8_t v) { plat_psg(v); }

/* ---- save RAM: the bytes on the odd addresses ------------------------------------------------- */

uint8_t sram_read8(uint32_t off) { uint32_t i = (off | 1) >> 1; return i < sizeof core.sram ? core.sram[i] : 0xFF; }
void sram_write16(uint32_t off, uint16_t v)
{
    uint32_t i = (off | 1) >> 1;
    if (i < sizeof core.sram) { core.sram[i] = (uint8_t)v; core.sram_dirty = 1; }
}

/* ---- the interrupt mask ---------------------------------------------------------------------- */

uint16_t cpu_get_sr(void) { return core.sr; }
void cpu_set_sr(uint16_t sr) { core.sr = sr; }

/* ---- the frame's end (hal_native.c vblank, mdrt_wait_vblank) ---------------------------------- */

void hal_wait_vblank(void)
{
    core.frames++;
    plat_frame_done(core.frames);                         /* the pass is over: hash, pace, present */
    core.beam = 0;                                        /* (the beam starts again at the handshake) */
    for (int p = 0; p < 4; p++) core.buttons[p] = plat_buttons(p, core.handshakes);
    core.handshakes++;
    core.vblank = 1;
    if (core.reg[1] & 0x20) {                             /* the vblank interrupt */
        core.in_vblank = 1;
        call68k(g.VBint);
        core.in_vblank = 0;
    }
    core.vblank = 0;
}
