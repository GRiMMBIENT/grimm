/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's VOICE source: Felucca's VOICE engine (hugelton/Felucca firmware/src/eng_formant.c), a formant oscillator,
 * sung vowels. After D. H. Klatt, "Software for a cascade/parallel formant synthesizer", JASA 67 (1980): a glottal
 * source (derivative of a Rosenberg (1971) pulse, no DC) into four two-pole resonators in cascade, F1 -> F2 -> F3 ->
 * F4. Felucca's design follows klattsch by Tony Gies (MIT, design reference only, no code copied); formant data from
 * Klatt (1980) and Hillenbrand et al. (1995). Taken over: the source, BUZZ, BREATH, the resonators, SHIFT and the
 * fixed key tracking, the presence shelf and the level, sine_i and noise32 (Felucca's dsp.c). Not taken (Felucca's
 * defaults: off): VOWL2 and TALK, RAND, the modulation; Q at its default (64).
 * GR!MM's: the four knobs, F1's bandwidth following F0 with a 32-bit division (Felucca: a 64-bit one), the state
 * in vx_s.
 *   VOWEL   A -> E -> I -> O -> U, gliding between them
 *   SHIFT   all formants -12 .. +12 semitones (a smaller or a larger throat)
 *   BUZZ    the source: open quotient 0.9 .. 0.35 and its tilt (soft .. bright, buzzy)
 *   BREATH  noise from the glottis; at the top the voicing fades out (a whisper)
 * (V2) TYPE (the track's vty, on SOURCE): SOLO, one voice as above; GREGORIAN, NUNS, CATHEDRAL, a choir a note of
 * real singers (choir_block): single cycles of VocalSet's singers (tools/gen_choir.py: men at C3 and C4, women at C4
 * and C5, A E I O U), six a note, each a wavetable of its own singer, a little detuned, a slow random drift,
 * a vibrato and a slower chorus sway of its own; VOWEL morphs between the vowels' cycles, SHIFT spreads the singers
 * (12: as set; 0: in unison; 24: twice as wide), BUZZ is the tone (dark .. open), BREATH breath noise (at the top
 * the singing fades into a whisper, as SOLO's).
 * Voice state: ph[0] glottal phase, ph[1..2] F4 y1 / y2; vx_s: F1..F3 y1 / y2, the tilt; vx_c: a choir's. */

#include "grimm_choir.h"                       /* (V2) tools/gen_choir.py: CHOIR_T, CHOIR_NS, CHOIR_LEN */

#define FMT_FOLOW 30                            /* formant key tracking: 30 / 127 (Felucca's) */

static inline int32_t sine_i(uint32_t ph)       /* sin, Q15, a full cycle over 2^32 (Felucca's dsp.c, SINE[1025]) */
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return a + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static inline uint32_t noise32(int32_t *st)     /* xorshift32 (Felucca's dsp.c) */
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}

/* formants (Hz) of A E I O U: adult male means, Hillenbrand et al., JASA 97 (1995) (/a/ hod, /E/ head, /i/ heed,
 * /o/ hoed, /u/ who'd); bandwidths (Hz) in the range of Klatt (1980) and of measured vowel bandwidths */
static const uint16_t VOWEL_F[5][3] = {
    {768, 1333, 2522}, {580, 1799, 2605}, {342, 2322, 3000}, {497, 910, 2459}, {378, 997, 2343},
};
static const uint8_t VOWEL_B[5][3] = {
    {90, 100, 160}, {70, 100, 170}, {50, 120, 200}, {70, 80, 150}, {60, 80, 150},
};
/* 2^(k/12), Q16, k = 0..12 */
static const uint32_t SEMI_Q16[13] = {
    65536, 69433, 73562, 77936, 82570, 87480, 92682, 98193, 104032, 110218, 116772, 123715, 131072,
};

/* 2^(st16 / 192) in Q16 (st16 in 1/16 semitone), clamped to 1/16 .. 16 */
static uint32_t fmt_ratio(int32_t st16)
{
    int32_t o, k;
    uint32_t r;
    st16 = clamp(st16, -4 * 192, 4 * 192 - 1) + 4 * 192;   /* 0 .. 8 octaves */
    o = (st16 * 2731) >> 19;                            /* / 192, exact for 0 .. 1535 */
    k = st16 - o * 192;
    r = SEMI_Q16[k >> 4] + (((SEMI_Q16[(k >> 4) + 1] - SEMI_Q16[k >> 4]) * (uint32_t)(k & 15)) >> 4);
    return o >= 4 ? r << (o - 4) : r >> (4 - o);
}

