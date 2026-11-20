/* The game's sound on Paula: see sound.h.
 *
 * FM (six channels): a key on starts the channel's patch, rendered once by fmpatch.c into a looped
 * waveform (five lengths, for the range of pitches Paula's period can reach), at the period for the
 * channel's frequency; its level is the carriers' attenuation from the game's volume command plus an
 * envelope stepped once a pass from the first carrier's rates (attack, decay to the sustain level,
 * the second decay, and the release at key off).
 * PCM (the crowd, the organ, speech): at a restart the start pointer's sample and the end pointer's
 * (which repeats) are taken through the game's volume table in Z80 RAM, as the relay does, to signed
 * bytes; the rate byte sets the period.
 * PSG (three square channels and noise): square waves and a noise loop.
 * Voices (D-3, the starting rule): PCM keeps channel 3 while it plays; the newest FM key ons, then the
 * PSG's audible channels, take the others; a key on takes the quietest voice when none is free. */
#include <string.h>
#include "sound.h"
#include "fmpatch.h"
#include "fmtab.h"

struct paula_voice paula[4];

#define FM_CLOCK 7670453u                                 /* the YM2612's clock (NTSC) */
#define PAULA_CLOCK 3579545u                              /* Paula's (NTSC) */
#define PATCHES 256
static const uint8_t *rom;
static uint8_t *chip, *chip_end, *chip_next;
static int8_t *patch_wave[PATCHES][5];                    /* lengths 128, 64, 32, 16, 8 */
static int8_t *square, *noise, *silence;

struct fm { int on, patch, att, env, phase, voice; uint16_t a4a0; uint32_t age; uint16_t pitch_for, per; int k; };   /* (the pitch worked out for a4a0 pitch_for) */
static struct fm fm[7];                                   /* by the command's channel number e: 0-2 and 4-6 */
static struct { uint16_t tone[4]; uint8_t att[4]; int latch; int voice[4]; } psg;
static struct { const int8_t *start, *loop; uint32_t start_len, loop_len; uint16_t per; int on, voice, fresh; } pcm;
static uint32_t now;
static const uint8_t *z80;

static void *alloc(uint32_t n)
{
    n = (n + 3) & ~3u;
    if (chip_next + n > chip_end) return 0;
    void *p = chip_next;
    chip_next += n;
    return p;
}

int sound_start(uint8_t *c, uint32_t bytes, const uint8_t *r)
{
    chip = c; chip_end = c + bytes; chip_next = c; rom = r;
    memset(patch_wave, 0, sizeof patch_wave);
    memset(fm, 0, sizeof fm);
    memset(&psg, 0, sizeof psg);
    memset(&pcm, 0, sizeof pcm);
    memset(paula, 0, sizeof paula);
    for (int i = 0; i < 7; i++) fm[i].voice = -1;
    for (int i = 0; i < 4; i++) { psg.voice[i] = -1; psg.att[i] = 15; }
    pcm.voice = -1;
    square = alloc(32);
    noise = alloc(4096);
    silence = alloc(4);
    if (!square || !noise || !silence) return 0;
    for (int i = 0; i < 32; i++) square[i] = (int8_t)(i < 16 ? 100 : -100);
    uint32_t lfsr = 0x8001;
    for (int i = 0; i < 4096; i++) { lfsr = (lfsr >> 1) ^ (-(lfsr & 1) & 0xB400u); noise[i] = (int8_t)((lfsr & 1) ? 90 : -90); }
    memset(silence, 0, 4);
    return 1;
}

/* ---- FM ----------------------------------------------------------------------------------------- */

static const uint8_t *patch_bytes(int idx) { return rom + 0x2C248u + (uint32_t)idx * 32u; }   /* (fm_instrument_patches: bank 5, $C248 in the relay's window) */

