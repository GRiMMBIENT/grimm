/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's WHEEL source: Felucca's WHEEL engine (hugelton/Felucca firmware/src/eng_wheel.c, "Felucca's own design"), a
 * tonewheel-style additive organ. Taken over: nine sines a voice at the drawbar footages (16' 5 1/3' 8' 4' 2 2/3' 2'
 * 1 3/5' 1 1/3' 1'), free-running wheels, the fold above ~6 kHz, the 16 registrations and their soft normalisation,
 * the percussion (single trigger, on the 4' or the 2 2/3', normal / soft, fast / slow; the 1' bar off and the bars
 * -3 dB with it, as the real circuit), the key click (high-passed noise at the key contact and, half, at the release),
 * the overdrive (tanh) and the rotary speaker (two rotors, the partials split at ~800 Hz, their amplitude and Doppler
 * folded into the per-block gains and increments: no per-sample cost). Not taken: SUB / BODY / TOP (0).
 * GR!MM's: the four knobs; the rotary fixed at SLOW (Felucca's default; no knob of its own); the track's state made
 * once a block by the first of its voices (vblock); the engine interface.
 *   DRAWBARS  the registration: FLUTE MELLO HOLLW SMOOT 3BAR BLUES GOSPL ROCK TOPS CLARI REED STRNG CHAPL BRITE BASS FULL
 *   PERC      OFF, 2ND, 3RD, 2SOFT, 3SOFT, 2SLOW, 3SLOW
 *   CLICK     the key click's level
 *   DRIVE     the overdrive: 1x .. 4x
 * State: drw_t[track] (once a block), drw_v[voice]; voice_t is unused. */

#define DRW_NP 9                                /* partials */
#define DRW_FOLD 584366000u                     /* ~6 kHz as a phase increment: partials above fold down an octave */
#define DRW_XOVER 77915000u                     /* ~800 Hz: below the low rotor, above the high rotor */
#define DRW_ROT 1u                              /* the rotary: SLOW */

/* drawbar level 0..8 -> Q15, 3 dB a step */
static const uint16_t DRW_LV[9] = {0, 2920, 4125, 5827, 8231, 11627, 16423, 23198, 32767};

/* the 16 registrations, one digit per bar: 16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1' */
static const char *const N_DRW_REG[16] = {"FLUTE", "MELLO", "HOLLW", "SMOOT", "3BAR", "BLUES", "GOSPL", "ROCK",
                                          "TOPS", "CLARI", "REED", "STRNG", "CHAPL", "BRITE", "BASS", "FULL"};
static const char DRW_REG[16][DRW_NP + 1] = {
    "008000000", /* FLUTE  one soft flute */
    "008400000", /* MELLO  flute and octave */
    "800800000", /* HOLLW  sub and octave, hollow */
    "838000000", /* SMOOT  smooth comping */
    "888000000", /* 3BAR   the first three bars out */
    "888800000", /* BLUES  three bars and the octave */
    "888600006", /* GOSPL  body with a little shimmer */
    "888000888", /* ROCK   body and the top three */
    "800000888", /* TOPS   sub and the top only */
    "008080800", /* CLARI  odd harmonics, reedy-hollow */
    "008765432", /* REED   a falling slope, brassy */
    "006876540", /* STRNG  bright and thin */
    "004432000", /* CHAPL  soft, churchy */
    "008888000", /* BRITE  even bars, bright */
    "858000000", /* BASS   for the left hand */
    "888888888", /* FULL   everything */
};
static const char *const N_DRW_PERC[7] = {"OFF", "2ND", "3RD", "2SOFT", "3SOFT", "2SLOW", "3SLOW"};

/* the note increment above which a partial would pass DRW_FOLD */
static const uint32_t DRW_FOLD_LIM[DRW_NP] = {
    DRW_FOLD * 2u, DRW_FOLD / 3u * 2u, DRW_FOLD, DRW_FOLD / 2u, DRW_FOLD / 3u, DRW_FOLD / 4u, DRW_FOLD / 5u,
    DRW_FOLD / 6u, DRW_FOLD / 8u,
};
static inline uint32_t drw_ratio(uint32_t k, uint32_t x)   /* partial frequency / note frequency, shifts and adds */
{
    switch (k) {
    case 0: return x >> 1;
    case 1: return x + (x >> 1);
    case 2: return x;
    case 3: return x << 1;
    case 4: return x * 3u;
    case 5: return x << 2;
    case 6: return x * 5u;
    case 7: return x * 6u;
    default: return x << 3;
    }
}

/* rotor speeds as phase increments per block (CTL samples): Hz * CTL / FS * 2^32 (Felucca's, as integers) */
static const int32_t DRW_ROT_SPD[2][3] = {
    {0, 2493166, 20880262},                     /* high rotor: OFF (stops), SLOW 0.8 Hz, FAST 6.7 Hz */
    {0, 2181520, 18075451},                     /* low rotor: 0.7 Hz, 5.8 Hz */
};

