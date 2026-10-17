/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's LFO: one a track (the LFO page: RATE, DEPTH, DESTINATION, SHAPE; the track's, saved with the project, the
 * slots and the sounds).
 *   RATE: free-running (never restarted by a note), ~60 s a cycle (0) to ~20 Hz (127), exponential
 *   DEPTH: 0..100 %; 0: off (its phase runs on, nothing else is done)
 *   DESTINATION: PITCH (100 %: +-1 octave), VOLUME (LEVEL), PAN, the source's four knobs (KNOB 1..4: the track's source
 *   now, by its names), REV SEND, GRAIN SEND, TRK CUTOFF (the track filter's, TRACK). It moves the destination around its knob, within the knob's range (100 %:
 *   +-half of it), the knob itself not moved: PITCH through VP16 (voices.c), a knob through lfo_params (a voice's own
 *   copy of the four), VOLUME, PAN and the sends through grimm.c's mix (its gains ramped over the block)
 *   SHAPE: SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE, RANDOM (a value a cycle), DRIFT (a smooth wander, a new value a
 *   cycle); the output follows through a ~3 ms smoothing: SQUARE, RANDOM and the saws' ends step without a click
 * Audio ISR, a control step (ctl_hook: lfo_ctl); the main loop reads the phase (the picture's dot). */

enum { LD_PITCH, LD_VOLUME, LD_PAN, LD_K1, LD_K2, LD_K3, LD_K4, LD_REV, LD_GRAIN, LD_CUT, NLFODEST };   /* (LD_CUT:
                                                 * the track filter's CUTOFF, TRACK) */
enum { LS_SINE, LS_TRI, LS_SAWUP, LS_SAWDN, LS_SQR, LS_RND, LS_DRIFT, NLFOSHAPE };
static const char *const LFO_SHAPE_NAME[NLFOSHAPE] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM",
                                                      "DRIFT"};
static const char *const LFO_SHAPE_SHORT[NLFOSHAPE] = {"SIN", "TRI", "SAWUP", "SAWDN", "SQR", "RND", "DRIFT"};   /* (slabs) */
static const char *const LFO_DEST_NAME[NLFODEST] = {"PITCH", "VOLUME", "PAN", 0, 0, 0, 0, "REV SEND", "GRAIN SEND",
                                                    "TRK CUTOFF"};
#define LFO_SMOOTH 7045                         /* the output's one-pole a control step (Q15): ~3 ms */

static volatile uint32_t lfo_ph[NT];            /* audio: the phase (a cycle: 2^32) */
static int32_t lfo_a[NT], lfo_b[NT];            /* audio: RANDOM's value, DRIFT's from and to (Q15) */
static volatile int32_t lfo_amt[NT];            /* audio: the output times DEPTH, smoothed (Q15, +-32767; 0: off) */
static int32_t lfo_pm[NV][4];                   /* audio: a voice's four, one of them moved */
static volatile uint8_t lk_m[NT], lk_v[NT][4];  /* audio: the step playing's locks (seq.c): which of the four, their
                                                 * values (the knob's in their place while it plays) */
static uint32_t lfo_rng = 0x2545F491u;

static uint32_t lfo_hz_q16(int32_t rate)        /* RATE -> 2^(rate x 10.23 / 127), Q16: x 1/60 Hz (1 .. ~1200) */
{
    return pow2_q16(clamp(rate, 0, 127) * 1965 / 127);
}
static int32_t lfo_shape(uint32_t s, uint32_t ph, int32_t a, int32_t b)   /* SHAPE s at phase ph: Q15 */
{
    int32_t x = (int32_t)(ph >> 16), f, k;
    switch (s) {
    case LS_TRI: return x < 32768 ? x * 2 - 32767 : (65535 - x) * 2 - 32767;
    case LS_SAWUP: return x - 32768;
    case LS_SAWDN: return 32767 - x;
    case LS_SQR: return ph < 0x80000000u ? 32767 : -32767;
    case LS_RND: return a;
    case LS_DRIFT:                              /* from a to b over the cycle, smoothstep */
        f = x >> 1;
        k = (int32_t)(((f * f) >> 15) * (98304 - 2 * f) >> 15);
        return a + (int32_t)(((int64_t)(b - a) * k) >> 15);
    default: return sine_i(ph);
    }
}
static int32_t lfo_rand(void)
{
    uint32_t s = lfo_rng;
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    lfo_rng = s;
    return (int32_t)(s >> 16) - 32768;
}

static void lfo_ctl(void)                       /* audio, a control step: each track's LFO on */
{
    uint32_t t, ph;
    int32_t d, x, a;
    for (t = 0; t < NT; t++) {
        ph = lfo_ph[t];
        lfo_ph[t] = ph + (uint32_t)(((uint64_t)51942u * lfo_hz_q16(trk[t].lfo_rate)) >> 16);   /* (1/60 Hz: 51942 a
                                                 * control step) */
        if (lfo_ph[t] < ph) {                   /* a new cycle: RANDOM's value, DRIFT's next */
            lfo_a[t] = clamp(trk[t].lfo_shape, 0, NLFOSHAPE - 1) == LS_DRIFT ? lfo_b[t] : lfo_rand();
            lfo_b[t] = lfo_rand();
        }
        d = clamp(trk[t].lfo_depth, 0, 100);
        a = lfo_amt[t];
        if (!d && !a) {                         /* off: nothing to do */
            lfo_p16[t] = 0;
            continue;
        }
        x = d ? lfo_shape((uint32_t)clamp(trk[t].lfo_shape, 0, NLFOSHAPE - 1), lfo_ph[t], lfo_a[t], lfo_b[t]) * d / 100 : 0;
        a += ((x - a) * LFO_SMOOTH) >> 15;
        if (!d && a > -64 && a < 64) a = 0;     /* (DEPTH 0: out, then off) */
        lfo_amt[t] = a;
        lfo_p16[t] = trk[t].lfo_dest == LD_PITCH ? a * 192 / 32767 : 0;
    }
}
/* voice v of track t (engine e): its four as the engine gets them (the step playing's locks in place of the knobs, one
 * moved by the LFO: a copy; else the track's) */
static const volatile int32_t *lfo_params(uint32_t v, uint32_t t, uint32_t e)
{
    int32_t a = lfo_amt[t], k = trk[t].lfo_dest - LD_K1, mx, i;
    uint32_t lm = lk_m[t];
    if (((!a || k < 0 || k > 3) && !lm) || v >= NV) return trk[t].p[e];
    for (i = 0; i < 4; i++)
        lfo_pm[v][i] = lm >> i & 1u ? clamp(lk_v[t][i], 0, ENGINES[e]->max[i]) : trk[t].p[e][i];
    if (!a || k < 0 || k > 3) return lfo_pm[v];
    mx = ENGINES[e]->max[k];
    lfo_pm[v][k] = clamp(lfo_pm[v][k] + (a * mx + (a < 0 ? -32767 : 32767)) / 65534, 0, mx);
    return lfo_pm[v];
}
static int32_t lfo_move(uint32_t t, uint32_t dest, int32_t v, int32_t mx)   /* track t's VOLUME, PAN or a send: v moved */
{
    int32_t a = lfo_amt[t];
    if (!a || trk[t].lfo_dest != (int32_t)dest) return v;
    return clamp(v + (a * mx + (a < 0 ? -32767 : 32767)) / 65534, 0, mx);
}