/* the three formants of vowel position pos (0..127 << 8, A E I O U), Hz * 16 */
static void vowel_at(int32_t pos, int32_t *f, int32_t *b)
{
    int32_t x = (clamp(pos, 0, 127 << 8) * 33027) >> 20, i = x >> 8, fr = x & 255, j;   /* * 4 / 127 */
    if (i >= 4) {
        i = 3;
        fr = 256;
    }
    for (j = 0; j < 3; j++) {
        f[j] = (VOWEL_F[i][j] << 4) + (((VOWEL_F[i + 1][j] - VOWEL_F[i][j]) * fr) >> 4);
        b[j] = (VOWEL_B[i][j] << 4) + (((VOWEL_B[i + 1][j] - VOWEL_B[i][j]) * fr) >> 4);
    }
}

typedef struct { int32_t a, b, c; } fres_t;
static int32_t formant_nz = 0x2545F491;         /* breath noise, one xorshift for all voices */
static int32_t vx_s[NV][7];                     /* F1..F3 y1 / y2, the tilt */

/* Klatt resonator coefficients (Q30) for F, BW in Hz * 16 */
static void fres_coef(fres_t *r, uint32_t f16, uint32_t bw16)
{
    int32_t s = sine_i(f16 * 3044u);            /* sin(pi F / FS), Q15 (2^32 / (2 FS) / 16) */
    int32_t x = (int32_t)(bw16 * 4781u);        /* pi BW / FS, Q30 (<= 0.2) */
    int32_t x2 = (int32_t)(((int64_t)x * x) >> 30), x3 = (int32_t)(((int64_t)x2 * x) >> 30);
    int32_t x4 = (int32_t)(((int64_t)x3 * x) >> 30);
    int32_t e = (1 << 30) - x + (x2 >> 1) - (int32_t)(((int64_t)x3 * 178956971) >> 30)   /* exp(-x): x^3 / 6, */
                + (int32_t)(((int64_t)x4 * 44739243) >> 30);                            /* x^4 / 24 */
    int32_t cs = (1 << 30) - 2 * s * s;         /* cos(2 pi F / FS) = 1 - 2 sin^2, Q30 */
    r->b = (int32_t)(((int64_t)e * cs) >> 29);  /* 2 r cos */
    r->c = -(int32_t)(((int64_t)e * e) >> 30);  /* -r^2 */
    r->a = (1 << 30) - r->b - r->c;
}

/* (V2) the choirs: their singers (VT_SING at most), which are women (bit k) and sing an octave up (bit k), each
 * singer's detune (cents, at SHIFT 12), the vibrato (cents), the drift (cents) */
enum { VT_SOLO, VT_GREG, VT_NUNS, VT_CATH, NVT };
static const char *const VT_NAME[NVT] = {"SOLO", "GREGORIAN", "NUNS", "CATHEDRAL"};
#define VT_SING 6u                              /* (six: the most the FM-1's time has for six voices of them, VOWEL
                                                 * between two vowels, and the effects) */
static const struct {
    uint8_t n, women, oct;
    int8_t det[VT_SING];
    uint8_t vib, drift;
} VT[NVT] = {
    {0, 0, 0, {0}, 0, 0},
    {6, 0x00, 0x00, {-9, -5, -2, 2, 5, 9}, 4, 6},  /* GREGORIAN: six men (the three recorded, twice), little vibrato */
    {6, 0x3F, 0x00, {-8, -5, -2, 2, 5, 8}, 12, 4}, /* NUNS: six women, a gentle vibrato */
    {6, 0x2A, 0x2A, {-7, -6, -3, 3, 6, 7}, 9, 5},  /* CATHEDRAL: the three men, the three women an octave over them */
};
static const uint32_t VIB_INC[VT_SING] = {16517421u, 17764019u, 15582473u, 18387318u, 14958174u, 17140708u};   /* a
                                                 * control step's: 5.3, 5.7, 5.0, 5.9, 4.8, 5.5 Hz (2^32 x CTL / 44100
                                                 * a Hz) */
