/* halcheck: amiga/host/hal_core.c (the Amiga port's answers to the game's hardware requests),
 * built for the host with the same src/ the Amiga runs, played over a script and checked frame by
 * frame against the reference host build's golden run (out/golden/NAME.golden, make golden). Each
 * golden line holds the FNV-1a 64 hashes of work RAM (as the 68000 has it) and of the VDP's state
 * (registers, VRAM, CRAM, VSRAM) at the game's wait. This tool keeps a VRAM copy only to hash it:
 * the Amiga program keeps none (PROMPT.md section 4).
 *
 * Usage: halcheck ROM GOLDEN FRAMES [SCRIPT]  -> exit 0 when every frame's RAM and VDP hashes match.
 * -o FILE writes the per-frame RAM hashes the Amiga's verify build checks (amiga/host/hash.h,
 * 4 bytes a frame, big-endian). */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "mem.h"
#include "rom.h"
#include "rules.h"
#include "rosters.h"
#include "hal_core.h"
#include "hash.h"
#include "intent.h"
#include "mdrt/vdp.h"                                     /* the host build's renderer: the picture reference only */

#define FNV0 0xCBF29CE484222325ull
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

/* this side's copy of VRAM, inside the host renderer's state so that it can draw the reference
 * picture from it (ext/pulseman's render.c; a host tool only, never part of the Amiga build). A byte
 * of a sprite's first four also goes into the renderer's sprite cache, as its own VDP does it */
static mdrt_vdp refv;
#define vram refv.s.vram
static void vput(uint16_t a, uint8_t b)
{
    vram[a] = b;
    int h40 = core.reg[12] & 0x81;
    uint32_t base = h40 ? (uint32_t)(core.reg[5] & 0x7E) << 9 : (uint32_t)(core.reg[5] & 0x7F) << 9, off = (uint32_t)a - base;
    if (a >= base && off < (h40 ? 0x280u : 0x200u) && (off & 7) < 4) refv.sat[off] = b;
}
static unsigned long n_words, n_block_words, n_bytes, n_fill, n_cram;   /* this frame's requests (HALCHECK_FRAMES) */
/* each request goes into the display intent hash (intent.h: the Amiga's reference, -i) and, for the
 * golden check, into this side's copy of VRAM */
void plat_vram_byte(uint16_t a, uint8_t b) { intent_byte(a, b); vput(a, b); n_bytes++; }
uint8_t plat_vram_read(uint16_t a) { return vram[a]; }
void plat_vram_block(uint16_t a, const uint8_t *src, uint32_t words) { intent_block(a, src, words); for (uint32_t k = 0; k < 2 * words; k++) vput((uint16_t)(a + k), src[k]); n_block_words += words; }
void plat_vram_word(uint16_t a, uint16_t w) { intent_word(a, w); vput(a, (uint8_t)(w >> 8)); vput((uint16_t)(a + 1), (uint8_t)w); n_words++; }
void plat_vram_long(uint16_t a, uint32_t v) { intent_long(a, v); for (int k = 0; k < 4; k++) vput((uint16_t)(a + k), (uint8_t)(v >> (24 - 8 * k))); n_words += 2; }
void plat_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step) { intent_fill(a, b, n, step); n_fill += n; while (n--) { vput(a ^ 1, b); a = (uint16_t)(a + step); } }
void plat_vram_copy(uint16_t a, uint16_t src, uint32_t n, uint8_t step)
{
    intent_copy(a, src, n, step);
    while (n--) { vput(a ^ 1, vram[src ^ 1]); src++; a = (uint16_t)(a + step); }
}
void plat_cram(int i, uint16_t c) { intent_cram(i, c); n_cram++; }
void plat_vsram(int i, uint16_t v) { intent_vsram(i, v); }
static int lit_before;                                 /* palette entries not black when this frame began */
static int saw_blank;                                  /* this frame: the display was switched off at some point */
void plat_reg(int r, uint8_t v) { intent_reg(r, v); if (r == 1 && !(v & 0x40)) saw_blank = 1; }
void plat_sound_command(const uint8_t *z) { (void)z; }
void plat_sound_reset(int running) { (void)running; }
void plat_sound_restart(const uint8_t *z) { (void)z; }
void plat_psg(uint8_t v) { (void)v; }

