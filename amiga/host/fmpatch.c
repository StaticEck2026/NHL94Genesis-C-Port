/* One of the game's FM patches as a looped waveform: see fmpatch.h. A four-operator model of the
 * YM2612's connections (the eight algorithms and operator 1's feedback), each operator a sine at its
 * multiple of the channel's frequency, phase-modulated by the operators before it. Integer only: it
 * runs on the 68020, when the program starts, from the user's own cartridge (no rendered sound is
 * ever on a disk). What it leaves out: the envelopes' effect on the timbre over time (the modulators
 * stay at their total level), detune, the LFO and SSG-EG. The volume and the envelope of the carriers
 * are the driver's, a step a pass (amiga/host/sound.c). */
#include "fmpatch.h"
#include "fmtab.h"

int fm_carriers(int alg) { return alg < 4 ? 1 : alg < 5 ? 2 : alg < 7 ? 3 : 4; }
int fm_is_carrier(int alg, int op)
{
    switch (alg & 7) {
    case 7: return 1;
    case 5: case 6: return op >= 2;
    case 4: return op == 2 || op == 4;
    default: return op == 4;
    }
}

static inline int16_t op_out(uint32_t phase /* 16.16, 1024 steps a cycle in the high part */, int32_t mod, uint16_t gain)
{
    int idx = (int)(((phase >> 16) + (uint32_t)(mod >> 1)) & 1023);
    return (int16_t)(((int32_t)fm_sine[idx] * gain) >> 15); /* (+-8191) */
}

int fm_render(const uint8_t *patch, int len, int8_t *out)
{
    int alg = patch[28] & 7, fb = (patch[28] >> 3) & 7;
    uint32_t inc[5];
    uint16_t gain[5];
    for (int op = 1; op <= 4; op++) {
        int mul = fm_reg(patch, 0, op) & 15;
        /* two periods of the base (MUL 1) frequency in len samples: a MUL 1 operator steps 2048 / len a sample */
        uint32_t base = (uint32_t)((2048u << 16) / (unsigned)len);
        inc[op] = mul ? base * (uint32_t)mul : base / 2;
        int tl = fm_reg(patch, 1, op) & 0x7F;
        gain[op] = fm_is_carrier(alg, op) ? 16384 : fm_lin[tl * 8];   /* (TL steps are 0.75 dB: 8 envelope units) */
    }
    uint32_t ph[5] = { 0, 0, 0, 0, 0 };
    int32_t o1a = 0, o1b = 0, peak = 0;
    static int32_t acc[256];
    /* a lap first, so the feedback starts where it settles, then the lap kept */
    for (int lap = 0; lap < 2; lap++)
        for (int n = 0; n < len; n++) {
            int32_t fbmod = fb ? (o1a + o1b) >> (10 - fb) : 0;
            int32_t s1 = op_out(ph[1], fbmod << 1, gain[1]);
            o1b = o1a; o1a = s1;
            int32_t s2, s3, s4, sum;
            switch (alg) {
            case 0: s2 = op_out(ph[2], s1, gain[2]); s3 = op_out(ph[3], s2, gain[3]); s4 = op_out(ph[4], s3, gain[4]); sum = s4; break;
            case 1: s2 = op_out(ph[2], 0, gain[2]); s3 = op_out(ph[3], s1 + s2, gain[3]); s4 = op_out(ph[4], s3, gain[4]); sum = s4; break;
            case 2: s2 = op_out(ph[2], 0, gain[2]); s3 = op_out(ph[3], s2, gain[3]); s4 = op_out(ph[4], s1 + s3, gain[4]); sum = s4; break;
            case 3: s2 = op_out(ph[2], s1, gain[2]); s3 = op_out(ph[3], 0, gain[3]); s4 = op_out(ph[4], s2 + s3, gain[4]); sum = s4; break;
            case 4: s2 = op_out(ph[2], s1, gain[2]); s3 = op_out(ph[3], 0, gain[3]); s4 = op_out(ph[4], s3, gain[4]); sum = s2 + s4; break;
            case 5: s2 = op_out(ph[2], s1, gain[2]); s3 = op_out(ph[3], s1, gain[3]); s4 = op_out(ph[4], s1, gain[4]); sum = s2 + s3 + s4; break;
            case 6: s2 = op_out(ph[2], s1, gain[2]); s3 = op_out(ph[3], 0, gain[3]); s4 = op_out(ph[4], 0, gain[4]); sum = s2 + s3 + s4; break;
            default: s2 = op_out(ph[2], 0, gain[2]); s3 = op_out(ph[3], 0, gain[3]); s4 = op_out(ph[4], 0, gain[4]); sum = s1 + s2 + s3 + s4; break;
            }
            for (int op = 1; op <= 4; op++) ph[op] += inc[op];
            if (lap) { acc[n] = sum; if (sum > peak) peak = sum; if (-sum > peak) peak = -sum; }
        }
    /* to 8 bits, the peak at 127 (the level is the driver's) */
    for (int n = 0; n < len; n++) out[n] = peak ? (int8_t)((acc[n] * 127) / peak) : 0;
    return peak;
}
