/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V2: FX's fifth and sixth pages, on the whole sound (grimm.c app_audio; the project's, store.c).
 *   MASTER: a filter, stereo: CUTOFF, RESONANCE, TYPE (LP / HP / BP: dsp.c's SVF and its outputs, as the tracks'),
 *     POSITION: PRE FX (the tracks' dry mix, before GRAIN's, DELAY's and SPACE's returns), POST REVERB (after them,
 *     before DRIVE and TAPE), END (after TAPE and PERFORM, the very last). The cutoff glides as the tracks' (~6 ms).
 *     LP with CUTOFF at the top, HP at the bottom: bypassed (nothing done). Each POSITION its own filter and its share
 *     of the sound: a new POSITION (or the filter opened, closed) fades in while the last one fades out, 8 blocks
 *     (~46 ms), the two running together only then: no click.
 *   DRIVE: after SPACE (and MASTER's POST REVERB), before TAPE. AMOUNT: the gain into it, 1x .. ~60x; TYPE: SOFT (tanh),
 *     FUZZ (tanh, lopsided: even harmonics), CLIP (hard), FOLD (a wavefolder); TONE: a low-pass after it (the top:
 *     none); MIX: the driven sound against the dry. Then a limiter on both sides together: the peak followed at once,
 *     let go over ~90 ms, the sound held at DR_LIM; no look-ahead, no buffer. AMOUNT 0: bypassed (nothing done). */
enum { MP_PRE, MP_POST, MP_END, NMPOS };        /* MASTER's POSITION */
enum { DT_SOFT, DT_FUZZ, DT_CLIP, DT_FOLD, NDTYPE };   /* DRIVE's TYPE */
static const char *const MP_NAME[NMPOS] = {"PRE FX", "POST REVERB", "END"};
static const char *const MP_SHORT[NMPOS] = {"PRE", "POST", "END"};
static const char *const DT_NAME[NDTYPE] = {"SOFT", "FUZZ", "CLIP", "FOLD"};
static volatile int32_t mf_cut = 127, mf_res, mf_type, mf_pos = MP_END;   /* MASTER's knobs (open: bypassed) */
static volatile int32_t dr_amt, dr_type, dr_tone = 127, dr_mix = 127;   /* DRIVE's (AMOUNT 0: bypassed) */
#define DR_LIM 30000                            /* the limiter's ceiling (Q15: ~-0.8 dB) */
#define DR_TOP 26000                            /* CLIP's and FOLD's (and FUZZ's offset's) level */
static int32_t mf_cs[NMPOS], mf_ic[NMPOS][2][2], mf_mx[NMPOS], dr_lp[2], dr_env;   /* audio: MASTER at each POSITION:
                                                 * its cutoff now, its states, its share (0 .. MF_ONE); DRIVE's TONE's,
                                                 * the limiter's peak */
#define MF_ONE 256
#define MF_STEP 32                              /* (a share's change a block: 8 blocks) */
/* one side's m frames (p: every other int, L or R) through the SVF c, its TYPE's output; the states in s (the chip:
 * one loop a TYPE, the coefficients and the states in registers) */
#define MF_RUN(OUT) for (; m; m--, p += 2) {                                                                          \
        int32_t in = *p >> 1, v3 = in - s2, v1 = (a1 * s1 + a2 * v3) >> 13, v2 = s2 + ((a2 * s1 + a3 * v3) >> 13), y;   \
        s1 = clamp(2 * v1 - s1, -150000, 150000), s2 = clamp(2 * v2 - s2, -150000, 150000), y = (OUT);             \
        *p = soft_knee(y, 60000) << 1;                                                                            \
    }
