/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's GRAIN effect: Felucca's GRAIN ("Felucca's own design": hugelton/Felucca firmware/src/eng_grain.c) over a
 * buffer of what the tracks send it, instead of a sample. Taken from eng_grain.c (as GR!MM's GRAIN engine had it, step 6):
 * the grain (a Hann window over its length), its length from SIZE and its interval from DENSITY (gr_len, gr_iv), the
 * random spread of its start and the jitter of the interval from SCATTER, the level by overlap (1 / sqrt n), the
 * window table. GR!MM's: the ring the GRAIN bus is recorded into, FREEZE, a pan for each grain, the return into the
 * mix and into SPACE.
 *   at half the rate (22.05 kHz, as DELAY and SPACE): the GRAIN bus (every other sample, 1 2 1) -> the ring (1.25 s) ->
 *   up to GE_NG grains read from it, each at its own place behind the newest sample and its own pan -> back to
 *   44.1 kHz, at MIX, into out and into the REV bus (the cloud falls into SPACE).
 *   FREEZE: the ring stops recording; the grains go on from what it holds.
 *   Nothing runs at MIX 0, or while the ring holds only silence (nothing sent for 1.25 s, nothing frozen).
 *   SIZE      a grain's length: 10 ms x 2^(SIZE / 24), 10 .. 390 ms
 *   DENSITY   grains a second: 2^(DENSITY x 10 / 192), 1 .. 98 (as many as GE_NG let sound)
 *   SCATTER   the grains' spread: where they start, up to the whole ring back; the pan; the interval's jitter
 *   MIX       the return
 * The audio ISR calls grain_render with the GRAIN bus, before DELAY and SPACE; the main loop sets the parameters and
 * ge_freeze. The ring is in the pool (zero at boot): 55 KB. */

#define GE_LEN 27563u                           /* the ring: 1.25 s at 22.05 kHz (of what was 2 s: the other 0.75 s,
                                                 * the FX layer's history, perform.c) */
#define GE_NC 128u                              /* a block at half the rate: app_audio's 256 frames */
#define GE_NG 8u                                /* grains at once */

/* sin^2(pi i / 256), Q15: the grain window */
static const int16_t GR_HANN[257] = {
    0, 5, 20, 44, 79, 123, 177, 241, 315, 398, 491, 593, 705, 827, 958, 1098,
    1247, 1406, 1573, 1749, 1935, 2128, 2331, 2542, 2761, 2989, 3224, 3468, 3719, 3978, 4244, 4518,
    4799, 5086, 5381, 5682, 5990, 6304, 6624, 6950, 7281, 7618, 7961, 8308, 8660, 9017, 9379, 9744,
    10114, 10487, 10864, 11244, 11628, 12014, 12403, 12794, 13187, 13583, 13980, 14378, 14778, 15178, 15580, 15981,
    16383, 16786, 17187, 17589, 17989, 18389, 18787, 19184, 19580, 19973, 20364, 20753, 21139, 21523, 21903, 22280,
    22653, 23023, 23388, 23750, 24107, 24459, 24806, 25149, 25486, 25817, 26143, 26463, 26777, 27085, 27386, 27681,
    27968, 28249, 28523, 28789, 29048, 29299, 29543, 29778, 30006, 30225, 30436, 30639, 30832, 31018, 31194, 31361,
    31520, 31669, 31809, 31940, 32062, 32174, 32276, 32369, 32452, 32526, 32590, 32644, 32688, 32723, 32747, 32762,
    32767, 32762, 32747, 32723, 32688, 32644, 32590, 32526, 32452, 32369, 32276, 32174, 32062, 31940, 31809, 31669,
    31520, 31361, 31194, 31018, 30832, 30639, 30436, 30225, 30006, 29778, 29543, 29299, 29048, 28789, 28523, 28249,
    27968, 27681, 27386, 27085, 26777, 26463, 26143, 25817, 25486, 25149, 24806, 24459, 24107, 23750, 23388, 23023,
    22653, 22280, 21903, 21523, 21139, 20753, 20364, 19973, 19580, 19184, 18787, 18389, 17989, 17589, 17187, 16786,
    16384, 15981, 15580, 15178, 14778, 14378, 13980, 13583, 13187, 12794, 12403, 12014, 11628, 11244, 10864, 10487,
    10114, 9744, 9379, 9017, 8660, 8308, 7961, 7618, 7281, 6950, 6624, 6304, 5990, 5682, 5381, 5086,
    4799, 4518, 4244, 3978, 3719, 3468, 3224, 2989, 2761, 2542, 2331, 2128, 1935, 1749, 1573, 1406,
    1247, 1098, 958, 827, 705, 593, 491, 398, 315, 241, 177, 123, 79, 44, 20, 5,
    0,
};

/* grain level by overlap (grains sounding at once on average): 1 / sqrt(n), Q15 (Felucca's, to GE_NG) */
static const int16_t GR_NORM[GE_NG + 1] = {32767, 32767, 23170, 18918, 16384, 14654, 13377, 12385, 11585};

typedef struct {
    uint32_t pos;                               /* the next sample it reads in the ring */
    uint32_t wph, winc;                         /* window phase: one turn = the grain */
    uint32_t left, wait;                        /* half-rate samples to go; to wait before it starts (this block) */
    int32_t gl, gr;                             /* its level by overlap, panned (Q15) */
} ge_grain_t;
static int16_t ge_buf[GE_LEN] __attribute__((section(".pool")));
static struct {
    ge_grain_t g[GE_NG];
    uint32_t on;                                /* bit k: grain k sounds */
    uint32_t w, quiet;                          /* the write index; silent samples written in a row */
    int32_t cd;                                 /* half-rate samples to the next grain */
    int32_t x, ml, mr;                          /* the input's last sample; the last wet out, left / right */
    int32_t l[GE_NC], r[GE_NC];                 /* the block's cloud at half the rate */
} ge;
static uint32_t gr_rng = 0x2545F491u;

/* the parameters, 0..127, and FREEZE, written by the main loop */
static volatile int32_t ge_size = 80, ge_dens = 80, ge_scat = 30, ge_mix = 100;   /* Felucca's GRAIN's SIZE, DENS, SPRD */
static volatile uint32_t ge_freeze;

static inline uint32_t gr_rnd(void)             /* xorshift32 */
{
    uint32_t s = gr_rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return gr_rng = s;
}
static inline uint32_t gr_scale(uint32_t n, uint32_t f16) { return (n >> 16) * f16 + (((n & 0xFFFFu) * f16) >> 16); }
static uint32_t gr_len(int32_t size) { return (441u * pow2_q16(clamp(size, 0, 127) * 8)) >> 16; }        /* 44.1 kHz samples */
static uint32_t gr_iv(int32_t dens) { return (44100u * pow2_q16(-clamp(dens, 0, 127) * 10)) >> 16; }   /* the interval */
static inline int32_t gr_win(uint32_t wph)      /* the Hann window, Q15 */
{
    uint32_t wi = wph >> 24;
    return GR_HANN[wi] + (((GR_HANN[wi + 1u] - GR_HANN[wi]) * (int32_t)((wph >> 9) & 0x7FFFu)) >> 15);
}

/* main loop: the cloud gives nothing out (MIX 0, or the ring silent) */
static int grain_idle(void) { return ge_mix <= 0 || ge.quiet >= GE_LEN; }

/* a grain, starting `off` samples into this block (write index w0 at the block's start) */
static void ge_spawn(uint32_t off, uint32_t w0, uint32_t iv, int32_t sc)
{
    uint32_t k, len, span, d, ov;
    int32_t pan, gain;
    ge_grain_t *g;
    for (k = 0; k < GE_NG && (ge.on >> k & 1u); k++)
        ;
    if (k == GE_NG)
        return;                                 /* all GE_NG sound: this one is left out */
    g = &ge.g[k];
    len = gr_len(ge_size) >> 1;                 /* (at half the rate) */
    len = len < 16u ? 16u : len;
    /* where: len + 1 .. the whole ring behind the newest sample (as the ring records, the grain stays as far behind; a
     * frozen one never reaches the newest sample, nor a recording one the samples written over in this block) */
    span = GE_LEN - GE_NC - 2u - len;
    d = len + 1u + gr_scale(gr_scale(span, (uint32_t)sc * 516u), gr_rnd() & 0xFFFFu);
    w0 += ge_freeze ? 0u : off;
    g->pos = w0 >= d ? w0 - d : w0 + GE_LEN - d;
    g->wph = 0;
    g->winc = 0xFFFFFFFFu / len;
    g->left = len;
    g->wait = off;
    ov = len / (iv ? iv : 1u);
    gain = GR_NORM[ov < 1u ? 1u : ov > GE_NG ? GE_NG : ov];
    pan = (((int32_t)(gr_rnd() & 0xFFFFu) - 32768) * sc) / 127;   /* +-SCATTER: from the middle to the sides */
    g->gl = mulq15(gain, mulq15(23170, 32768 - pan));
    g->gr = mulq15(gain, mulq15(23170, 32768 + pan));
    ge.on |= 1u << k;
}

/* one grain into the block's cloud (Felucca's gr_run: the window over the grain; here a ring read at the source's rate) */
static void ge_run(ge_grain_t *g, uint32_t k)
{
    uint32_t i = g->wait, pos = g->pos, wph = g->wph, winc = g->winc, end = i + g->left;
    int32_t gl = g->gl, gr = g->gr;
    end = end < GE_NC ? end : GE_NC;
    g->left -= end - i;
    g->wait = 0;
    while (i < end) {                           /* in runs up to the ring's end */
        uint32_t m = end - i < GE_LEN - pos ? end - i : GE_LEN - pos, j;
        const int16_t *s = ge_buf + pos;
        for (j = 0; j < m; j++, i++, wph += winc) {
            int32_t y = (s[j] * gr_win(wph)) >> 15;
            ge.l[i] += (y * gl) >> 15;
            ge.r[i] += (y * gr) >> 15;
        }
        pos += m;
        pos = pos == GE_LEN ? 0u : pos;
    }
    g->pos = pos;
    g->wph = wph;
    if (!g->left)
        ge.on &= ~(1u << k);
}

/* the audio ISR: n (= 2 GE_NC) frames of the GRAIN bus in (mono) -> the cloud added to out (L R) and to the REV bus */
static void grain_render(const int32_t *in, int32_t *out, int32_t *rev, uint32_t n)
{
    uint32_t i, k, any = 0, w0 = ge.w, iv;
    int32_t mix = ge_mix, sc = clamp(ge_scat, 0, 127), xp = ge.x, ml = ge.ml, mr = ge.mr;
    if (n != 2u * GE_NC || mix <= 0)
        return;                                 /* MIX 0: the ring waits as it is */
    for (i = 0; i < 2u * GE_NC && !any; i++)
        any = in[i] != 0;
    if (!any && ge.quiet >= GE_LEN) {           /* nothing sent, the ring silent: nothing to do */
        ge.on = 0;
        ge.x = 0;
        return;
    }
    if (!ge_freeze) {                           /* the bus into the ring (1 2 1 to half the rate) */
        uint32_t w = w0, q = ge.quiet;
        for (i = 0; i < GE_NC; i++) {
            int32_t x = clamp((xp + 2 * in[2u * i] + in[2u * i + 1u]) >> 2, -32767, 32767);
            xp = in[2u * i + 1u];
            ge_buf[w] = (int16_t)x;
            q = x ? 0u : q + 1u;
            w = w + 1u == GE_LEN ? 0u : w + 1u;
        }
        ge.w = w;
        ge.quiet = q > GE_LEN ? GE_LEN : q;
    }
    ge.x = xp;

    /* the grains due in this block: DENSITY a second, SCATTER jitters the interval (eng_grain.c) */
    iv = gr_iv(ge_dens) >> 1;
    iv = iv ? iv : 1u;
    for (k = 0; k < 4u && ge.cd < (int32_t)GE_NC; k++) {
        int32_t j = (((int32_t)(gr_rnd() & 0xFFFFu) - 32768) * sc) / 254;   /* +-SCATTER / 2, Q15 */
        ge_spawn((uint32_t)(ge.cd > 0 ? ge.cd : 0), w0, iv, sc);
        ge.cd += (int32_t)iv + mulq15((int32_t)iv, j);
    }
    ge.cd = ge.cd - (int32_t)GE_NC > 0 ? ge.cd - (int32_t)GE_NC : 0;

    for (i = 0; i < GE_NC; i++)
        ge.l[i] = ge.r[i] = 0;
    for (k = 0; k < GE_NG; k++)
        if (ge.on >> k & 1u)
            ge_run(&ge.g[k], k);

    /* back to 44.1 kHz, between samples, at the return's level (MIX), into out and into the REV bus (as DELAY) */
    {
        int32_t wg = mix * 258;
        for (i = 0; i < GE_NC; i++) {
            int32_t wl = clamp(ge.l[i], -65535, 65535), wr = clamp(ge.r[i], -65535, 65535), *o = out + 4u * i,
                    a = mulq15((ml + wl) >> 1, wg),
                    b = mulq15((mr + wr) >> 1, wg), c = mulq15(wl, wg), d = mulq15(wr, wg);
            o[0] += a, o[1] += b, o[2] += c, o[3] += d;
            rev[2u * i] += (a + b) >> 1;        /* (the cloud, both sides, into SPACE) */
            rev[2u * i + 1u] += (c + d) >> 1;
            ml = wl;
            mr = wr;
        }
    }
    ge.ml = ml, ge.mr = mr;
}