static const uint16_t CH_OFF[4] = {0, 256, 384, 448};   /* a cycle's sizes in CHOIR_T: 256, 128, 64, 32 */
static struct {
    uint32_t ph[VT_SING], vp[VT_SING], cp[VT_SING];   /* each singer's cycle's phase, its vibrato's, its sway's */
    int32_t dr[VT_SING], dt[VT_SING], nst, lp, nl;   /* its drift now, where it goes (cents x 256); the drift's
                                                 * random; the tone's low-pass, the breath's */
    uint8_t lv[VT_SING];                        /* its cycle's size (CH_OFF's; 255: none yet) */
} vx_c[NV];

static void formant_begin(voice_t *v)           /* a note from silence: the opening starts at zero (no click) */
{
    uint32_t i, n = (uint32_t)(v - vc);
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    for (i = 0; i < 7u; i++)
        vx_s[n][i] = 0;
    vx_c[n].nst = (int32_t)(0x9E3779B9u ^ (n * 0x85EBCA6Bu) ^ (uint32_t)formant_nz) | 1;
    for (i = 0; i < VT_SING; i++) {             /* (V2: the singers out of step, their vibratos, sways too) */
        vx_c[n].ph[i] = noise32(&vx_c[n].nst), vx_c[n].vp[i] = noise32(&vx_c[n].nst), vx_c[n].cp[i] = noise32(&vx_c[n].nst);
        vx_c[n].dr[i] = vx_c[n].dt[i] = 0;
    }
    vx_c[n].lp = vx_c[n].nl = 0;
    for (i = 0; i < VT_SING; i++) vx_c[n].lv[i] = 255u;
}

/* the four resonators of VOWEL, SHIFT (+ sh, 1/16 semitones: a choir's men or women) for a voice at pitch (inc its
 * phase increment), and the output's gain og (and its presence, ogk) at BUZZ buzz */
static void fmt_set(fres_t *r, int32_t *og_, int32_t *ogk_, const volatile int32_t *p, int32_t pitch, uint32_t inc,
                    int32_t sh, int32_t buzz)
{
    int32_t fa[3], ba[3], j, og;
    uint32_t ratio, f0;
    vowel_at(clamp(p[0], 0, 127) << 8, fa, ba);
    /* SHIFT (semitones) and the key tracking around C4 (fixed), one ratio for all four; Q at 64: the bandwidths as
     * they are */
    ratio = fmt_ratio((clamp(p[1], 0, 24) - 12) * 16 + sh + ((FMT_FOLOW * (pitch - 60 * 16) * 516) >> 16));
    f0 = (uint32_t)(((uint64_t)inc * 705600u) >> 32);   /* F0 in Hz * 16 */
    for (j = 0; j < 3; j++) {
        int32_t f = (int32_t)(((int64_t)fa[j] * ratio) >> 16), b = (int32_t)(((int64_t)ba[j] * ratio) >> 16);
        if (j == 0 && (uint32_t)f < f0 + (f0 >> 4) && f > 0) {   /* sung high: F1 follows F0 (sopranos do), */
            uint32_t q = ((f0 + (f0 >> 4)) << 8) / (uint32_t)f;   /* same Q: no boom on the fundamental (x 256) */
            b = (int32_t)(((int64_t)b * q) >> 8);
            f = (int32_t)(f0 + (f0 >> 4));
        }
        fa[j] = clamp(f, 60 * 16, 7000 * 16);
        ba[j] = clamp(b, 20 * 16, 1500 * 16);
    }
    fres_coef(&r[0], (uint32_t)fa[0], (uint32_t)ba[0]);
    fres_coef(&r[1], (uint32_t)fa[1], (uint32_t)ba[1]);
    fres_coef(&r[2], (uint32_t)fa[2], (uint32_t)ba[2]);
    fres_coef(&r[3], (uint32_t)clamp((int32_t)((3300u * 16u * (uint64_t)ratio) >> 16), 60 * 16, 7000 * 16),
              (uint32_t)clamp((int32_t)((250u * 16u * (uint64_t)ratio) >> 16), 20 * 16, 1500 * 16));
    og = 13000;                                 /* output (Q at 64) */
    og = (int32_t)(((int64_t)og * fmt_ratio((60 * 16 - pitch) / 4)) >> 16);   /* 1.5 dB / oct: low notes are sparse */
    *og_ = og, *ogk_ = (og >> 2) * (4 + (buzz >> 10));   /* presence k = 1 .. 4.9 */
}
static void choir_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1, uint32_t ty);