static int8_t *wave(int idx, int k)
{
    if (!patch_wave[idx][0]) {
        int8_t *w = alloc(128 + 64 + 32 + 16 + 8);
        if (!w) return silence;
        int8_t *p = w;
        for (int j = 0; j < 5; j++) { fm_render(patch_bytes(idx), 128 >> j, p); patch_wave[idx][j] = p; p += 128 >> j; }
    }
    return patch_wave[idx][k];
}

/* the waveform length and Paula's period for a channel's block and F-number (64-bit divisions: done
 * once for each new frequency, by fm_pitch_of) */
static void fm_pitch(uint16_t a4a0, int *k, uint16_t *per)
{
    uint32_t fnum = a4a0 & 0x7FF, block = (a4a0 >> 11) & 7;
    if (!fnum) { *k = 0; *per = 65535; return; }
    /* the base frequency f = fnum 2^(block-1) FM_CLOCK / (144 2^20); two periods in L samples: the
     * rate is f L / 2, and the period PAULA_CLOCK / rate */
    for (int j = 0; j < 5; j++) {
        uint32_t L = 128u >> j;
        uint64_t num = (uint64_t)PAULA_CLOCK * 2u * 144u * (1u << 20);
        uint64_t den = (uint64_t)fnum * FM_CLOCK * L;
        if (block) den <<= (block - 1); else num <<= 1;
        uint64_t p = num / den;
        if (p >= 124 || j == 4) { *k = j; *per = (uint16_t)(p > 65535 ? 65535 : p < 124 ? 124 : p); return; }
    }
}

/* the envelope a pass: attenuation in 0.75 dB steps from the first carrier's rates (approximate: a
 * rate r of the chip's 64 takes about 6.7 ms 2^((60 - r) / 4) to cover 96 dB) */
static int env_step(int rate64)
{
    if (rate64 <= 0) return 0;
    if (rate64 >= 60) return 128;
    uint32_t t_ms10 = 67u << ((60 - rate64) / 4);         /* tenths of a ms */
    uint32_t steps = 128u * 167u / (t_ms10 ? t_ms10 : 1);  /* 128 steps of 0.75 dB over that time, a pass 16.7 ms */
    return steps ? (int)steps : 1;
}

static void fm_pitch_of(struct fm *c, int *k, uint16_t *per)
{
    if (c->pitch_for != c->a4a0 || !c->per) { fm_pitch(c->a4a0, &c->k, &c->per); c->pitch_for = c->a4a0; }
    *k = c->k;
    *per = c->per;
}

static void fm_envelope(struct fm *c)
{
    const uint8_t *p = patch_bytes(c->patch);
    int alg = p[28] & 7, op = 4;
    (void)alg;
    int ar = fm_reg(p, 2, op) & 31, d1r = fm_reg(p, 3, op) & 31, d2r = fm_reg(p, 4, op) & 31;
    int sl = fm_reg(p, 5, op) >> 4, rr = fm_reg(p, 5, op) & 15;
    int sustain = sl == 15 ? 124 : sl * 4;                /* (SL in 3 dB steps) */
    switch (c->phase) {
    case 0: c->env -= env_step(ar * 2) * 4; if (c->env <= 0 || ar >= 30) { c->env = 0; c->phase = 1; } break;
    case 1: c->env += env_step(d1r * 2); if (c->env >= sustain) { c->env = sustain; c->phase = 2; } break;
    case 2: c->env += env_step(d2r * 2); break;
    default: c->env += env_step(rr * 4 + 2); break;
    }
    if (c->env > 127) c->env = 127;
    if (c->env < 0) c->env = 0;
}

/* ---- PCM ------------------------------------------------------------------------------------------ */

/* a sample from a 24-bit cartridge address through the volume table, to its end (the table gives 0)
 * -> its bytes, signed, in sample memory */