typedef struct {                                /* per track, once per block (drw_track) */
    uint32_t stamp;                             /* the vblock it was made for */
    int32_t g[DRW_NP];                          /* bar gains, Q15, normalised */
    int32_t pk, plev;                           /* percussion: partial (DRW_NP = off), level Q15 */
    uint32_t pdec;                              /* its decay per block, Q16 */
    int32_t clk;                                /* click level Q15 */
    int32_t dg, dcomp;                          /* drive gain, Q8; output compensation, Q8 (0 = no drive) */
    uint32_t rph[2];                            /* rotor phases (0 high, 1 low) */
    int32_t rsp[2];                             /* rotor speeds (phase increment per block) */
    int32_t rwet;                               /* rotary amount, Q15 (ramps on / off) */
    int32_t am[2];                              /* this block's amplitude, Q15, per rotor */
    int32_t fm[2];                              /* this block's pitch offset, Q16 of the increment, per rotor */
} drw_trk_t;

typedef struct {                                /* per voice */
    uint32_t ph[DRW_NP];
    int32_t gp[DRW_NP];                         /* each partial's gain at the end of the last block (the ramp start) */
    int32_t perc;                               /* percussion envelope, Q15 */
    int32_t clk;                                /* click envelope, Q15 */
    int32_t nz, hp;                             /* click noise state, its high-pass state */
    uint8_t gate, init;
} drw_vc_t;

static drw_trk_t drw_t[NT];
static drw_vc_t drw_v[NV];

/* once per block, before the track's voices: bar gains, percussion, click, drive, the rotors (wheel_block) */
static void drw_track(drw_trk_t *T, const volatile int32_t *p)
{
    uint32_t k, perc = (uint32_t)clamp(p[1], 0, 6), rot = DRW_ROT;
    int32_t s = 0, lv[DRW_NP];
    uint32_t norm;
    for (k = 0; k < DRW_NP; k++) {
        lv[k] = perc && k == DRW_NP - 1u ? 0 : DRW_LV[DRW_REG[(uint32_t)clamp(p[0], 0, 15)][k] - '0'];   /* PERC on: the 1' bar off */
        s += lv[k];
    }
    /* sum of the gains = 2.4 s / (s + 1.5) (s in units of a full bar): one bar 0.96, three 1.6, all nine 2.06 */
    norm = (uint32_t)(2u * 39322u * 32768u) / (uint32_t)(s + 49152);
    if (perc == 1u || perc == 2u || perc == 5u || perc == 6u)
        norm = (norm * 23170u) >> 15;           /* normal percussion: the bars -3 dB */
    for (k = 0; k < DRW_NP; k++)
        T->g[k] = (int32_t)(((uint32_t)lv[k] * norm) >> 15);
    T->pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : DRW_NP;   /* 2ND: 4', 3RD: 2 2/3' */
    T->plev = perc == 3u || perc == 4u ? 12000 : 24000;
    T->pdec = perc >= 5u ? 65457u : 65219u;     /* tau 0.6 s / 0.15 s */
    T->clk = clamp(p[2], 0, 127) * 180;
    if (p[3] > 0) {                             /* y = 6 / (2 + g) tanh(g x / 2) */
        T->dg = 256 + clamp(p[3], 0, 127) * 6;  /* 1 .. 4 */
        T->dcomp = (6 * 256 * 256) / (512 + T->dg);
    } else {
        T->dg = 0;
    }
    /* rotors: each speed eases to its target (high rotor tau ~0.37 s, low ~1.5 s) */
    for (k = 0; k < 2u; k++) {
        int32_t sp, c, sn, dep;
        T->rsp[k] += (DRW_ROT_SPD[k][rot] - T->rsp[k]) >> (k ? 11 : 9);
        sp = T->rsp[k];
        T->rph[k] += (uint32_t)sp;
        sn = sine_i(T->rph[k]);
        c = sine_i(T->rph[k] + 0x40000000u);
        dep = k ? 7000 : 13000;                 /* AM depth: low rotor 21 %, high 40 % */
        T->am[k] = 32767 - mulq15(dep, (32767 - c) >> 1);
        T->fm[k] = (sn * (sp >> (k ? 17 : 16))) >> 15;   /* Doppler: +-0.5 % fast (high), +-0.2 % (low) */
    }
    T->rwet += ((rot ? 32767 : 0) - T->rwet) >> 5;   /* ~25 ms in or out */
    for (k = 0; k < 2u; k++) {
        T->am[k] = 32767 - mulq15(T->rwet, 32767 - T->am[k]);
        T->fm[k] = mulq15(T->fm[k], T->rwet);
    }
}

