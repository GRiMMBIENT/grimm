/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's TAPE effect: a worn tape. GR!MM's own, from DELAY's wear (wear.c: the wow and flutter, the saturation; the
 * design after X0X's TAPE, see tape.c). (V2: a track's own: TAPE's page the tape's four knobs, and each track goes
 * through it as far as its TAPE tile (MIXER 1) times its FX (HOME) is up, 0 .. all of it, tt_a: a crossfade from the
 * track as it is to it taped, never the two added. A tile at 0: the track untouched, to the bit. Before: the whole mix,
 * the last stage before MASTER, and the tiles a taped copy beside the track)
 *   WOW         (V2) a worn motor, a slipping belt: the notes' pitch (every source, the samples' speed too: TUNE's
 *               path, voices.c wow_f) steady, and now and then a warble (tape_wow), evenly spread: 3 .. 6.5 s steady
 *               between them (mostly near the middle), one every ~5.5 s. A warble short, 0.3 .. 1.5 s, a random walk
 *               of 1 .. 4 steps: each to a pitch drawn anew (mostly flat, a motor losing power; sharp too), straight
 *               there, as a jerk (20 .. 40 ms) or a sag (up to 300 .. 600 ms), sometimes held there a while; then
 *               back to exactly 0, quick or slow. The knob only how far the steps may go (straight: 20 %, ~27 cents;
 *               half, ~68; the top, ~136), never how often. WOW 0: no bend at all. (V2: one warble for every track,
 *               the same moments; a track's notes bent as far as it goes through the tape: half of it, half the bend)
 *   SATURATION  DELAY's tanh, driven 1x .. 4x, the level made up after it, blended in with the knob: 0 = clean. One
 *               curve (tf_curve), made by the main loop when the knob moves (tapefx_poll), crossed over to from the
 *               last within a block: no step in the sound, and nothing heavy in the audio ISR
 *   TONE        a one-pole low-pass, darker as it turns: open .. ~1.8 kHz
 *   HISS        noise (a draw every two frames: a little darker than white), under the low-pass (TONE darkens it
 *               too): up to ~ -41 dB rms. (V2: the tape's own, into the mix after DRIVE, as loud as the track most
 *               on tape is on it: no track on it, no hiss)
 * SATURATION and TONE at 0: a track's tape clean, nothing runs (WOW alone: the notes bent, the sound as it is). The main
 * loop sets the four and calls tapefx_poll; the audio ISR calls tape_begin a block (before the voices), tape_trk on each
 * track (mix_tracks, before its filter), tapefx_render on the mix (after DRIVE). */

#define TF_NC 768u                              /* SATURATION's curve: x -98304 .. 98304 in steps of 256 */
#define TF_CQ 2                                 /* its values kept >> TF_CQ (int16: |y| <= |x| <= 98304, so <= 24576;
                                                 * the rounding: at most 2 in 98304, under one bit of the output) */

static int16_t tf_curve[2][TF_NC + 1u];         /* SATURATION as one curve: drive, tanh, make-up, blend (tf_make): the
                                                 * one in use (tf.ci) and the next / the last */
static struct {
    uint32_t rng, fade, pub;                    /* the hiss; (V2) a new curve crossed over to this block, its tf_pub */
    int32_t lpl, lpr, csat, ci;                 /* HISS's low-pass (TONE), L R; the SATURATION the curve in use is for,
                                                 * and which one it is */
    int32_t hl, hn;                             /* (V2) HISS's level, the last block's end, this one's */
} tf;
static struct {                                 /* (V2) a track through the tape (tape_trk) */
    int32_t a, lp;                              /* how far, the last block's end (tt_a's); TONE's low-pass */
    uint32_t on;                                /* (the low-pass running: else from the sound) */
} tt[NT];

/* the parameters, 0..127, written by the main loop */
static volatile int32_t tf_wow, tf_sat, tf_tone, tf_hiss;
static volatile uint32_t tf_pub, tf_ack;        /* main loop: + 1, the other curve made (for tf_psat); audio: = tf_pub,
                                                 * crossed over to it */