static const int8_t *pcm_take(uint32_t addr, uint32_t *len)
{
    const uint8_t *tab = z80 + ((uint32_t)z80[0x83] << 8 & 0x1F00);
    uint32_t n = 0;
    while (addr + n < 0x100000u && n < 0x1FFF0u && tab[rom[addr + n]]) n++;
    n &= ~1u;
    if (n < 2) { *len = 4; return silence; }
    int8_t *s = alloc(n);
    if (!s) { chip_next = chip + (32 + 4096 + 4) ; memset(patch_wave, 0, sizeof patch_wave); s = alloc(n); }   /* (full: the cache starts again) */
    if (!s) { *len = 4; return silence; }
    for (uint32_t i = 0; i < n; i++) s[i] = (int8_t)(tab[rom[addr + i]] - 128);
    *len = n;
    return s;
}

void sound_restart(const uint8_t *z)
{
    z80 = z;
    uint32_t a = (uint32_t)z[0x23] << 16 | (uint32_t)z[0x24] << 8 | z[0x25];
    uint32_t e = (uint32_t)z[0x26] << 16 | (uint32_t)z[0x27] << 8 | z[0x28];
    pcm.start = pcm_take(a & 0xFFFFF, &pcm.start_len);
    pcm.loop = pcm_take(e & 0xFFFFF, &pcm.loop_len);
    /* the relay's loop: about 120 Z80 cycles a byte output, the rate byte's carries advancing the sample */
    uint32_t rate = z[0x8E] ? z[0x8E] : 1;
    uint32_t hz = 3579545u / 120u * rate / 256u;
    pcm.per = (uint16_t)(PAULA_CLOCK / (hz ? hz : 1));
    if (pcm.per < 124) pcm.per = 124;
    pcm.on = 1;
    pcm.fresh = 1;
}

void sound_reset(int running) { if (!running) pcm.on = 0; }

/* ---- the command ------------------------------------------------------------------------------------ */

void sound_command(const uint8_t *z)
{
    z80 = z;
    for (int e = 0; e < 7; e++) {
        if (e == 3) continue;
        struct fm *c = &fm[e];
        if (z[0x02] >> e & 1) { if (c->on) c->phase = 3; c->on = 0; }
        if (z[0x06] >> e & 1) c->patch = z[0x1C + e];
        if (z[0x05] >> e & 1) c->a4a0 = (uint16_t)(z[0x0E + 2 * e] << 8 | z[0x0F + 2 * e]);
        if (z[0x04] >> e & 1) c->att = z[0x07 + e] & 0x7F;
        if (z[0x03] >> e & 1) { c->on = 1; c->phase = 0; c->env = 127; c->age = ++now; c->voice = -2; }   /* (-2: wants a voice) */
    }
    if (z[0x03] & 0x40) pcm.on = 0;                       /* (channel 6 as FM: the DAC off) */
}

void sound_psg(uint8_t v)
{
    if (v & 0x80) {
        psg.latch = (v >> 4) & 7;
        int ch = psg.latch >> 1;
        if (psg.latch & 1) psg.att[ch] = v & 15;
        else psg.tone[ch] = (uint16_t)((psg.tone[ch] & 0x3F0) | (v & 15));
    } else if (!(psg.latch & 1)) {
        int ch = psg.latch >> 1;
        psg.tone[ch] = (uint16_t)((psg.tone[ch] & 15) | (v & 0x3F) << 4);
    }
}

/* ---- the voices, once a pass ----------------------------------------------------------------------------- */

static int owner[4];                                      /* what each voice plays: 1-7 FM channel + 1, 10-13 PSG, 20 PCM, 0 nothing */

static int fm_level(const struct fm *c) { int a = c->att + c->env; return a > 127 ? 0 : fm_vol[a]; }