static void mf_side(int32_t *p, uint32_t m, const tsvf_t *c, int32_t k, uint32_t ty, int32_t *s)
{
    int32_t a1 = c->a1, a2 = c->a2, a3 = c->a3, s1 = s[0], s2 = s[1];
    if (ty == FT_LP) MF_RUN(v2)
    else if (ty == FT_HP) MF_RUN(in - ((k * v1) >> 12) - v2)
    else MF_RUN((k * v1) >> 12)
    s[0] = s1, s[1] = s2;
}
#undef MF_RUN
static void mf_side_mix(int32_t *p, uint32_t m, const tsvf_t *c, int32_t k, uint32_t ty, int32_t *s, int32_t a, int32_t da)
{                                               /* as mf_side, the filtered sound's share a (Q16 of MF_ONE) a frame, + da */
    int32_t a1 = c->a1, a2 = c->a2, a3 = c->a3, s1 = s[0], s2 = s[1];
    for (; m; m--, p += 2, a += da) {
        int32_t in = *p >> 1, v3 = in - s2, v1 = (a1 * s1 + a2 * v3) >> 13, v2 = s2 + ((a2 * s1 + a3 * v3) >> 13), y;
        s1 = clamp(2 * v1 - s1, -150000, 150000), s2 = clamp(2 * v2 - s2, -150000, 150000);
        y = soft_knee(ty == FT_LP ? v2 : ty == FT_HP ? in - ((k * v1) >> 12) - v2 : (k * v1) >> 12, 60000) << 1;
        *p += ((y - *p) * (a >> 16)) >> 8;
    }
    s[0] = s1, s[1] = s2;
}
static void master_filter(int32_t *out, uint32_t n, uint32_t at)   /* audio: MASTER at POSITION at (MP_), its share */
{
    uint32_t ty = (uint32_t)clamp(mf_type, 0, NFTYPE - 1), i, m;
    int32_t tgt = clamp(mf_cut, 0, 127) << 8, k;
    int32_t a0 = mf_mx[at], a1, cs, da;
    int open = ty == FT_LP ? tgt >= 127 << 8 : ty == FT_HP && tgt <= 0;   /* (LP at the top, HP at the bottom) */
    int32_t want = (uint32_t)clamp(mf_pos, 0, NMPOS - 1) == at && !open ? MF_ONE : 0;
    tsvf_t c;
    if (!a0 && !want) return;                   /* (not here: nothing done) */
    k = 8192 - clamp(mf_res, 0, 127) * 7 / 8 * 7600 / 127;
    a1 = a0 < want ? (a0 + MF_STEP < want ? a0 + MF_STEP : want) : (a0 - MF_STEP > want ? a0 - MF_STEP : want);
    mf_mx[at] = a1;
    if (!a0) {                                  /* (coming in: from its cutoff now, its states clear; it fades in) */
        mf_cs[at] = tgt;
        mf_ic[at][0][0] = mf_ic[at][0][1] = mf_ic[at][1][0] = mf_ic[at][1][1] = 0;
    }
    cs = mf_cs[at];
    da = (int32_t)(((a1 - a0) << 16) / (int32_t)n);
    for (i = 0; i < n; i += CTL) {
        cs += (tgt - cs) >> 3;
        tsvf_coef_k(&c, cs, k);
        m = n - i < CTL ? n - i : CTL;
        if (a0 == MF_ONE && a1 == MF_ONE) {     /* (all of it, as it mostly is) */
            mf_side(out + 2u * i, m, &c, k, ty, mf_ic[at][0]);
            mf_side(out + 2u * i + 1u, m, &c, k, ty, mf_ic[at][1]);
        } else {                                /* (fading in or out) */
            int32_t a = (a0 << 16) + da * (int32_t)i;
            mf_side_mix(out + 2u * i, m, &c, k, ty, mf_ic[at][0], a, da);
            mf_side_mix(out + 2u * i + 1u, m, &c, k, ty, mf_ic[at][1], a, da);
        }
    }
    mf_cs[at] = cs;
}

static inline int32_t dr_shape(uint32_t ty, int32_t y)   /* y (Q15, driven): the TYPE's curve */
{
    int32_t u;
    switch (ty) {
    case DT_FUZZ: return softclip(y + DR_TOP / 3) - softclip(DR_TOP / 3);   /* (the offset's own level taken off) */
    case DT_CLIP: return clamp(y, -DR_TOP, DR_TOP);
    case DT_FOLD:                               /* a triangle: past +-DR_TOP it turns back, again and again */
        u = (int32_t)(((uint32_t)(y + DR_TOP) + 0x80000000u - (0x80000000u % (4u * DR_TOP))) % (4u * DR_TOP));
        return u < 2 * DR_TOP ? u - DR_TOP : 3 * DR_TOP - u;
    default: return softclip(y);
    }
}
/* the driven sound of n frames (L R) into w: one loop a TYPE (the chip: no choice a sample); x clamped to +-2^17 first,
 * so the gain (Q8, to ~62x) stays in 32 bits */
