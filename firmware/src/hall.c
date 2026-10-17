/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's SPACE: Felucca's HALL reverb (hugelton/Felucca firmware/src/fx.c: rev_hall, hall_ap, hall_end(_lp),
 * sh_tz, HL_RUNS), taken over without Felucca's buses, made longer and darker. As Felucca's:
 *   at half the rate (22.05 kHz: the same memory holds twice the time, half the work; the tail stops near 10 kHz):
 *   the send, every other sample low-passed 1 2 1 -> a low cut (~110 Hz: no boom in a long tail) -> allpass
 *   diffusers -> 8 lines, mutually prime lengths, that all feed each other through a conference matrix (Paley,
 *   order 8: every line feeds the 7 others, +-1 each, none itself), lines 1 2 4 7 read through slow sines (0.37 /
 *   0.74 Hz: the resonances drift instead of ringing); each line's gain from its length (all decay alike); outputs
 *   left 0 3 4 -7, right 1 2 5 6; rounding that lets a quiet tail end in exact silence; back to 44.1 kHz between
 *   samples.
 * GR!MM's: lines of 39 .. 74 ms (Felucca's 21 .. 34) and 4 diffusers (Felucca's 2): bigger; DAMP's low-pass on all
 * 8 lines (Felucca's 4), down to ~650 Hz: darker; RT60 0.4 .. ~32 s (Felucca's 0.5 .. 9 s), and SIZE 127 = INF:
 * the matrix lossless but for 1e-4 a pass, the loop's damping off and the send faded out (a loop that keeps its
 * energy and still takes more can only grow: INF holds what is in it, as a freeze); a soft knee on everything that
 * goes back into the lines, so nothing feeding back can clip or run away; PRE-DELAY 0 .. 250 ms before the
 * diffusers; MIX: the return (the wet's level: the tracks' REV sends set how much goes in); nothing run at MIX 0 (the tail waits)
 * or once the tank is empty and nothing comes in.
 *
 * The audio ISR calls hall_render with the REV bus (the tracks' REV sends and DELAY's echoes); the main loop sets the four parameters. Its buffers are in the
 * pool (zero at boot): ~37 KB. */

#define RV_NL 8u                                /* lines */
#define RV_NAP 4u                               /* diffusers */
#define RV_NC 128u                              /* a block at half the rate: app_audio's 256 frames */
#define RV_MOD 10                               /* the drifting taps' swing, samples (read 2 .. 22 short of the end) */
#define RV_DRIFT 0x96u                          /* the lines that drift: 1 2 4 7 (Felucca's) */
#define RV_LOUD 32                              /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define RV_KD(d) (32767 - (d) * 210)            /* DAMP d's low-pass (Q15): none at 0 .. ~650 Hz at 127 */
#define RV_IN 10320                             /* the send's level (Q15): 4 x Felucca's, the lines 12 dB hotter: the tail
                                                 * holds 12 dB longer before its last LSBs round away */
#define RV_OUT 5                                /* the outputs' gain, / 4 (RV_IN's 4 x back out) */
#define RV_PRE_MAX 5513u                        /* PRE-DELAY at most 250 ms (22.05 kHz) */
#define RV_PRE_LEN 5760u                        /* its line: that and a block (and a little) */
#define RV_LINES (853u + 941u + 1031u + 1123u + 1237u + 1361u + 1493u + 1637u + RV_NL)   /* (+1 each: the mirror) */
#define RV_APS (149u + 211u + 337u + 499u)
#define RV_EMPTY 14u                            /* blocks of silence in and out of the lines: all of them silent (1637 / 128) */
static const uint16_t RV_LEN[RV_NL] = {853, 941, 1031, 1123, 1237, 1361, 1493, 1637};
static const uint16_t RV_AP[RV_NAP] = {149, 211, 337, 499};
static const int16_t RV_APG[RV_NAP] = {11469, 11469, 10240, 10240};   /* Q14: 0.7, 0.7, 0.625, 0.625 (Felucca's two) */
_Static_assert(RV_NC + 2u * RV_MOD + 2u < 853u && RV_NC <= 149u, "SPACE: one wrap a block at most, in place");
_Static_assert(RV_PRE_MAX + RV_NC <= RV_PRE_LEN, "SPACE: the pre-delay's line");

static int16_t rv_line[RV_LINES] __attribute__((section(".pool")));
static int16_t rv_apb[RV_APS] __attribute__((section(".pool")));
static int16_t rv_pbuf[RV_PRE_LEN] __attribute__((section(".pool")));
static struct {
    uint16_t i[RV_NL], d[RV_NAP], pw;           /* the lines' write indices, the diffusers', the pre-delay's */
    int32_t lp[RV_NL], le[RV_NL];               /* the low-passes and their steps' remainders */
    int32_t rp[RV_NL];                          /* the drifting taps at the end of the last block (Q8 samples) */
    int32_t x, hp, he, ml, mr, send;            /* the send's last sample, its low cut (and remainder); the last wet
                                                 * out, left / right; the send's level (Q15: 0 at INF) */
    uint32_t ph, quiet, empty;                  /* the taps' drift; half-rate samples with nothing coming in; blocks
                                                 * with nothing in the lines */
    int32_t bx[RV_NC], l[RV_NC], r[RV_NC];      /* the block at half the rate: the diffused send, left, right */
    int16_t y[RV_NL][RV_NC];                    /* the lines' ends, then what goes back into them */
} rv __attribute__((section(".pool")));