void sound_frame(void)
{
    for (int i = 0; i < 4; i++) paula[i].restart = 0;
    /* PCM: channel 3 while it plays */
    if (pcm.on) {
        if (owner[3] && owner[3] != 20) {
            if (owner[3] <= 7) fm[owner[3] - 1].voice = -2; else if (owner[3] < 20) psg.voice[owner[3] - 10] = -1;
        }
        owner[3] = 20;
        if (pcm.fresh) {
            paula[3].ptr = pcm.start; paula[3].len = pcm.start_len;
            paula[3].loop = pcm.loop; paula[3].loop_len = pcm.loop_len;
            paula[3].per = pcm.per; paula[3].vol = 64; paula[3].restart = 1;
            pcm.fresh = 0;
        }
    } else if (owner[3] == 20) { owner[3] = 0; paula[3].vol = 0; }
    /* FM: envelopes, and voices for the key ons */
    for (int e = 0; e < 7; e++) {
        struct fm *c = &fm[e];
        if (e == 3 || (!c->on && c->phase != 3)) continue;
        fm_envelope(c);
        if (c->phase == 3 && c->env >= 127) { if (c->voice >= 0) { owner[c->voice] = 0; paula[c->voice].vol = 0; } c->voice = -1; c->phase = 4; continue; }
        if (c->voice == -2) {
            int v = -1, best = 999;
            for (int i = 0; i < 4; i++) {
                if (owner[i] == 20) continue;
                int lvl = !owner[i] ? -1 : owner[i] <= 7 ? fm_level(&fm[owner[i] - 1]) : 30;
                if (owner[i] == e + 1) lvl = -2;
                if (lvl < best) { best = lvl; v = i; }
            }
            if (v < 0) continue;
            if (owner[v] && owner[v] <= 7 && owner[v] != e + 1) fm[owner[v] - 1].voice = -1;
            else if (owner[v] >= 10 && owner[v] < 20) psg.voice[owner[v] - 10] = -1;
            owner[v] = e + 1;
            c->voice = v;
            int k; uint16_t per;
            fm_pitch_of(c, &k, &per);
            paula[v].ptr = paula[v].loop = wave(c->patch, k);
            paula[v].len = paula[v].loop_len = 128u >> k;
            paula[v].per = per;
            paula[v].restart = 1;
        } else if (c->voice >= 0) {
            int k; uint16_t per;
            fm_pitch_of(c, &k, &per);
            if (paula[c->voice].len != (128u >> k)) {     /* (into another octave's waveform) */
                paula[c->voice].ptr = paula[c->voice].loop = wave(c->patch, k);
                paula[c->voice].len = paula[c->voice].loop_len = 128u >> k;
                paula[c->voice].restart = 1;
            }
            paula[c->voice].per = per;
        }
        if (c->voice >= 0) paula[c->voice].vol = (uint16_t)fm_level(c);
    }
    /* the PSG: its audible channels on free voices */
    for (int ch = 0; ch < 4; ch++) {
        int audible = psg.att[ch] < 15 && (ch == 3 || psg.tone[ch] > 1);
        int v = psg.voice[ch];
        if (!audible) { if (v >= 0) { owner[v] = 0; paula[v].vol = 0; psg.voice[ch] = -1; } continue; }
        if (v < 0) {
            for (int i = 0; i < 4 && v < 0; i++) if (!owner[i]) v = i;
            if (v < 0) continue;
            owner[v] = 10 + ch;
            psg.voice[ch] = v;
            paula[v].ptr = paula[v].loop = ch == 3 ? noise : square;
            paula[v].len = paula[v].loop_len = ch == 3 ? 4096 : 32;
            paula[v].restart = 1;
        }
        /* a tone of N: 3579545 / (32 N) Hz; the square is 32 samples a period */
        uint32_t n = ch == 3 ? 16u << (psg.tone[3] & 3) : psg.tone[ch];
        uint32_t per = PAULA_CLOCK / (3579545u / (32u * (n ? n : 1)) * 32u);
        paula[v].per = (uint16_t)(per < 124 ? 124 : per > 65535 ? 65535 : per);
        paula[v].vol = (uint16_t)(64 >> (psg.att[ch] / 3));   /* (2 dB steps: about a halving every 3) */
    }
}