/* the script: "FRAME:BUTTON+BUTTON,..." held from that frame (native/main.c read_script) */
static struct { long frame; uint16_t b; } *script;
static size_t script_len;
static int load_script(const char *path)
{
    static const struct { const char *n; uint16_t bit; } names[] = {
        { "UP", 1 }, { "DOWN", 2 }, { "LEFT", 4 }, { "RIGHT", 8 }, { "B", 0x10 }, { "C", 0x20 }, { "A", 0x40 }, { "START", 0x80 },
    };
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    static char line[1 << 20];
    size_t cap = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        for (char *it = strtok(line, ",\r\n"); it; it = strtok(NULL, ",\r\n")) {
            char *colon = strchr(it, ':');
            if (!colon) continue;
            if (script_len == cap) script = realloc(script, (cap = cap ? 2 * cap : 256) * sizeof *script);
            script[script_len].frame = atol(it);
            uint16_t b = 0;
            for (char *nm = colon + 1; *nm;) {
                size_t n = strcspn(nm, "+"), i = 0;
                while (i < 8 && (strlen(names[i].n) != n || strncmp(names[i].n, nm, n))) i++;
                if (i == 8) { fprintf(stderr, "%s: button %.*s is not a 3-button pad's\n", path, (int)n, nm); return 0; }
                b |= names[i].bit;
                nm += n + (nm[n] == '+');
            }
            script[script_len++].b = b;
        }
    }
    fclose(f);
    return 1;
}
uint16_t plat_buttons(int pad, long h)
{
    if (pad != 0) return 0;
    uint16_t b = 0;
    for (size_t i = 0; i < script_len; i++) if (script[i].frame <= h) b = script[i].b;
    return b;
}