/* the parameters, 0..127, written by the main loop */
static volatile int32_t rv_size = 80, rv_damp = 48, rv_mix = 100, rv_pre = 16;  /* 6.4 s, ~2.5 kHz, return 79 %, 31 ms */
#define RV_INF 127

/* Felucca's helpers: m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors holds
 * an offset and a little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; its state *lp and its step's
 * remainder *le), times its gain g (Q14): rounded to the nearest while the line is loud, towards zero once it is
 * quiet (lo: the tail ends in exact silence). Felucca's hall_end_lp */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    for (i = 0; i < n; i++) {
        e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
        y[i] = (int16_t)(lo ? sh_tz(l * g, 14) : (l * g + 8192) >> 14);
    }
    *lp = l;
    *le = r;
}
/* a drifting line's end over the block: r0 -> r1 (Q8 samples ahead of the write index w, gliding over the block:
 * GR!MM's block is 8 of Felucca's, too long to hold a tap still), between samples (b[len] is b[0]: the mirror), then
 * as hall_end_lp */
static inline void hall_end_drift(const int16_t *b, uint32_t len, uint32_t w, int32_t r0, int32_t r1, int16_t *y,
                                  int32_t g, int lo, int32_t *lp, int32_t *le, int32_t kd)
{
    uint32_t i, p;
    int32_t l = *lp, re = *le, e, dr = r1 - r0;
    for (i = 0; i < RV_NC; i++) {
        int32_t r = r0 + ((dr * (int32_t)i) >> 7), f = r & 255, x;
        p = w + i + (uint32_t)(r >> 8);
        p = p >= len ? p - len : p;
        x = b[p] + (((b[p + 1u] - b[p]) * f + 128) >> 8);
        e = (x - l) * kd + re, re = e & 32767, l += e >> 15;
        y[i] = (int16_t)(lo ? sh_tz(l * g, 14) : (l * g + 8192) >> 14);
    }
    *lp = l;
    *le = re;
}
/* the block's RV_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on (Felucca's HL_RUNS) */
#define RV_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < RV_NC; s += k) {                                                        \
        k = (len) - (d) < RV_NC - s ? (len) - (d) : RV_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }
/* the drift's sine: a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %: Dinghy's) */
static int32_t rv_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
#define RV_LFO 9224927u                         /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd): 2^32 0.37 256 / 44100 a block */

/* main loop: the reverb gives nothing out (MIX 0, or the tank empty and nothing coming in) */
static int hall_idle(void) { return rv_mix <= 0 || (rv.quiet >= RV_PRE_LEN && rv.empty >= RV_EMPTY); }

