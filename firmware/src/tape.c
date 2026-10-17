/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's DELAY: a tape echo. Written for GR!MM in integers; the design follows X0X's TAPE delay (fx_delay in
 * firmware/src/dsp/fxbus.c of charlesvestal/fm1-x0x, GPL-3.0, itself a port of 9W9's er99_dly_tick, GPL-3.0): wow
 * (0.5 Hz) and flutter (6.1 Hz) on the read position, a darker loop with a tanh saturation that holds it, a 30 Hz DC
 * block inside it, feedback up to past unity. X0X's code is float, which the FM-1 cannot run: none of it is here.
 * The wow and the saturation are in wear.c (the TAPE effect has them too). The integer pieces are Felucca's, as GR!MM
 * has them (softclip, soft_knee, sh_tz, the one-pole low-pass that keeps
 * its steps' remainders so a tail ends in exact silence).
 *
 *   at half the rate (22.05 kHz, as SPACE: 3.5 s fit in 155 KB, the work halves, the echoes stop near 10 kHz): the
 *   voices (every other sample, 1 2 1) -> the line (77456 samples) -> read TIME back, plus the wow (two slow sines,
 *   0.5 and 0.19 Hz: not quite periodic) and a little flutter (6.1 Hz), between samples, gliding over the block; the
 *   right side 9 ms later -> the loop: a one-pole low-pass (WEAR: ~7 kHz .. ~1.2 kHz), FEEDBACK (0 .. 110 %), a tanh
 *   saturation (WEAR: 1x .. 1.8x drive, the level made up), the DC block -> back into the line with the input,
 *   through a soft knee: no sum can clip. Each repeat: darker, a little saturated, a little more wobbly.
 *   TIME glides to a new value (~50 ms): the pitch bends, as a tape's does.
 *   MIX: the return (the echoes' level: the tracks' DLY sends set how much goes in); the echoes also go into the REV
 *   bus at that level: they fall into SPACE. Nothing runs at MIX 0, or once
 *   the line is all silence and nothing comes in.
 *
 * The audio ISR calls tape_render with the DLY bus, before SPACE (the echoes fall into the reverb); the main loop
 * sets the four parameters. The line and the state are in the pool (zero at boot): ~157 KB. */

#define DL_NC 128u                              /* a block at half the rate: app_audio's 256 frames */
#define DL_LEN 77456u                           /* 3500 ms (77175 samples) + the wow's reach + the right side's 9 ms */
#define DL_SPREAD (198 << 8)                    /* the right side reads 9 ms further back (Q8 samples) */
#define DL_IN 16384                             /* the voices into the line, Q15: half (headroom for the loop) */
#define DL_DCK 280                              /* the DC block: 30 Hz at 22.05 kHz, Q15 */
#define DL_W1 12467522u                         /* the wow: 0.5 Hz, a block's step (2^32 * 256 / 44100 * Hz) */
#define DL_W2 4737658u                          /* .. and 0.19 Hz beside it: not quite periodic */
#define DL_W3 152103765u                        /* the flutter: 6.1 Hz */
_Static_assert(77175u + 51u + 198u + 2u < DL_LEN, "DELAY: the longest read stays in the line");

static int16_t dl_buf[DL_LEN] __attribute__((section(".pool")));
static struct {
    uint32_t w, quiet;                          /* the write index; half-rate samples with silence in and written */
    uint32_t p1, p2, p3;                        /* the wow's and the flutter's phases */
    int32_t d, m;                               /* TIME gliding to its target, the wow at the block's end (Q8 samples) */
    int32_t x, lp, le, dc, de, ml, mr;          /* the input's last sample; the loop's low-pass and DC block (and
                                                 * their steps' remainders); the last wet out, left / right */
    int32_t l[DL_NC], r[DL_NC];                 /* the block's echoes at half the rate */
} dl __attribute__((section(".pool")));

/* the parameters, 0..127, written by the main loop */
static volatile int32_t dl_time = 88, dl_fb = 64, dl_wear = 50, dl_mix = 100;   /* 717 ms, 55 %, return 79 % (the tracks' DLY 0: nothing goes in) */

/* main loop: the delay gives nothing out (MIX 0, or the line silent and nothing coming in) */
static int tape_idle(void) { return dl_mix <= 0 || dl.quiet >= DL_LEN; }

/* a read d (Q8 samples) behind the write index w, between samples */
static inline int32_t tape_read(uint32_t w, int32_t d)
{
    int32_t i0 = (int32_t)w - (d >> 8), f = d & 255, b0, b1;
    i0 = i0 < 0 ? i0 + (int32_t)DL_LEN : i0;
    b0 = dl_buf[i0];
    b1 = dl_buf[i0 ? i0 - 1 : (int32_t)DL_LEN - 1];   /* one further back */
    return b0 + (((b1 - b0) * f + 128) >> 8);
}

/* the audio ISR: n (= 2 DL_NC) frames of the DLY bus in (mono) -> the echoes added to out (L R) and to the REV bus */
static void tape_render(const int32_t *in, int32_t *out, int32_t *rev, uint32_t n)
{
    uint32_t i, any = 0, w;
    int32_t mix = dl_mix, wear = dl_wear, fbk = dl_fb;
    int32_t target, d0, d1, m1, a, tc, g, ig, fb;
    int32_t xp = dl.x, lp = dl.lp, le = dl.le, dc = dl.dc, de = dl.de, ml = dl.ml, mr = dl.mr;
    if (n != 2u * DL_NC || mix <= 0)
        return;                                 /* MIX 0: the dry alone; the line waits as it is */
    for (i = 0; i < 2u * DL_NC && !any; i++)
        any = in[i] != 0;
    if (!any && dl.quiet >= DL_LEN)
        return;                                 /* the line silent, nothing coming in: nothing to do */
    wear = clamp(wear, 0, 127);
    fbk = clamp(fbk, 0, 127);

    /* this block's settings: TIME (gliding), the wow, the loop's tone, drive and feedback */
    target = (int32_t)DL_MS[clamp(dl_time, 0, 127)] * 5645;   /* ms -> Q8 samples at 22.05 kHz */
    if (!dl.d)
        dl.d = target;                          /* (the first block: no glide from nothing) */
    d0 = dl.d;
    d1 = d0 + ((target - d0) >> 3);             /* ~50 ms to a new TIME */
    dl.d = d1;
    dl.p1 += DL_W1, dl.p2 += DL_W2, dl.p3 += DL_W3;
    a = (10 + wear) * 94;                       /* the wow's reach, Q8 samples: ~0.17 .. ~2.3 ms */
    m1 = wear_wow(dl.p1, dl.p2, dl.p3, a);      /* (wear.c) */
    tc = 28300 - wear * 148;                    /* the loop's low-pass, Q15: ~7 kHz .. ~1.2 kHz */
    g = 4096 + wear * 26;                       /* the saturation's drive, Q12: 1x .. 1.8x */
    ig = (int32_t)(134217728u / (uint32_t)g);   /* .. and the level made up after it, Q15 */
    fb = fbk * 284;                             /* FEEDBACK, Q15: 0 .. 1.1 */

    w = dl.w;
    {
        int32_t D0 = d0 + dl.m, dD = (d1 + m1) - D0;
        for (i = 0; i < DL_NC; i++) {
            int32_t x = (xp + 2 * in[2u * i] + in[2u * i + 1u]) >> 2, D = D0 + ((dD * (int32_t)i) >> 7);
            int32_t y = tape_read(w, D), v, e;
            xp = in[2u * i + 1u];
            x = mulq15(clamp(x, -65535, 65535), DL_IN);
            dl.l[i] = y;
            dl.r[i] = tape_read(w, D + DL_SPREAD);
            e = (y - lp) * tc + le, le = e & 32767, lp += e >> 15;          /* darker each time round */
            v = sh_tz(lp * fb, 15);                                         /* FEEDBACK */
            v = wear_sat(v, g, ig);                                         /* the tape's saturation (wear.c) */
            e = (v - dc) * DL_DCK + de, de = e & 32767, dc += e >> 15;      /* the DC block */
            v = clamp(soft_knee(x + v - dc, 16384), -32767, 32767);         /* in with the input: never clips */
            dl_buf[w] = (int16_t)v;
            dl.quiet = (v | x) ? 0u : dl.quiet + 1u;
            if (++w == DL_LEN) w = 0;
        }
    }
    dl.w = w, dl.m = m1, dl.x = xp, dl.lp = lp, dl.le = le, dl.dc = dc, dl.de = de;

    /* back to 44.1 kHz, between samples, at the return's level (MIX), into out and into the REV bus */
    {
        int32_t wg = mix * 258;
        for (i = 0; i < DL_NC; i++) {
            int32_t wl = dl.l[i], wr = dl.r[i], *o = out + 4u * i, a = mulq15((ml + wl) >> 1, wg),
                    b = mulq15((mr + wr) >> 1, wg), c = mulq15(wl, wg), d = mulq15(wr, wg);
            o[0] += a, o[1] += b, o[2] += c, o[3] += d;
            rev[2u * i] += (a + b) >> 1;        /* (the echoes, both sides, into SPACE) */
            rev[2u * i + 1u] += (c + d) >> 1;
            ml = wl;
            mr = wr;
        }
    }
    dl.ml = ml, dl.mr = mr;
}