/* CTL frames of voice v, added to mono[] (eng_formant.c formant_render) */
static void formant_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    int32_t *st = vx_s[v - vc], pitch = VP16(v), damp = amp1 - amp0;
    fres_t rr[4];
    int32_t buzz, oq, tilt, gv, gn, og, ogk, br = clamp(p[3], 0, 127);
    uint32_t inc = vinc(pitch), i, te, tp, rp, rn, ty = (uint32_t)trk[v->trk & 3u].vty;
    uint32_t ph = v->ph[0];
    int32_t y1 = st[0], y2 = st[1], y3 = st[2], y4 = st[3], y5 = st[4], y6 = st[5];
    int32_t y7 = (int32_t)v->ph[1], y8 = (int32_t)v->ph[2], lp = st[6], nst = formant_nz;
    fres_t r1, r2, r3, r4;

    if (ty != VT_SOLO && ty < NVT) { choir_block(v, p, mono, amp0, amp1, ty); return; }   /* (V2: a choir) */
    /* the source: BUZZ -> open quotient and tilt */
    buzz = clamp(p[2], 0, 127) << 8;
    fmt_set(rr, &og, &ogk, p, pitch, inc, 0, buzz);
    r1 = rr[0], r2 = rr[1], r3 = rr[2], r4 = rr[3];
    oq = 58982 - ((buzz * 284) >> 8);           /* Q16: 0.90 .. 0.35 */
    tilt = 2500 + ((buzz * 109) >> 8);          /* one-pole coefficient Q14: dark .. open */
    te = (uint32_t)oq << 16;                    /* closure (end of the open phase) */
    tp = (te >> 8) * 166u;                      /* opening: 65 % of it, closing 35 % */
    rp = (1u << 30) / ((tp >> 16) | 1u);
    rn = (1u << 30) / (((te - tp) >> 16) | 1u);
    gv = 32767 - br * br * 2;                   /* BREATH: voicing fades at the top */
    gn = br * 258;                              /* noise: Q15, up to 1.0 */

    for (i = 0; i < (uint32_t)CTL; i++) {
        int32_t e, x, a, s;
        if (ph < tp)                            /* opening: + sin(pi u) * Tn / Tp */
            e = (sine_i(((ph >> 16) * rp) << 1) * 17644) >> 15;
        else if (ph < te)                       /* closing: - sin(pi / 2 u) */
            e = -sine_i(((ph - tp) >> 16) * rn);
        else
            e = 0;
        e += blep(ph - te, inc) >> 1;           /* the closure step, band-limited */
        lp += ((e - lp) * tilt) >> 14;
        x = (lp * gv) >> 15;
        if (gn) {
            int32_t nz = (((int32_t)(noise32(&nst) >> 16) - 32768) * gn) >> 15;
            x += ph < te ? nz : nz >> 1;        /* aspiration follows the glottal opening */
        }
        /* F1 -> F2 -> F3 -> F4, Q30, rounded; the signal x 64 inside */
        a = (int32_t)(((int64_t)r1.a * (x << 6) + (int64_t)r1.b * y1 + (int64_t)r1.c * y2 + (1 << 29)) >> 30);
        y2 = y1;
        y1 = a;
        a = (int32_t)(((int64_t)r2.a * a + (int64_t)r2.b * y3 + (int64_t)r2.c * y4 + (1 << 29)) >> 30);
        y4 = y3;
        y3 = a;
        a = (int32_t)(((int64_t)r3.a * a + (int64_t)r3.b * y5 + (int64_t)r3.c * y6 + (1 << 29)) >> 30);
        y6 = y5;
        y5 = a;
        a = (int32_t)(((int64_t)r4.a * a + (int64_t)r4.b * y7 + (int64_t)r4.c * y8 + (1 << 29)) >> 30);
        y8 = y7;
        y7 = clamp(a, -(1 << 28), 1 << 28);     /* int32 state stays far from overflow */
        s = soft_knee(clamp((int32_t)(((int64_t)y7 * og + (int64_t)(y7 - y8) * ogk) >> 21), -200000, 200000), 24000);
        ph += inc;
        mono[i] += mulq15(mulq15(s, amp_at(amp0, damp, (int32_t)i)), VOICE_LV);   /* (Felucca: s x 2 at its level) */
    }
    v->ph[0] = ph;
    v->ph[1] = (uint32_t)y7;
    v->ph[2] = (uint32_t)y8;
    formant_nz = nst;
    st[0] = y1, st[1] = y2, st[2] = y3, st[3] = y4, st[4] = y5, st[5] = y6, st[6] = lp;
}