/* the audio ISR: n (= 2 RV_NC) frames of the REV bus in (mono) -> the reverb added to out (L R) */
static void hall_render(const int32_t *in, int32_t *out, uint32_t n)
{
    int16_t *b, *a;
    uint32_t i, k, s, m, d, w, rd, pd, any = 0, full = 0;
    int32_t mix = rv_mix, size = rv_size, damp = rv_damp, pre = rv_pre;
    int32_t inf = size >= RV_INF, kd = inf ? 32767 : RV_KD(damp), st = inf ? 0 : 32767;
    int32_t xp = rv.x, hp = rv.hp, he = rv.he, sd = rv.send, ml = rv.ml, mr = rv.mr;
    const int16_t *g = RV_G[size < 0 ? 0 : size > RV_INF ? RV_INF : size];
    if (n != 2u * RV_NC || mix <= 0)
        return;                                 /* MIX 0: the dry alone; the tank waits as it is */
    for (i = 0; i < 2u * RV_NC && !any; i++)
        any = in[i] != 0;
    if (!any && rv.quiet >= RV_PRE_LEN && rv.empty >= RV_EMPTY)
        return;                                 /* nothing comes in, nothing is left: nothing to do */

    /* the send: every other sample, 1 2 1, the low cut, its level (fading to 0 at INF), into the pre-delay */
    pd = (uint32_t)(pre < 0 ? 0 : pre > 127 ? 127 : pre) * RV_PRE_MAX / 127u;
    w = rv.pw;
    rd = w >= pd ? w - pd : w + RV_PRE_LEN - pd;
    for (i = 0; i < RV_NC; i++) {
        int32_t x = (xp + 2 * in[2u * i] + in[2u * i + 1u]) >> 2, v;
        xp = in[2u * i + 1u];
        x = mulq15(clamp(x, -262143, 262143), RV_IN);
        v = x - hp + he;                        /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        sd += sd < st ? 256 : sd > st ? -256 : 0;   /* (~6 ms from full to 0) */
        sd = clamp(sd, 0, 32767);
        rv_pbuf[w] = (int16_t)clamp(mulq15(x - hp, sd), -32768, 32767);
        rv.bx[i] = rv_pbuf[rd];
        if (++w == RV_PRE_LEN) w = 0;
        if (++rd == RV_PRE_LEN) rd = 0;
    }
    rv.x = xp, rv.hp = hp, rv.he = he, rv.send = sd, rv.pw = (uint16_t)w;
    rv.quiet = any ? 0u : rv.quiet + RV_NC;

    a = rv_apb;                                 /* the diffusers */
    for (k = 0; k < RV_NAP; k++) {
        d = rv.d[k];
        RV_RUNS(d, RV_AP[k], m, s, hall_ap(a + d, RV_APG[k], rv.bx + s, m));
        rv.d[k] = (uint16_t)d;
        a += RV_AP[k];
    }

    rv.ph += RV_LFO;                            /* the lines' ends */
    b = rv_line;
    for (k = 0; k < RV_NL; k++) {
        int32_t lo;
        d = rv.i[k];
        s = d + RV_LEN[k] / 2u;                 /* quiet? (two samples half a line apart) */
        s = s >= RV_LEN[k] ? s - RV_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < RV_LOUD;
        if ((RV_DRIFT >> k) & 1u) {             /* RV_MOD + 2 +- RV_MOD samples short of the end, between samples */
            int32_t r = ((RV_MOD + 2) << 8) + ((rv_sine(rv.ph * (1u + (k & 1u)) + k * 0x2C000000u) * RV_MOD) >> 7);
            hall_end_drift(b, RV_LEN[k], d, rv.rp[k], r, rv.y[k], g[k], lo, &rv.lp[k], &rv.le[k], kd);
            rv.rp[k] = r;
        } else {                                /* (the line's end) */
            RV_RUNS(d, RV_LEN[k], m, s, hall_end_lp(b + d, rv.y[k] + s, m, g[k], lo, &rv.lp[k], &rv.le[k], kd));
        }
        b += RV_LEN[k] + 1u;
    }

    for (i = 0; i < RV_NC; i++) {               /* the outputs and the matrix */
        int32_t y0 = rv.y[0][i], y1 = rv.y[1][i], y2 = rv.y[2][i], y3 = rv.y[3][i];
        int32_t y4 = rv.y[4][i], y5 = rv.y[5][i], y6 = rv.y[6][i], y7 = rv.y[7][i], x = rv.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        rv.l[i] = y0 + y3 + y4 - y7;
        rv.r[i] = y1 + y2 + y5 + y6;
        /* C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances 1 2 4, -1 at 3 5
         * 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i (Felucca's) */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        /* back into the lines through a soft knee (linear to half scale; GR!MM's: nothing feeding back clips) */
        rv.y[0][i] = (int16_t)clamp(soft_knee(t + x, 16384), -32767, 32767);
        rv.y[1][i] = (int16_t)clamp(soft_knee(2 * p0 + y1 - y0 + x, 16384), -32767, 32767);
        rv.y[2][i] = (int16_t)clamp(soft_knee(2 * p1 + y2 - y0 + x, 16384), -32767, 32767);
        rv.y[3][i] = (int16_t)clamp(soft_knee(2 * p2 + y3 - y0 + x, 16384), -32767, 32767);
        rv.y[4][i] = (int16_t)clamp(soft_knee(2 * p3 + y4 - y0 + x, 16384), -32767, 32767);
        rv.y[5][i] = (int16_t)clamp(soft_knee(2 * p4 + y5 - y0 + x, 16384), -32767, 32767);
        rv.y[6][i] = (int16_t)clamp(soft_knee(2 * p5 + y6 - y0 + x, 16384), -32767, 32767);
        rv.y[7][i] = (int16_t)clamp(soft_knee(2 * p6 + y7 - y0 - x, 16384), -32767, 32767);
        full |= (uint32_t)(rv.l[i] | rv.r[i] | x);
        for (k = 0; k < RV_NL; k++)
            full |= (uint32_t)rv.y[k][i];
    }
    rv.empty = full ? 0u : rv.empty + 1u;

    b = rv_line;                                /* back into the lines (and the mirror after a wrap) */
    for (k = 0; k < RV_NL; k++) {
        d = rv.i[k];
        RV_RUNS(d, RV_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = rv.y[k][s + i]; if (!d) b[RV_LEN[k]] = b[0]);
        rv.i[k] = (uint16_t)d;
        b += RV_LEN[k] + 1u;
    }

    /* back to 44.1 kHz, between samples, at the return's level (MIX) */
    {
        int32_t wg = mix * 258;
        for (i = 0; i < RV_NC; i++) {
            int32_t wl = (rv.l[i] * RV_OUT) >> 2, wr = (rv.r[i] * RV_OUT) >> 2;
            int32_t *o = out + 4u * i;
            o[0] += mulq15((ml + wl) >> 1, wg);
            o[1] += mulq15((mr + wr) >> 1, wg);
            o[2] += mulq15(wl, wg);
            o[3] += mulq15(wr, wg);
            ml = wl;
            mr = wr;
        }
    }
    rv.ml = ml, rv.mr = mr;
}
