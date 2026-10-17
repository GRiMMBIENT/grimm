/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's PHYS source: Felucca's PHYS engine (hugelton/Felucca firmware/src/eng_phys.c), physical models. Its DSP is
 * Felucca's fixed-point port of DaisySP (Electrosmith, Emilie Gillet; MIT: phys_dsp.c) and of Rings' sympathetic
 * strings (Emilie Gillet; MIT: phys_symp.c), both as Felucca has them, unchanged. Taken over from eng_phys.c: the four
 * models, their parameters' meanings, the SYMP chords, the output gains and soft knee, the state reset. Not taken:
 * presets, the legacy loading, Felucca's per-part engine memory (here a slot a voice).
 *   MODEL      MODAL (a bank of band-pass modes struck or bowed: bells, bars, plates, metal), STRING (an extended
 *              Karplus-Strong string, plucked or bowed), MEMBRANE (a struck drum head), SYMPATH (a string with three
 *              sympathetic strings ringing along)
 *   STRUCTURE  MODAL: the partials, harmonic .. more and more inharmonic (metal); STRING: a curved bridge (buzzy)
 *              .. dispersion (stiff); MEMBRANE: HARM, an ideal head .. a loaded one; SYMPATH: CHORD of its strings
 *   DAMP       the decay (at the top it rings on)
 *   BOW        MODAL, STRING: 0 struck / plucked, above sustained (bowed, blown) at that level; MEMBRANE: BEND,
 *              the pitch falling after the strike; SYMPATH: BUZZ, the curved bridge
 * Fixed at Felucca's defaults: BRIT 80, POS 20 (SYMPATH: SYMP), ACC 90 at a velocity of 100, EXC 0.
 * State: ph_slot[voice] (~4 KB each, RAM); voice_t is unused but for its random state. */

#include "phys_dsp.c"
#include "phys_symp.c"

enum { PM_MODAL, PM_STRING, PM_MEMB, PM_SYMP, PM_COUNT };
#define PHYS_BRIT 80                            /* Felucca's defaults for the knobs GR!MM leaves out */
#define PHYS_POS 20
#define PHYS_ACC 90
#define PHYS_VEL 100
/* output: model 1.0 -> this (Felucca's VOICE_FS scale), then a soft knee; per model (a strike of all the modes in
 * phase peaks high but is short) */
static const int32_t PHYS_GAIN[PM_COUNT] = {28000, 12000, 90000, 12000};

typedef struct {
    uint8_t model;                              /* PM_* the state belongs to */
    union {
        px_modal_t m;                           /* MODAL, MEMB */
        px_string_t s;
        px_symp_t y;
    } u;
} phys_slot_t;
static phys_slot_t ph_slot[NV];

static const char *const N_PHYS_MODEL[PM_COUNT] = {"MODAL", "STRING", "MEMBRANE", "SYMPATH"};
static const char *const N_PHYS_CHORD[8] = {"OCT", "5TH", "4TH", "MAJ", "MIN", "SUS", "7TH", "ROOT"};
/* SYMP: the sympathetic strings above the note, cents (CHORD's names) */
static const int16_t PHYS_CHORDS[7][PX_NSYMP] = {
    {-1200, 1200, 2400}, {700, 1200, 1900}, {500, 1200, 1700}, {400, 700, 1200},
    {300, 700, 1200}, {200, 700, 1400}, {400, 700, 1000},
};
static const int8_t PHYS_DETUNE[PX_NSYMP] = {1, -1, 0};   /* 1/16 semitone (6 cents): the strings beat a little */

/* a clean state for model md */
static void phys_reset(phys_slot_t *S, uint32_t md, uint32_t seed)
{
    uint32_t i, *w = (uint32_t *)&S->u;
    for (i = 0; i < sizeof S->u / 4u; i++)
        w[i] = 0;
    S->model = (uint8_t)md;
    seed |= 1u;
    switch (md) {
    case PM_STRING:
    case PM_SYMP:                               /* (the main string is the union's px_string_t) */
        S->u.s.rng = seed;
        S->u.s.src = 1u << 30;
        break;
    default:
        S->u.m.rng = seed;
        break;
    }
}
static uint32_t phys_seed(voice_t *v)           /* (xorshift32 on the voice's random state) */
{
    uint32_t s = (uint32_t)v->rng;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    v->rng = (int32_t)s;
    return s;
}

static uint32_t phys_model(const volatile int32_t *p) { return (uint32_t)clamp(p[0], 0, PM_COUNT - 1); }

static void phys_begin(voice_t *v)              /* a note from silence: from rest, struck (phys_note_on) */
{
    phys_slot_t *S = &ph_slot[v - vc];
    uint32_t md = phys_model(trk[v->trk].p[ENG_PHYS]);
    phys_reset(S, md, phys_seed(v));
    if (md == PM_MODAL || md == PM_MEMB)
        S->u.m.trig = 1;
    else
        S->u.s.trig = 1;
}

