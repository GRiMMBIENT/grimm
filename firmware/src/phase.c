/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's PHASE source: Felucca's PHASE engine (hugelton/Felucca firmware/src/eng_phase.c), phase distortion in the
 * manner of Casio's CZ. Its waveforms are a C port of the oscillator of CrispyZebra (Leo Kuroshita, GPL-3.0,
 * github.com/hugelton/CrispyZebra), the author's own phase-distortion core: a -cos table read through a bent phase
 * whose bend is DCW. WAVE2 alternates with WAVE every other cycle; a second line DETUNE cents up is mixed in. Taken
 * over: pd_cos, pd_setup, pd_wave, the render loop. Not taken: ENV -> DCW (no envelope of its own yet), the ring
 * modulation (LINE: MIX), SUB.
 * GR!MM's: the four knobs, the level (as VA's), the engine interface.
 *   WAVE    SAW, SQR, PLS, DSIN, SPLS, RSAW, RTRI, RTRP (the last three: CZ's resonance waves)
 *   WAVE2   -, or a wave every other cycle
 *   DCW     the bend: a sine at 0 .. the wave at the top
 *   DETUNE  a second line DETUNE cents up, mixed in (0: one line)
 * Voice state: ph[0..1] the lines' phases, s[0..1] their WAVE / WAVE2 toggles. */
static const char *const N_PD_WAVE[] = {"SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};
static const char *const N_PD_WAVE2[] = {"-", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};

static inline int32_t pd_cos(uint32_t ph16)             /* -cos, Q15, from the 16-bit PD phase */
{
    return -sine_i(((ph16 & 0xFFFFu) << 16) + 0x40000000u);
}

/* one wave's bend for a block: break points and the slopes of the bent phase
 * (Q16 reciprocals), so a sample costs a multiply instead of a divide (the
 * bent phase comes out the same or 1 lower in 65536) */
typedef struct {
    uint32_t w, x, peak, rez;
    uint32_t k0, k1;
} pd_t;

static inline uint32_t pd_slope(uint32_t span, uint32_t len) { return (span << 16) / (len ? len : 1u); }

static void pd_setup(pd_t *b, uint32_t w, uint32_t dcw)
{
    uint32_t x;
    b->w = w;
    switch (w) {
    case 0:                                                  /* SAW */
        x = 32768u + ((dcw * 32767u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, 65535u - x);
        break;
    case 1:                                                  /* SQUARE */
        x = 32768u - ((dcw * 31120u) >> 16);
        b->k0 = pd_slope(32768u, x);
        b->k1 = pd_slope(32767u, x);
        break;
    case 2:                                                  /* PULSE */
        x = 65535u - ((dcw * 63487u) >> 16);
        b->k0 = pd_slope(65535u, x);
        break;
    case 3:                                                  /* DOUBLE SINE */
        x = 65535u - ((dcw * 49151u) >> 16);
        b->k0 = pd_slope(65535u, x);
        b->k1 = pd_slope(65535u, 65535u - x);
        break;
    case 4:                                                  /* SAW-PULSE */
        x = 65535u - ((dcw * 32767u) >> 16);
        b->peak = (x * (32768u + ((dcw * 29491u) >> 16))) >> 16;
        if (!b->peak)
            b->peak = 1;
        b->k0 = pd_slope(32768u, b->peak);
        b->k1 = pd_slope(32767u, x - b->peak);
        break;
    default:                                                 /* RESONANCE */
        x = 0;
        b->rez = (dcw * 44000u) >> 16;
        break;
    }
    b->x = x;
}

/* one sample at phase ph (16 bit): bipolar Q15 */
static int32_t pd_wave(const pd_t *b, uint32_t ph)
{
    uint32_t pd, x = b->x;
    switch (b->w) {
    case 0:                                                  /* SAW */
        pd = ph < x ? (ph * b->k0) >> 16 : 32768u + (((ph - x) * b->k1) >> 16);
        break;
    case 1:                                                  /* SQUARE */
        if (ph < x)
            pd = (ph * b->k0) >> 16;
        else if (ph < 32768u)
            pd = 32768u;
        else if (ph < 32768u + x)
            pd = 32768u + (((ph - 32768u) * b->k1) >> 16);
        else
            pd = 65535u;
        break;
    case 2:                                                  /* PULSE */
        pd = ph < x ? (ph * b->k0) >> 16 : 65535u;
        break;
    case 3:                                                  /* DOUBLE SINE */
        pd = ph < x ? (ph * b->k0) >> 16 : ((ph - x) * b->k1) >> 16;
        break;
    case 4:                                                  /* SAW-PULSE */
        if (ph >= x)
            pd = 65535u;
        else if (ph < b->peak)
            pd = (ph * b->k0) >> 16;
        else
            pd = 32768u + (((ph - b->peak) * b->k1) >> 16);
        break;
    default: {                                               /* RESONANCE: windowed sine, rez = pitch of the core */
        uint32_t rp = (ph + ((ph * b->rez) >> 16) * 7u) & 0xFFFFu, win;
        int32_t core = (pd_cos(rp) * 5) >> 3;
        if (b->w == 5)
            win = 65535u - ph;                               /* saw window */
        else if (b->w == 6)
            win = ph < 32768u ? ph << 1 : (65535u - ph) << 1; /* triangle window */
        else
            win = ph < 16384u ? ph << 2 : ph > 49152u ? (65535u - ph) << 2 : 65535u;   /* trapezoid */
        if (win > 65535u)
            win = 65535u;
        return (core * (int32_t)win) >> 16;                  /* |core| <= 20479: fits 32 bits */
    }
    }
    return pd_cos(pd);
}

static void phase_begin(voice_t *v)             /* a note from silence (phase_note_on): the lines from the start */
{
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = 0;                                /* WAVE / WAVE2 toggle, line 1 */
    v->s[1] = 0;                                /* line 2 */
}

/* CTL frames of voice v, added to mono[] (phase_render; line 2 mixed, no SUB) */
static void phase_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    uint32_t w1 = (uint32_t)clamp(p[0], 0, 7), w2 = (uint32_t)clamp(p[1], 0, 8), i;
    int32_t depth = clamp(p[2], 0, 127) << 8, pitch = VP16(v), damp = amp1 - amp0;
    uint32_t dcw, inc = vinc(pitch), inc2;
    int32_t det = clamp(p[3], 0, 127), line2 = det != 0;
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;   /* (cents_inc(), written out) */
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];
    int32_t tg0 = v->s[0], tg1 = v->s[1];       /* WAVE / WAVE2 toggles */
    pd_t b1, b2;
    dcw = (uint32_t)depth * 65535u / (127u << 8);
    dcw = (dcw * 56000u) >> 16;                 /* the classic range of the bend */
    inc2 = vinc(pitch + d16);
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    pd_setup(&b1, w1, dcw);
    pd_setup(&b2, w2 ? w2 - 1u : w1, dcw);      /* WAVE2 (every other cycle) */
    for (i = 0; i < (uint32_t)CTL; i++) {
        uint32_t old = ph0;
        int32_t s;
        s = pd_wave(tg0 ? &b2 : &b1, ph0 >> 16);
        ph0 += inc;
        if (ph0 < old)
            tg0 ^= 1;
        if (line2) {
            uint32_t old2 = ph1;
            int32_t s2 = pd_wave(tg1 ? &b2 : &b1, ph1 >> 16);
            ph1 += inc2;
            if (ph1 < old2)
                tg1 ^= 1;
            s = (s + s2) >> 1;                  /* both within +-32767 */
        }
        mono[i] += mulq15(mulq15(s, amp_at(amp0, damp, (int32_t)i)), VOICE_LV);
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = tg0;
    v->s[1] = tg1;
}

static void phase_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = p[k];
    switch (k) {
    case 0: put_s(s, N_PD_WAVE[clamp(v, 0, 7)]); return;
    case 1: put_s(s, N_PD_WAVE2[clamp(v, 0, 8)]); return;
    case 2: put_s(put_u(s, ((uint32_t)clamp(v, 0, 127) * 100u + 63u) / 127u), "%"); return;
    default: if (v) put_s(put_u(s, (uint32_t)clamp(v, 0, 127)), " ct"); else put_s(s, "OFF"); return;
    }
}

static const engine_t PHASE_ENGINE = {"PHASE", {"WAVE", "WAVE2", "DCW", "DETUNE"}, {7, 8, 127, 127},
                                      phase_begin, phase_block, phase_text};