/* (V2) CTL frames of a choir (TYPE ty) on voice v: VT's singers, each a cycle of its recorded singer (k mod CHOIR_NS
 * of the men or the women) at the note it sings (the lower below F#3, F#4: the cycle sung nearest), as large as its
 * pitch allows (band-limited), morphed between VOWEL's two vowels; its pitch this step: detune, drift (a new aim now and
 * then, glided to), sway (~0.3 Hz: the chorus), all times SHIFT's spread, and vibrato; summed (1 / sqrt(n)), the
 * breath noise in, the tone's low-pass (BUZZ: open at the top) */
static void choir_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1, uint32_t ty)
{
    static struct {                             /* (static: the audio ISR's alone; a fixed address, quicker) */
        const int8_t *t0, *t1;                  /* its cycle: VOWEL's two vowels' */
        uint32_t ph, inc, sh, sf, mk;           /* its phase, its step; the index's shift, the fraction's, the mask */
    } sg[VT_SING];
    typeof(&sg[0]) q, qe;
    uint32_t n = (uint32_t)(v - vc), ns = VT[ty].n, i, k;
    int32_t pitch = VP16(v), damp = amp1 - amp0, br = clamp(p[3], 0, 127), sp = clamp(p[1], 0, 24);
    int32_t pos = (clamp(p[0], 0, 127) * 4 * 256) / 127, vi = pos >> 8, mf = pos & 255, l = vx_c[n].lp, nl = vx_c[n].nl;
    int32_t tc = clamp(1500 + clamp(p[2], 0, 127) * clamp(p[2], 0, 127) * 2, 0, 32767), gv = 32767 - br * br * 2, gn = br * br >> 1;   /*
                                                 * (BUZZ: ~330 Hz, 2.5 kHz at 64, 4.7 at 85, open at the top) */
    int32_t g = 13377, nst = formant_nz;        /* (1 / sqrt(6)) */
    if (vi >= 4) vi = 3, mf = 256;
    if (mf < 8) mf = 0;                         /* (on a vowel, near enough: its cycle alone, half the reading) */
    else if (mf > 248) vi++, mf = 0;
    for (k = 0, q = sg; k < ns; k++, q++) {
        uint32_t w = VT[ty].women >> k & 1u, rv = noise32(&vx_c[n].nst), lv, b;
        int32_t pk = pitch + (VT[ty].oct >> k & 1u ? 192 : 0), z = pk >= (w ? 66 * 16 : 54 * 16), d;
        const int8_t *c = CHOIR_T[w][k % CHOIR_NS][z][0];
        if (!(rv & 511u)) vx_c[n].dt[k] = ((int32_t)(rv >> 16 & 255u) - 128) * VT[ty].drift * 2;
        vx_c[n].dr[k] += (vx_c[n].dt[k] - vx_c[n].dr[k]) >> 8;
        vx_c[n].vp[k] += VIB_INC[k];
        vx_c[n].cp[k] += VIB_INC[k] >> 4;       /* (the sway: a sixteenth of its vibrato's rate) */
        d = ((VT[ty].det[k] * 256 + vx_c[n].dr[k] + ((sine_i(vx_c[n].cp[k]) * 4) >> 7)) * sp) / 12 +
            ((sine_i(vx_c[n].vp[k]) * VT[ty].vib) >> 7);   /* (cents x 256) */
        b = vinc(pk);
        q->inc = b + (uint32_t)(int32_t)(((int64_t)b * (d * 38)) >> 24);   /* (2^(c / 1200) ~ 1 + c x 0.000578) */
        for (lv = 0; lv < 3u && (b >> 20) * (64u >> lv) > 1858u; lv++)   /* (its harmonics under ~20 kHz at its
                                                 * note: its vibrato and drift, a few cents, stay under 22 kHz) */
            ;
        if (lv < vx_c[n].lv[k] && vx_c[n].lv[k] < 4u && ((b + (b >> 5)) >> 20) * (64u >> lv) > 1858u)
            lv = vx_c[n].lv[k];                 /* (the larger cycle only once the note is clearly under its edge: a
                                                 * pitch moving at the edge, an LFO's, doesn't switch to and fro) */
        vx_c[n].lv[k] = (uint8_t)lv;
        q->t0 = c + vi * CHOIR_LEN + CH_OFF[lv], q->t1 = mf ? c + (vi + 1) * CHOIR_LEN + CH_OFF[lv] : q->t0;
        q->sh = 24u + lv, q->sf = 16u + lv, q->mk = (256u >> lv) - 1u, q->ph = vx_c[n].ph[k];
    }
    qe = sg + ns;
    for (i = 0; i < (uint32_t)CTL; i++) {
        int32_t acc = 0, x, nz;
        for (q = sg; q != qe; q++) {
            uint32_t ph = q->ph, j = ph >> q->sh, j1 = (j + 1u) & q->mk;
            int32_t fr = (int32_t)(ph >> q->sf & 255u), a0 = q->t0[j], a = (a0 << 8) + (q->t0[j1] - a0) * fr;
            if (mf) {                           /* (between two vowels: the other's too, morphed) */
                int32_t e0 = q->t1[j], e = (e0 << 8) + (q->t1[j1] - e0) * fr;
                a += ((e - a) * mf) >> 8;
            }
            acc += a;
            q->ph = ph + q->inc;
        }
        x = mulq15(acc, g) << 1;                /* (Q8 x 127 a singer, n of them: to about SOLO's level; six peaks
                                                 * together: ~100000, so the products after it in 64 bits) */
        x = (int32_t)(((int64_t)x * gv) >> 15);
        if (gn) {                               /* (the breath: noise less its low-pass, airy) */
            nz = (int32_t)(noise32(&nst) >> 16) - 32768;
            nl += (nz - nl) >> 3;
            x += ((nz - nl) * gn) >> 13;
        }
        l += (int32_t)(((int64_t)(x - l) * tc) >> 15);   /* (BUZZ: the tone) */
        mono[i] += mulq15((int32_t)(((int64_t)l * amp_at(amp0, damp, (int32_t)i)) >> 15), VOICE_LV);
    }
    for (k = 0, q = sg; k < ns; k++, q++) vx_c[n].ph[k] = q->ph;
    vx_c[n].lp = l, vx_c[n].nl = nl;
    formant_nz = nst;
}