/* SYMP: the sympathetic strings' pitches (cycles Q32) by CHORD: a chord above the note, or ROOT (C) */
static void phys_symp_pitches(int32_t chord, int32_t pitch16, uint32_t *f)
{
    uint32_t c = (uint32_t)clamp(chord, 0, 127) >> 4, k;   /* 0..7 */
    for (k = 0; k < PX_NSYMP; k++) {
        int32_t p16 = PHYS_DETUNE[k];
        p16 += c < 7u ? pitch16 + ((PHYS_CHORDS[c][k] * 41) >> 8)   /* cents -> 1/16 semitones (0.16) */
                      : (48 + (k == 1u ? 7 : k == 2u ? 12 : 0)) * 16;
        f[k] = vinc(p16);
    }
}

/* CTL frames of voice v, added to mono[] (phys_render) */
static void phys_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    phys_slot_t *S = &ph_slot[v - vc];
    uint32_t md = phys_model(p), i, f0 = vinc(VP16(v));
    int32_t y[CTL], ax[CTL], damp = amp1 - amp0;
    int32_t strc = clamp(p[1], 0, 127) * 516, brit = PHYS_BRIT * 516, dmp = clamp(p[2], 0, 127) * 516;
    int32_t pos = PHYS_POS, k4 = clamp(p[3], 0, 127), bow = k4 * 206;   /* BOW: Dust at up to 0.4 */
    int32_t acc = clamp(PHYS_ACC * PHYS_VEL * 4, 0, 65536);   /* ACC at the velocity, Q16 */
    int32_t g = PHYS_GAIN[md], sh = bow && (md == PM_MODAL || md == PM_STRING) ? 18 : 20;   /* (bowed: x 4, as loud as
                                                                                    * the other sources) */
    if (S->model != md) {                       /* MODEL changed under a sounding note: struck anew */
        phys_reset(S, md, phys_seed(v));
        if (md == PM_MODAL || md == PM_MEMB)
            S->u.m.trig = 1;
        else
            S->u.s.trig = 1;
    }
    switch (md) {
    case PM_MODAL: {
        px_modal_blk_t B;
        px_modal_block(&B, &S->u.m, f0, strc, brit, dmp, acc, pos * 129, bow);   /* pos 0..1/4 */
        px_modal_run(&B, &S->u.m, y, ax, CTL);
        break;
    }
    case PM_MEMB: {
        px_modal_blk_t B;
        px_memb_block(&B, &S->u.m, f0, strc, brit, dmp, acc, pos * 516, k4 * 6192);   /* BEND 0..12 st */
        px_modal_run(&B, &S->u.m, y, ax, CTL);
        break;
    }
    case PM_STRING: {
        px_string_blk_t B;
        px_string_block(&B, &S->u.s, f0, strc, brit, dmp, acc, bow, pos * 516);   /* 0..1/2 period */
        px_string_excite(&B, &S->u.s, ax, CTL);
        px_string_run(&B, &S->u.s, ax, y, CTL);
        break;
    }
    default: {                                  /* SYMP */
        px_string_blk_t B;
        uint32_t f[PX_NSYMP];
        phys_symp_pitches(p[1], VP16(v), f);
        px_string_block(&B, &S->u.y.s, f0, 16384 - k4 * 129, brit, dmp, acc, 0, 0);   /* BUZZ: STRC 0.25 .. 0 */
        px_string_excite(&B, &S->u.y.s, ax, CTL);
        px_string_run(&B, &S->u.y.s, ax, y, CTL);
        px_symp_block(&S->u.y, f, brit, dmp, pos * 516);
        px_symp_run(&S->u.y, y, y, CTL);
        break;
    }
    }
    for (i = 0; i < (uint32_t)CTL; i++) {       /* (EXC 0: the exciter is not heard); the knee caps a strike */
        int32_t s = px_m(y[i], g, sh), a = s < 0 ? -s : s;
        if (a > 24000) {                        /* soft knee: only peaks saturate */
            a = 24000 + (softclip(clamp(a - 24000, 0, 1 << 20) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        mono[i] += mulq15(mulq15(s, amp_at(amp0, damp, (int32_t)i)), VOICE_LV) << 1;
    }
}

static void phys_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    uint32_t md = phys_model(p), pc = ((uint32_t)clamp(p[k], 0, 127) * 100u + 63u) / 127u;
    switch (k) {
    case 0: put_s(s, N_PHYS_MODEL[md]); return;
    case 1:
        if (md == PM_SYMP) put_s(s, N_PHYS_CHORD[(uint32_t)clamp(p[1], 0, 127) >> 4]);
        else put_s(put_u(md == PM_MEMB ? put_s(s, "HARM ") : s, pc), "%");
        return;
    case 3:
        if (md == PM_MEMB) s = put_s(s, "BEND ");
        else if (md == PM_SYMP) s = put_s(s, "BUZZ ");
        else if (!p[3]) { put_s(s, "STRUCK"); return; }
        put_s(put_u(s, pc), "%");
        return;
    default: put_s(put_u(s, pc), "%"); return;
    }
}

static const engine_t PHYS_ENGINE = {"PHYS", {"MODEL", "STRUCTURE", "DAMP", "BOW"}, {PM_COUNT - 1, 127, 127, 127},
                                     phys_begin, phys_block, phys_text};