static volatile int32_t tf_psat;

/* (V2) WOW: the warbles, a block on (app_audio, once a block, before the voices): wow_b (voices.c), the bend at this
 * block's end, Q8 of tune_f's units (1 cent: 2.366, so ~606 in Q8); its start the last one's end (wow_a), and voices.c
 * goes from one to the other a control block at a time. No shapes and no oscillator: a warble a random walk, steps
 * straight to a target drawn each time (tw.g, Q15 of the knob's most), quick or slow (tw.m blocks), sometimes held there
 * (tw.h), then the next (tw.n left, the last one back to exactly 0), all in the warble's time (tw.q). The draws never
 * depend on the knob (it only scales them: the same warbles, the same moments, at every WOW) */
#define TW_BPS 172                              /* blocks a second (44100 / 256) */
static struct {
    uint32_t rng;                               /* the draws */
    int32_t gap, q, t, n, m, h, r, s, g, f;     /* blocks still steady before the next (-1: draw them); the warble's
                                                 * blocks left; the step's block (-1: none), the steps left, its move's
                                                 * blocks, held, the way back's; from, to, now (Q15 of the most, - flat) */
} tw = {0x2545F491u, -1, 0, -1, 0, 0, 0, 0, 0, 0, 0};
static uint32_t tw_rnd(uint32_t n) { tw.rng = tw.rng * 1664525u + 1013904223u; return (tw.rng >> 8) % n; }
static int32_t tw_move(void) { return tw_rnd(2) ? 4 + (int32_t)tw_rnd(4) : 52 + (int32_t)tw_rnd(52); }   /* (a jerk, 23
                                                 * .. 40 ms; or a sag, 300 .. 600 ms) */
static void tw_step(int32_t back)               /* the next step: a target (or 0), how fast, how long held there; within
                                                 * the warble's time (the way back's kept), a jerk kept for each target
                                                 * after it, the last held for what is left */
{
    int32_t m = tw_move(), x;
    tw.s = tw.f, tw.t = 0;
    if (back) {
        tw.g = 0, tw.h = 0, tw.m = tw.r;
        return;
    }                                           /* (leaning flat, a motor losing power: 2 in 3 flat, 0.2 .. 1 of the
                                                 * most; or sharp, 0.2 .. 0.7) */
    tw.g = tw_rnd(3) ? -(6554 + (int32_t)tw_rnd(26214)) : 6554 + (int32_t)tw_rnd(16384);
    tw.h = tw_rnd(5) < 3u ? 9 + (int32_t)tw_rnd(112) : 0;   /* (3 in 5 held, 50 .. 700 ms) */
    x = tw.q - 4 * (tw.n - 1);
    tw.m = m < x ? m : x;
    if (tw.n == 1 || tw.h > x - tw.m) tw.h = x - tw.m;   /* (the last: held the rest) */
    tw.q -= tw.m + tw.h;
}
static void tape_wow(void)
{
    int32_t w = clamp(tf_wow, 0, 127), e;
    wow_a = wow_b;
    if (!w) {                                   /* (WOW 0: none, at once; the next gap drawn when it turns up) */
        tw.t = -1, tw.gap = -1, tw.f = 0, wow_b = 0;
        return;
    }
    if (tw.t < 0) {                             /* steady: exactly 0 */
        if (tw.gap < 0)                         /* (3 .. 6.5 s, mostly near the middle: one every ~5.5 s, evenly) */
            tw.gap = 3 * TW_BPS + (int32_t)tw_rnd(301) + (int32_t)tw_rnd(301);
        if (tw.gap > 0) tw.gap--;
        else {                                  /* a warble: 1 .. 4 steps in 0.3 .. 1.5 s (the draws stirred with the
                                                 * notes playing, and when: never the same twice from power-on) */
            uint32_t v, st = vblock;
            for (v = 0; v < NV; v++) st = st * 31u + vc[v].note * 7u;
            tw.rng ^= st * 2654435761u;
            tw.n = 1 + (int32_t)tw_rnd(4), tw.q = 52 + (int32_t)tw_rnd(207), tw.r = tw_move();
            if (tw.r > tw.q / 3) tw.r = tw.q / 3;   /* (the way back: a jerk or a sag, a third of it at most) */
            tw.q -= tw.r;
            tw_step(0);
        }
        if (tw.t < 0) {
            wow_b = 0;
            return;
        }
    }
    e = ++tw.t;                                 /* the step: straight there, then held */
    tw.f = e < tw.m ? tw.s + (tw.g - tw.s) * e / tw.m : tw.g;
    if (e >= tw.m + tw.h) {
        if (tw.n > 0) tw_step(--tw.n == 0);     /* (the next; after the last target, back to 0) */
        else {                                  /* (back at 0: done, steady again) */
            tw.t = -1, tw.gap = -1, tw.f = 0, wow_b = 0;
            return;
        }
    }
    wow_b = (int32_t)(((int64_t)(82416 * w / 127) * tw.f) >> 15);   /* (the knob: the most, straight, to 136 cents) */
}