static void wheel_begin(voice_t *v)             /* a note from silence (wheel_note_on) */
{
    uint32_t vi = (uint32_t)(v - vc), k, held = 0;
    drw_vc_t *V = &drw_v[vi];
    for (k = 0; k < NV; k++)                    /* single trigger: another key of the track held? */
        if (k != vi && vc[k].trk == v->trk && vc[k].eng == ENG_WHEEL && (vc[k].stage == ST_ATK || vc[k].stage == ST_SUS))
            held = 1;
    if (!V->init) {                             /* the wheels: spread start phases, then free running */
        for (k = 0; k < DRW_NP; k++)
            V->ph[k] = (k * 0x9E3779B9u) ^ (vi * 0x7F4A7C15u);
        V->nz = 0x13579BDF + (int32_t)vi;
        V->init = 1;
    }
    for (k = 0; k < DRW_NP; k++)
        V->gp[k] = 0;
    V->perc = held ? 0 : 32767;
    V->clk = 32767;
    V->hp = 0;
    V->gate = 1;
}

/* CTL frames of voice v, added to mono[] (wheel_render) */
static void wheel_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    drw_trk_t *T = &drw_t[v->trk % NT];
    drw_vc_t *V = &drw_v[v - vc];
    int32_t acc[CTL], perc1, clk0, clk1, damp = amp1 - amp0;
    uint32_t i, k, inc0 = vinc(VP16(v));
    if (T->stamp != vblock) {                   /* (the track's first voice this block) */
        T->stamp = vblock;
        drw_track(T, p);
    }
    for (i = 0; i < (uint32_t)CTL; i++)
        acc[i] = 0;
    perc1 = (int32_t)(((uint32_t)V->perc * T->pdec) >> 16);
    for (k = 0; k < DRW_NP; k++) {
        uint32_t x = inc0, inc, ph = V->ph[k];
        int32_t g1 = T->g[k], g0 = V->gp[k], r;
        if ((int32_t)k == T->pk)
            g1 += mulq15(T->plev, perc1);
        while (x > DRW_FOLD_LIM[k])             /* fold back above ~6 kHz */
            x >>= 1;
        inc = drw_ratio(k, x);
        r = inc < DRW_XOVER;                    /* 1: the low rotor */
        g1 = mulq15(g1, T->am[r]);
        inc += (uint32_t)((int32_t)(inc >> 16) * T->fm[r]);
        V->gp[k] = g1;
        if (!g0 && !g1) {
            V->ph[k] = ph + inc * (uint32_t)CTL;
            continue;
        }
        if (g0 == g1) {
            for (i = 0; i < (uint32_t)CTL; i++) {
                ph += inc;
                acc[i] += (SINE[ph >> 22] * g1) >> 15;
            }
        } else {                                /* gain ramp over the block (CTL = 32) */
            int32_t gq = g0 << 5, dg = g1 - g0;
            for (i = 0; i < (uint32_t)CTL; i++) {
                ph += inc;
                gq += dg;
                acc[i] += (SINE[ph >> 22] * (gq >> 5)) >> 15;
            }
        }
        V->ph[k] = ph;
    }
    V->perc = perc1;
    /* key click: high-passed noise, at the key contact and (half) at the release */
    if (V->gate && (v->stage == ST_REL || v->stage == ST_FADE)) {
        V->gate = 0;
        V->clk = 16384;
    }
    clk0 = mulq15(V->clk, T->clk);
    if (clk0 > 16) {
        int32_t nst = V->nz, hp = V->hp, dc;
        V->clk = (V->clk * 3) >> 2;             /* tau ~2.5 ms */
        clk1 = mulq15(V->clk, T->clk);
        dc = clk1 - clk0;
        clk0 <<= 5;
        for (i = 0; i < (uint32_t)CTL; i++) {
            int32_t z = (int32_t)(noise32(&nst) >> 17) - 16384, y = z - hp;
            hp += y >> 3;
            clk0 += dc;
            acc[i] += (y * (clk0 >> 5)) >> 15;
        }
        V->nz = nst;
        V->hp = hp;
    }
    if (T->dg) {                                /* overdrive */
        int32_t dg = T->dg, dcomp = T->dcomp;
        for (i = 0; i < (uint32_t)CTL; i++)
            acc[i] = (softclip((acc[i] * dg) >> 9) * dcomp) >> 8;
    }
    for (i = 0; i < (uint32_t)CTL; i++)         /* acc: Q15 with headroom to 4.0 */
        mono[i] += mulq15(mulq15(acc[i] >> 1, amp_at(amp0, damp, (int32_t)i)), VOICE_LV) << 1;
}

static void wheel_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = p[k];
    switch (k) {
    case 0: put_s(s, N_DRW_REG[clamp(v, 0, 15)]); return;
    case 1: put_s(s, N_DRW_PERC[clamp(v, 0, 6)]); return;
    default: put_s(put_u(s, ((uint32_t)clamp(v, 0, 127) * 100u + 63u) / 127u), "%"); return;
    }
}

static const engine_t WHEEL_ENGINE = {"WHEEL", {"DRAWBARS", "PERC", "CLICK", "DRIVE"}, {15, 6, 127, 127},
                                      wheel_begin, wheel_block, wheel_text};
