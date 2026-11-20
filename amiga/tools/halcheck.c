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

#define FNV0 0xCBF29CE484222325ull
static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

static uint8_t vram[0x10000];
void plat_vram_byte(uint16_t a, uint8_t b) { vram[a] = b; }
uint8_t plat_vram_read(uint16_t a) { return vram[a]; }
void plat_vram_block(uint16_t a, const uint8_t *src, uint32_t words) { memcpy(vram + a, src, 2 * (size_t)words); }
void plat_vram_word(uint16_t a, uint16_t w) { vram[a] = (uint8_t)(w >> 8); vram[(uint16_t)(a + 1)] = (uint8_t)w; }
void plat_vram_fill(uint16_t a, uint8_t b, uint32_t n, uint8_t step) { while (n--) { vram[a ^ 1] = b; a = (uint16_t)(a + step); } }
void plat_cram(int i, uint16_t c) { (void)i; (void)c; }
void plat_reg(int r, uint8_t v) { (void)r; (void)v; }
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

static FILE *golden, *out;
static long frames_wanted, checked, bad_ram, bad_vdp, first_bad = -1;
static jmp_buf done;
void plat_frame_done(long frame)
{
    static uint8_t ram[RAM_SIZE];
    ram_pack(ram);
    uint64_t hr = fnv(FNV0, ram, sizeof ram);
    uint64_t hv = fnv(fnv(fnv(fnv(FNV0, core.reg, 24), vram, sizeof vram), core.cram, sizeof core.cram), core.vsram, sizeof core.vsram);
    if (out) {                                            /* the Amiga's reference: ram_hash32, big-endian */
        uint32_t h = ram_hash32(ram);
        uint8_t be[4] = { (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h };
        fwrite(be, 1, 4, out);
    }
    char line[256];
    unsigned long long gr, gv;
    if (golden && fgets(line, sizeof line, golden) && sscanf(line, "%llx %llx", &gr, &gv) == 2) {
        checked++;
        if (gr != hr) { bad_ram++; if (first_bad < 0) first_bad = frame; }
        if (gv != hv) { bad_vdp++; if (first_bad < 0) first_bad = frame; }
    }
    if (frame >= frames_wanted) longjmp(done, 1);
}

int main(int argc, char **argv_in)
{
    char **volatile argv = argv_in;
    const char *outpath = NULL;
    volatile int nargs = argc;
    if (nargs > 2 && !strcmp(argv[1], "-o")) { outpath = argv[2]; argv += 2; nargs -= 2; }
    argc = nargs;
    if (argc < 4) { fprintf(stderr, "usage: halcheck [-o HASHES] ROM GOLDEN FRAMES [SCRIPT]\n"); return 2; }
    if (!rom_load(argv[1])) { fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    golden = fopen(argv[2], "r");
    frames_wanted = atol(argv[3]);
    if (argc > 4 && !load_script(argv[4])) return 2;
    if (outpath) out = fopen(outpath, "wb");
    memcpy(rules, rule_classic, sizeof rules);            /* (the vanilla game: native/vanilla/stubs.c) */
    rom_art = NULL;
    rom_limit = ROM_SIZE;
    rosters_retail();
    core_power_on(0);
    if (!setjmp(done)) run68k(Start);
    if (out) fclose(out);
    printf("halcheck %s: %ld frames checked, RAM differs in %ld, VDP in %ld", argv[2], checked, bad_ram, bad_vdp);
    if (first_bad >= 0) printf(", first at frame %ld", first_bad);
    printf(" (DMAs %u, fills %u, VRAM copies %u, VRAM reads %u, sound commands %u, restarts %u, other $96 %u)\n",
           core.dmas, core.fills, core.copies, core.vram_reads, core.commands, core.restarts, core.odd_targets);
    return checked && !bad_ram && !bad_vdp ? 0 : 1;
}