/* a SATURATION curve into c: DELAY's tanh (wear.c), driven 1x .. 4x, made up, blended in with the knob (0: x itself) */
static void tf_make(int16_t *c, int32_t sat)
{
    int32_t g = 4096 + sat * 97, ig = (int32_t)(134217728u / (uint32_t)g), sw = sat * 129, x, y;   /* sw: Q14 */
    uint32_t j;
    for (j = 0; j <= TF_NC / 2u; j++) {         /* (odd: one half, mirrored) */
        x = (int32_t)(j << 8);
        y = x + (((wear_sat(x, g, ig) - x) * sw) >> 14);
        c[TF_NC / 2u + j] = (int16_t)clamp((y + (1 << (TF_CQ - 1))) >> TF_CQ, -32767, 32767);
        c[TF_NC / 2u - j] = (int16_t)-c[TF_NC / 2u + j];
    }
}
/* main loop: the curve for SATURATION as it is now, into the one not in use, once the last has been taken (first call:
 * the curve in use, x itself) */
static void tapefx_poll(void)
{
    static uint32_t made;
    int32_t sat = clamp(tf_sat, 0, 127);
    if (!made) {
        tf_make(tf_curve[0], 0);
        made = 1;
    }
    if (tf_ack != tf_pub || sat == tf_psat)
        return;
    tf_make(tf_curve[tf.ci ^ 1], sat);
    tf_psat = sat;
    tf_pub++;
}
static inline int32_t tf_shape(const int16_t *c, int32_t x)   /* x through curve c, between its points */
{
    int32_t a = clamp(x + (int32_t)(TF_NC << 7), 0, (int32_t)(TF_NC << 8) - 1), i = a >> 8;
    int32_t y0 = (int32_t)c[i] * (1 << TF_CQ), y1 = (int32_t)c[i + 1] * (1 << TF_CQ);
    return y0 + (((y1 - y0) * (a & 255)) >> 8);
}

/* (V2) the audio ISR, a block, before the voices: how far each track goes through the tape (its TAPE tile x its FX,
 * all of it at most: Q15, 32768 all), HISS's level by the most of them; the new SATURATION curve taken (crossed over to
 * this block, the old one let go the next); then WOW */
static void tape_begin(void)
{
    int32_t m = 0, h = clamp(tf_hiss, 0, 127);
    uint32_t t;
    for (t = 0; t < NT; t++) {
        int32_t x = clamp(trk[t].tap, 0, 127) * clamp(trk[t].fx, 0, 200);   /* (127 x 100: all of it) */
        tt_a[t] = x >= 12700 ? 32768 : x * 32768 / 12700;
        if (tt_a[t] > m) m = tt_a[t];
    }
    tf.hn = (((h * h) >> 5) * m) >> 15;         /* (no track on tape, or HISS 0: none) */
    if (tf.fade)
        tf_ack = tf.pub, tf.fade = 0;           /* (the last curve free for the next) */
    else if (tf_pub != tf_ack)
        tf.fade = 1, tf.pub = tf_pub, tf.ci ^= 1, tf.csat = tf_psat;
    tape_wow();
}