static void formant_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    static const char VOW[5] = {'A', 'E', 'I', 'O', 'U'};
    int32_t v = p[k], x;
    switch (k) {
    case 0:                                     /* the vowel, or the two it is between: "A", "A-E", "E" */
        x = (clamp(v, 0, 127) * 4 * 256) / 127;
        if ((x & 255) < 64 || x >> 8 >= 4) {
            s[0] = VOW[x >> 8], s[1] = 0;
        } else if ((x & 255) >= 192) {
            s[0] = VOW[(x >> 8) + 1], s[1] = 0;
        } else {
            s[0] = VOW[x >> 8], s[1] = '-', s[2] = VOW[(x >> 8) + 1], s[3] = 0;
        }
        return;
    case 1:
        if (v == 12) { put_s(s, "0"); return; }
        put_u(put_s(s, v < 12 ? "-" : "+"), (uint32_t)(v < 12 ? 12 - v : v - 12));
        return;
    default: put_s(put_u(s, ((uint32_t)clamp(v, 0, 127) * 100u + 63u) / 127u), "%"); return;
    }
}

static const engine_t FORMANT_ENGINE = {"VOICE", {"VOWEL", "SHIFT", "BUZZ", "BREATH"}, {127, 24, 127, 127},
                                        formant_begin, formant_block, formant_text};
