/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Tape wear, shared by DELAY (tape.c) and the TAPE effect (tapefx.c), as tape.c had it: the wow (two slow sines,
 * 0.5 and 0.19 Hz, not quite periodic) with a little flutter (6.1 Hz), and the saturation (a tanh with a drive and
 * the level made up after it). The design is X0X's TAPE delay's (see tape.c); the integer pieces are Felucca's
 * (dsp.c softclip, the rounding towards zero). */

/* the wow's sine: a full cycle over 2^32 -> -32767..32767 (a refined parabola, ~0.1 %: Dinghy's) */
static int32_t wear_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}
/* m / 2^sh rounded towards zero (Felucca's sh_tz: a product that feeds back must not floor) */
static inline int32_t wear_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }

/* the wow and flutter at phases p1 (0.5 Hz), p2 (0.19 Hz), p3 (6.1 Hz), reach a: about +-0.9 a */
static inline int32_t wear_wow(uint32_t p1, uint32_t p2, uint32_t p3, int32_t a)
{
    return (((wear_sine(p1) + mulq15(wear_sine(p2), 19661)) >> 1) * a + mulq15(wear_sine(p3), 3277) * a) >> 15;
}
/* the saturation: v driven by g (Q12, 4096 = 1x) through the tanh (wear_drive), made up by ig (Q15, 2^27 / g) */
static inline int32_t wear_drive(int32_t v, int32_t g) { return softclip(wear_tz(v * g, 12)); }
static inline int32_t wear_sat(int32_t v, int32_t g, int32_t ig) { return wear_tz(wear_drive(v, g) * ig, 15); }