static inline int32_t mulq15w(int32_t a, int32_t b);
/* (V2) the audio ISR: track t's sound (x, n frames, mono, Q15 << PB; 0: it is silent) through the tape as far as tt_a:
 * SATURATION (the curve in use; a new one crossed over to in the block), TONE, crossfaded in from the sound as it is
 * (a ramp over the block from the last's). 0 (and the last block 0), or the tape clean: x as it is */
static void tape_trk(uint32_t t, int32_t *x, uint32_t n)
{
    int32_t a0 = tt[t].a, a1 = tt_a[t], tone = clamp(tf_tone, 0, 127), tc = 32767 - ((tone * tone * 1606) >> 10);
    int32_t l = tt[t].lp, g, dg, y;
    uint32_t i, fade = tf.fade, shape = tf.csat || fade, on = tt[t].on;
    const int16_t *c1 = tf_curve[tf.ci], *c0 = tf_curve[tf.ci ^ 1];
    tt[t].a = a1;
    if (!x || (!a0 && !a1) || (!shape && !tone)) {
        tt[t].on = 0;
        return;
    }
    g = a0 << 8, dg = ((a1 - a0) << 8) / (int32_t)n;   /* (8 bits more: small steps) */
    for (i = 0; i < n; i++) {
        y = (x[i] + (1 << (PB - 1))) >> PB;     /* (Q15, as the tape works) */
        if (fade) {                             /* (the last curve to the new one over the block) */
            int32_t o = tf_shape(c0, y);
            y = o + mulq15(clamp(tf_shape(c1, y) - o, -65535, 65535), (int32_t)i << 7);
        } else if (shape)
            y = tf_shape(c1, y);
        if (tone) {                             /* (the low-pass, Q15; from the sound when it starts) */
            if (!on) l = y, on = 1;
            l += (clamp(y - l, -65535, 65535) * tc) >> 15;
            y = l;
        }
        g += dg;
        x[i] += mulq15w((y << PB) - x[i], g >> 8);   /* (all of it: y itself) */
    }
    tt[t].on = tone ? on : 0, tt[t].lp = l;
}

/* the audio ISR: HISS (V2: the tape's own, as far as the track most on it is: tape_begin) into the mix (n frames, L R,
 * Q15), ramped from the last block's level; under TONE's low-pass. None: nothing runs */
static void tapefx_render(int32_t *out, uint32_t n)
{
    int32_t tone = clamp(tf_tone, 0, 127), tc = 32767 - ((tone * tone * 1606) >> 10), l = tf.lpl, r = tf.lpr;
    int32_t h0 = tf.hl, h1 = tf.hn, g, dg, hl0 = 0, hr0 = 0;
    uint32_t i, rng = tf.rng ? tf.rng : 0x9E3779B9u;
    tf.hl = h1;
    if (!h0 && !h1)
        return;
    if (n > VB)
        n = VB;
    g = h0 << 8, dg = ((h1 - h0) << 8) / (int32_t)n;
    for (i = 0; i < n; i++) {
        g += dg;
        if (!(i & 1u)) {                        /* (a draw a side for two frames: its top bits; a little darker than
                                                 * white) */
            rng = rng * 1664525u + 1013904223u;
            hl0 = (((int32_t)rng >> 16) * (g >> 8)) >> 15;
            rng = rng * 1664525u + 1013904223u;
            hr0 = (((int32_t)rng >> 16) * (g >> 8)) >> 15;
        }
        if (tone) {                             /* (TONE darkens it) */
            l += (clamp(hl0 - l, -65535, 65535) * tc) >> 15;
            r += (clamp(hr0 - r, -65535, 65535) * tc) >> 15;
        } else
            l = hl0, r = hr0;
        out[2u * i] += l;
        out[2u * i + 1u] += r;
    }
    tf.rng = rng, tf.lpl = l, tf.lpr = r;
}
