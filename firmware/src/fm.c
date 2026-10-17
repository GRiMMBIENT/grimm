/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's FM engine: Felucca's FM6 (hugelton/Felucca firmware/src/eng_fm6.c), whose synthesis is msfa, Dexed's
 * 6-operator FM core, ported to integer C (fm6_core.c, Apache-2.0: LICENSES/Apache-2.0-msfa.txt). Taken from
 * eng_fm6.c: the patch through the macros (fm6_sync: MLVL, MEG, DTUN), a note's start (fm6_note_on) and a block
 * (fm6_render: the note's log frequency, the operators, the ~7 Hz DC blocker, the amplitude ramp). Not taken: the
 * patch formats and SysEx, the factory slots F1..F8 (GR!MM has its own patches: tools/gen_fm_patches.py), the voice
 * bank in flash, the editor, user presets, ALG / FB / MRAT / VMOD, unison.
 * GR!MM's: each track its own patch (track_t's fm_alg, fm_fb, fmo: FM ALGO and FM OP edit it; PATCH loads a built-in one
 * into it: fm_builtin), laid over the built-in patch for what is not edited (the envelopes' levels and last rates, the
 * scaling, the pitch envelope, the LFO); edits while notes sound: RATIO retunes them, LEVEL and FEEDBACK glide (a
 * level a block, a feedback step a block, its history kept continuous), a new ALGO only for new notes (grimm.c
 * restarts the track's: they fade). Four macros on KNOB 1..4, GR!MM's drone envelope over the patch (the operators stay keyed down while the
 * voice sounds: the patch's own envelopes shape the attack and the timbre, GR!MM's 3 s / 6 s the level), velocity
 * fixed (100), the engine interface of voices.c.
 *   PATCH   one of GR!MM's patches (FM_PATCH)
 *   BRIGHT  the output level of every operator that is not a carrier, -36 .. +36 dB (Felucca's MLVL): 64 = as made
 *   DECAY   the modulators' envelope rates (Felucca's MEG): above 64 slower (the brightness lasts), below faster
 *   DETUNE  the carriers spread apart in pitch, up to ~+-36 cents between the outer ones (Felucca's DTUN)
 * State: a note per voice (fm_note, as Felucca's per-part engine memory), per track the patch through the macros and
 * the LFO (once a control block: voices.c vblock). */
#include "fm6_core.c"
#include "grimm_fm.h"                           /* tools/gen_fm_patches.py: FM_NPATCH, FM_PATCH, FM_PATCH_NAME */

#define FM_LV 16000                             /* a carrier at full: ~8000 (Q15), as a VA voice */
#define FM_VEL 100

static fm6_note_t fm_note[NV];                  /* a voice's note (by its index in vc) */
static uint32_t fm_vgen[NV];                    /* a voice's: the patch (fm_trk's gen) it follows, */
static int16_t fm_vol[NV][6];                   /* its operators' output levels gliding there (env outlevel), */
static uint8_t fm_valg[NV], fm_vfb[NV], fm_vset[NV];   /* the algorithm it began with, its feedback now, gliding */
enum { FO_RATIO, FO_FINE, FO_LEVEL, FO_ATK, FO_DEC };
static struct {                                 /* a track's patch through the macros: the audio ISR's */
    uint8_t p[FP_SIZE + 1u];
    int32_t made[NPARAM];                       /* the parameters it was made with */
    uint32_t h, gen;                            /* .. the track's patch it was made from (fm_hash); + 1 a change */
    int32_t dt[6];                              /* DETUNE: per operator, Q24 log2 */
    uint8_t alg, fb, ok;
    fm6_lfo_t lfo;
    int32_t lv, ld;                             /* this block's LFO value and delay (Q24) */
    uint32_t stamp;                             /* the voices.c block it was last run for */
} fm_trk[NT];

static void fm_builtin(uint32_t t, int32_t pt)   /* main loop: track t's patch := built-in PATCH pt, unedited */
{
    const uint8_t *p = FM_PATCH[clamp(pt, 0, FM_NPATCH - 1)];
    uint32_t i;
    trk[t].fm_alg = p[FP_ALG] & 31u, trk[t].fm_fb = p[FP_FB] & 7u;
    for (i = 0; i < 6u; i++) {                  /* (OP1 first: the patch has the sixth first) */
        const uint8_t *op = p + (5u - i) * FP_OP;
        trk[t].fmo[i][FO_RATIO] = op[FP_FC] & 31u, trk[t].fmo[i][FO_FINE] = op[FP_FF] % 100u;
        trk[t].fmo[i][FO_LEVEL] = op[FP_OL] % 100u;
        trk[t].fmo[i][FO_ATK] = 99 - op[FP_R1] % 100u, trk[t].fmo[i][FO_DEC] = 99 - op[FP_R1 + 1] % 100u;
    }
}
static uint32_t fm_hash(uint32_t t)             /* track t's patch in a word (a change: the patch made again) */
{
    uint32_t h = (uint32_t)trk[t].fm_alg * 31u + (uint32_t)trk[t].fm_fb, i, k;
    for (i = 0; i < 6u; i++)
        for (k = 0; k < 5u; k++) h = h * 31u + (uint32_t)trk[t].fmo[i][k];
    return h;
}
/* the patch through the macros (eng_fm6.c fm6_sync; audio ISR, cheap when nothing changed): the built-in PATCH, the
 * track's own over it, then the macros */
static void fm_sync(uint32_t t, const volatile int32_t *e)
{
    int32_t q[NPARAM], meg, step;
    uint32_t k, j, car, n = 0, h = fm_hash(t);
    uint8_t *p = fm_trk[t].p;
    static const int8_t SPREAD[6] = {0, 1, -1, 2, -2, 3};   /* carriers 0, +1, -1, +2, -2, +3 steps apart */
    for (k = 0; k < NPARAM; k++)
        q[k] = e[k];
    if (fm_trk[t].ok && fm_trk[t].h == h) {
        for (k = 0; k < NPARAM && fm_trk[t].made[k] == q[k]; k++)
            ;
        if (k == NPARAM)
            return;
    }
    for (k = 0; k < FP_SIZE; k++)
        p[k] = FM_PATCH[clamp(q[0], 0, FM_NPATCH - 1)][k];
    p[FP_ALG] = (uint8_t)clamp(trk[t].fm_alg, 0, 31), p[FP_FB] = (uint8_t)clamp(trk[t].fm_fb, 0, 7);
    for (k = 0; k < 6u; k++) {                  /* (the track's operators, OP1 first) */
        uint8_t *op = p + (5u - k) * FP_OP;
        op[FP_FC] = (uint8_t)clamp(trk[t].fmo[k][FO_RATIO], 0, 31), op[FP_FF] = (uint8_t)clamp(trk[t].fmo[k][FO_FINE], 0, 99);
        op[FP_OL] = (uint8_t)clamp(trk[t].fmo[k][FO_LEVEL], 0, 99);
        op[FP_R1] = (uint8_t)(99 - clamp(trk[t].fmo[k][FO_ATK], 0, 99)), op[FP_R1 + 1] = (uint8_t)(99 - clamp(trk[t].fmo[k][FO_DEC], 0, 99));
    }
    fm_trk[t].h = h, fm_trk[t].gen++;
    fm_trk[t].alg = (uint8_t)(p[FP_ALG] & 31u);
    fm_trk[t].fb = (uint8_t)clamp(p[FP_FB], 0, 7);
    car = fm6_carriers(fm_trk[t].alg);
    meg = (clamp(q[2], 0, 127) - 64) * 40 / 64;              /* DECAY: rate steps (+ slower) */
    step = clamp(q[3], 0, 127) * (12 * 13981) / 127;          /* DETUNE: up to 12 cents a step (1 cent = 13981) */
    for (k = 0; k < 6u; k++) {
        uint32_t ki = 5u - k;                                 /* OP1 first: carriers in their order */
        uint8_t *op = p + ki * FP_OP;
        fm_trk[t].dt[ki] = 0;
        if ((car >> ki) & 1u) {
            fm_trk[t].dt[ki] = SPREAD[n++] * step;
            continue;
        }
        for (j = 0; j < 4u; j++)
            op[FP_R1 + j] = (uint8_t)clamp(op[FP_R1 + j] - meg, 0, 99);
    }
    fm6_lfo_reset(&fm_trk[t].lfo, p);
    for (k = 0; k < NPARAM; k++)
        fm_trk[t].made[k] = q[k];
    fm_trk[t].ok = 1;
}

/* once a control block and track: the macros, the LFO (eng_fm6.c fm6_block) */
static void fm_track(uint32_t t, const volatile int32_t *e)
{
    if (fm_trk[t].stamp == vblock && fm_trk[t].ok)
        return;
    fm_trk[t].stamp = vblock;
    fm_sync(t, e);
    fm_trk[t].lv = fm6_lfo_sample(&fm_trk[t].lfo);
    fm_trk[t].ld = fm6_lfo_delay(&fm_trk[t].lfo);
}

static void fm_begin(voice_t *v)                /* a note from silence (eng_fm6.c fm6_note_on) */
{
    uint32_t t = v->trk;
    const uint8_t *p;
    fm_sync(t, trk[t].p[ENG_FM]);
    p = fm_trk[t].p;
    fm6_note_init(&fm_note[v - vc], p, (int32_t)v->note + p[FP_TRNSP] - 24, FM_VEL, 1);
    fm6_lfo_key(&fm_trk[t].lfo);
    fm_vgen[v - vc] = fm_trk[t].gen, fm_vset[v - vc] = 0;
    fm_valg[v - vc] = fm_trk[t].alg, fm_vfb[v - vc] = fm_trk[t].fb;
}
/* the patch edited while voice v sounds: its ratios now; its levels to glide to (as fm6_note_init has them) */
static void fm_follow(voice_t *v, const uint8_t *p)
{
    uint32_t vi = (uint32_t)(v - vc), k;
    int32_t note = clamp((int32_t)v->note + p[FP_TRNSP] - 24, 0, 127);
    fm6_note_t *n = &fm_note[vi];
    for (k = 0; k < 6u; k++) {
        const uint8_t *op = p + k * FP_OP;
        int32_t ol = fm6_scaleout(op[FP_OL]) + fm6_scale_level(note, op);
        ol = ((ol > 127 ? 127 : ol) << 5) + fm6_scale_vel(FM_VEL, op[FP_KVS]);
        if (!((n->fixed >> k) & 1u)) n->base[k] = fm6_osc_base(op, note, 0);
        fm_vol[vi][k] = (int16_t)(ol < 0 ? 0 : ol);
    }
    fm_vset[vi] = 1;
}
static void fm_glide(uint32_t vi, uint32_t fb)  /* a block: the levels a step (~0.75 dB) on, the feedback one */
{
    fm6_note_t *n = &fm_note[vi];
    uint32_t k, more = 0;
    for (k = 0; k < 6u && fm_vset[vi]; k++) {
        fm6_env_t *e = &n->env[k];
        int32_t d = fm_vol[vi][k] - e->outlevel;
        d = d > 32 ? 32 : d < -32 ? -32 : d;
        if (!d) continue;
        e->outlevel = (int16_t)(e->outlevel + d), e->level += d * 65536, e->target += d * 65536;
        if (e->level < 0) e->level = 0;
        more = 1;
    }
    fm_vset[vi] = (uint8_t)more;
    if (fm_vfb[vi] != fb) {                     /* (its history scaled: the feedback's input goes on as it was) */
        uint32_t f0 = fm_vfb[vi], f1 = f0 < fb ? f0 + 1u : f0 - 1u, s0 = f0 ? 8u - f0 : 16u, s1 = f1 ? 8u - f1 : 16u;
        if (s1 < s0) n->fb[0] >>= s0 - s1, n->fb[1] >>= s0 - s1;
        else if (s1 == s0 + 1u) n->fb[0] *= 2, n->fb[1] *= 2;
        fm_vfb[vi] = (uint8_t)f1;
    }
}

/* CTL frames of voice v, added to mono[] (eng_fm6.c fm6_render, with GR!MM's envelope as the ramp) */
static void fm_block(voice_t *v, const volatile int32_t *e, int32_t *mono, int32_t amp0, int32_t amp1)
{
    uint32_t t = v->trk, i;
    fm6_note_t *n = &fm_note[v - vc];
    const uint8_t *p;
    int32_t bus[FM6_N], lf, lvl, a, da, dc;
    fm_track(t, e);
    p = fm_trk[t].p;
    lf = fm6_note_logfreq(VP16(v) + (p[FP_TRNSP] - 24) * 16, tune_f);   /* (TUNE: the fine factor) */
    lvl = (clamp(e[1], 0, 127) - 64) * 24;                    /* BRIGHT: microsteps (MLVL) */
    if (fm_vgen[v - vc] != fm_trk[t].gen) {                   /* (edited while it sounds) */
        fm_vgen[v - vc] = fm_trk[t].gen;
        fm_follow(v, p);
    }
    fm_glide((uint32_t)(v - vc), fm_trk[t].fb);
    if (!fm6_note_compute(n, p, bus, fm_trk[t].lv, fm_trk[t].ld, lf, fm_valg[v - vc], fm_vfb[v - vc], fm_trk[t].dt,
                          lvl << 16))
        return;                                               /* every carrier below the threshold */
    a = mulq15(amp0, FM_LV);
    da = (mulq15(amp1, FM_LV) - a) >> FM6_LG_N;
    dc = n->dc;
    for (i = 0; i < FM6_N; i++) {
        int32_t x = bus[i] - dc;
        dc += x >> 10;                                        /* a one-pole high-pass, ~7 Hz: FM puts sidebands at 0 Hz */
        a += da;
        mono[i] += (int32_t)(((int64_t)x * a) >> 26);
    }
    n->dc = dc;
}

static void fm_text(const volatile int32_t *e, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = e[k];
    if (k == 0u) {
        put_s(s, FM_PATCH_NAME[clamp(v, 0, FM_NPATCH - 1)]);
    } else if (k == 3u) {
        put_u(s, (uint32_t)v);
    } else {                                                  /* BRIGHT, DECAY: from the patch as made, +-64 */
        s[0] = v < 64 ? '-' : '+';
        put_u(s + 1, (uint32_t)(v < 64 ? 64 - v : v - 64));
    }
}

static const engine_t FM_ENGINE = {"FM", {"PATCH", "BRIGHT", "DECAY", "DETUNE"}, {FM_NPATCH - 1, 127, 127, 127},
                                   fm_begin, fm_block, fm_text};