static FILE *golden, *out, *intent_out;
static long frames_wanted, checked, bad_ram, bad_vdp, first_bad = -1, intent_passes, bad_pic, first_bad_pic;
static jmp_buf done;
void plat_frame_done(long frame)
{
    static uint8_t ram[RAM_SIZE];
    const char *want = getenv("HALCHECK_FRAMES");         /* "406,431,...": what those frames asked of the VDP */
    if (want) {
        char key[24];
        snprintf(key, sizeof key, ",%ld,", frame);
        static char list[4096];
        if (!list[0]) snprintf(list, sizeof list, ",%s,", want);
        if (strstr(list, key)) {
            int lit = 0;
            for (int i = 0; i < 64; i++) lit += core.cram[i] != 0;
            printf("(%d lit at its start) ", lit_before);
            printf("frame %ld: %lu words by the data port, %lu words by DMA blocks, %lu bytes, %lu fill bytes, %lu palette writes, VBint $%06X; display %s%s, %d colours lit\n",
                   frame, n_words, n_block_words, n_bytes, n_fill, n_cram, (unsigned)g.VBint,
                   core.reg[1] & 0x40 ? "on" : "off", saw_blank ? " (switched off in this pass)" : "", lit);
        }
    }
    lit_before = 0;
    for (int i = 0; i < 64; i++) lit_before += core.cram[i] != 0;
    n_words = n_block_words = n_bytes = n_fill = n_cram = 0;
    saw_blank = !(core.reg[1] & 0x40);
    ram_pack(ram);
    uint64_t hr = fnv(FNV0, ram, sizeof ram);
    uint64_t hv = fnv(fnv(fnv(fnv(FNV0, core.reg, 24), vram, sizeof vram), core.cram, sizeof core.cram), core.vsram, sizeof core.vsram);
    /* the picture the host build shows for this frame: its renderer on this side's VDP state */
    static uint16_t pic[240 * 320];
    memcpy(refv.s.reg, core.reg, 24);
    memcpy(refv.s.cram, core.cram, sizeof refv.s.cram);
    memcpy(refv.s.vsram, core.vsram, sizeof refv.s.vsram);
    mdrt_vdp_render(&refv, pic, 320);
    uint64_t hp = FNV0;
    for (int y = 0; y < mdrt_vdp_height(&refv); y++) hp = fnv(hp, pic + y * 320, (size_t)mdrt_vdp_width(&refv) * 2);
    if (out) {                                            /* the Amiga's reference: ram_hash32, big-endian */
        uint32_t h = ram_hash32(ram);
        uint8_t be[4] = { (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
        fwrite(be, 1, 4, out);
    }
    uint32_t hi = intent_take();                          /* this pass's display requests (intent.h) */
    if (intent_out) {
        uint8_t be[4] = { (uint8_t)(hi >> 24), (uint8_t)(hi >> 16), (uint8_t)(hi >> 8), (uint8_t)hi };
        fwrite(be, 1, 4, intent_out);
    }
    if (hi != INTENT_START) intent_passes++;
    char line[256];
    unsigned long long gr, gv, gz, gp;
    if (golden && fgets(line, sizeof line, golden) && sscanf(line, "%llx %llx %llx %llx", &gr, &gv, &gz, &gp) == 4) {
        checked++;
        if (gr != hr) { bad_ram++; if (first_bad < 0) first_bad = frame; }
        if (gv != hv) { bad_vdp++; if (first_bad < 0) first_bad = frame; }
        if (gp != hp) { if (!bad_pic) first_bad_pic = frame; bad_pic++; }
    }
    if (frame >= frames_wanted) longjmp(done, 1);
}

int main(int argc, char **argv_in)
{
    char **volatile argv = argv_in;
    const char *volatile outpath = NULL, *volatile intentpath = NULL;
    volatile int nargs = argc;
    while (nargs > 2 && (!strcmp(argv[1], "-o") || !strcmp(argv[1], "-i"))) {
        if (argv[1][1] == 'o') outpath = argv[2]; else intentpath = argv[2];
        argv += 2;
        nargs -= 2;
    }
    argc = nargs;
    if (argc < 4) { fprintf(stderr, "usage: halcheck [-o RAM_HASHES] [-i INTENT_HASHES] ROM GOLDEN FRAMES [SCRIPT]\n"); return 2; }
    if (!rom_load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    golden = fopen(argv[2], "r");
    frames_wanted = atol(argv[3]);
    if (argc > 4 && !load_script(argv[4])) return 2;
    if (outpath) out = fopen(outpath, "wb");
    if (intentpath) intent_out = fopen(intentpath, "wb");
    memcpy(rules, rule_classic, sizeof rules);            /* (the vanilla game: native/vanilla/stubs.c) */
    rom_art = NULL;
    rom_limit = ROM_SIZE;
    rosters_retail();
    core_power_on(0);
    if (!setjmp(done)) run68k(Start);
    if (out) fclose(out);
    if (intent_out) fclose(intent_out);
    printf("halcheck %s: %ld frames checked, RAM differs in %ld, VDP in %ld; %ld passes with display requests", argv[2], checked, bad_ram, bad_vdp, intent_passes);
    if (first_bad >= 0) printf(", first at frame %ld", first_bad);
    printf("; picture differs in %ld", bad_pic);
    if (bad_pic) printf(", first at frame %ld", first_bad_pic);
    printf(" (DMAs %u, fills %u, VRAM copies %u, VRAM reads %u, sound commands %u, restarts %u, other $96 %u)\n",
           core.dmas, core.fills, core.copies, core.vram_reads, core.commands, core.restarts, core.odd_targets);
    return checked && !bad_ram && !bad_vdp && !bad_pic ? 0 : 1;
}