#define DR_RUN(SH) for (i = 0; i < 2u * n; i++) {                                                                \
        int32_t y = (clamp(x[i], -131071, 131071) * g) >> 8;                                                      \
        w[i] = (SH);                                                                                            \
    }
static void dr_wet(const int32_t *x, int32_t *w, uint32_t n, uint32_t ty, int32_t g)
{
    uint32_t i;
    switch (ty) {
    case DT_FUZZ: DR_RUN(softclip(y + DR_TOP / 3) - softclip(DR_TOP / 3)) break;
    case DT_CLIP: DR_RUN(clamp(y, -DR_TOP, DR_TOP)) break;
    case DT_FOLD: DR_RUN(dr_shape(DT_FOLD, y)) break;
    default: DR_RUN(softclip(y)) break;
    }
}
#undef DR_RUN
static void drive_render(int32_t *out, uint32_t n)   /* audio: DRIVE and its limiter on the mix (n frames, L R) */
{
    int32_t a = clamp(dr_amt, 0, 127), g, tc, mx, env = dr_env, l0 = dr_lp[0], l1 = dr_lp[1];
    uint32_t ty = (uint32_t)clamp(dr_type, 0, NDTYPE - 1), i, j, m;
    if (!a) {                                   /* (bypassed: from silence when it comes back) */
        dr_env = dr_lp[0] = dr_lp[1] = 0;
        return;
    }
    g = (int32_t)(pow2_q16(a * 9) >> 8);        /* 2^(a / 21.3): 1x .. ~62x, Q8 */
    tc = dr_tone >= 127 ? 32768 : (int32_t)(pow2_q16((clamp(dr_tone, 0, 127) - 127) * 12) >> 1) + 160;   /* (Q15:
                                                 * open, then down an octave every 16 steps, ~100 Hz at 0) */
    mx = clamp(dr_mix, 0, 127) * 258;           /* (Q15: 127 is 32766, the dry all but gone) */
    for (j = 0; j < n; j += VB / 2u) {          /* (VB / 2 frames at a time: the driven sound in the DLY bus, free by
                                                 * now, VB ints) */
        int32_t *x = out + 2u * j;
        m = n - j < VB / 2u ? n - j : VB / 2u;
        dr_wet(x, dbus, m, ty, g);
        for (i = 0; i < m; i++) {
            int32_t xl = x[2u * i], xr = x[2u * i + 1u], yl = dbus[2u * i], yr = dbus[2u * i + 1u], al, ar;
            if (tc < 32768)                     /* TONE */
                l0 += ((yl - l0) >> 1) * tc >> 14, l1 += ((yr - l1) >> 1) * tc >> 14, yl = l0, yr = l1;
            if (mx < 32766)                     /* MIX */
                yl = xl + (((yl - xl) >> 2) * mx >> 13), yr = xr + (((yr - xr) >> 2) * mx >> 13);
            al = yl < 0 ? -yl : yl, ar = yr < 0 ? -yr : yr, al = al > ar ? al : ar;
            env = al > env ? al : env - (env >> 12);   /* the limiter: the peak at once, let go over ~90 ms */
            if (env > DR_LIM) {
                int32_t gl = (int32_t)(((uint32_t)DR_LIM << 15) / (uint32_t)env);   /* (Q15) */
                yl = (int32_t)(((int64_t)yl * gl) >> 15), yr = (int32_t)(((int64_t)yr * gl) >> 15);
            }
            x[2u * i] = yl, x[2u * i + 1u] = yr;
        }
    }
    dr_env = env, dr_lp[0] = l0, dr_lp[1] = l1;
}
