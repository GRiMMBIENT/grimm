/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments (env_tick, amp_at)
 * GR!MM: Copyright (C) 2026 GR!MM */
/* GR!MM's tracks and voices.
 *   NT tracks, each with a source (an engine: VA, FM, SAMPLE, VOICE, WHEEL, PHYS, PHASE), every
 *   engine's four parameters, LEVEL, PAN and the
 *   sends DLY, REV and GRN
 *   (the main loop writes them, 32-bit volatiles: docs/RULES.md);
 *   NV voices shared by all the tracks: a voice plays a note for a track, with the track's engine and parameters
 *   as they are at each block;
 *   the envelope: Felucca's voice.c env_tick (hugelton/Felucca: Q24, a step every CTL frames, a linear attack, an
 *   exponential release; dsp.c amp_at's ramp over each block), with each track's ENV (ATTACK ~1 ms .. ~10 s, DECAY
 *   ~10 ms .. ~10 s, SUSTAIN 0 .. 100 %, RELEASE ~10 ms .. ~20 s; the times exponential in the knob) and a ~10 ms
 *   fade for a voice handed to another note (no click); an engine's own envelope (FM's DECAY, a PHYS strike) runs
 *   inside it;
 *   voices_render: every sounding voice into its track's buffer (mono, 44.1 kHz), for grimm.c's mixer.
 * Another engine: its begin, block and text functions and its names in an engine_t (as analog.c's VA_ENGINE, fm.c's
 * FM_ENGINE), an ENG_ and an entry in ENGINES; a track then plays it by its eng. A voice keeps the engine it began with;
 * a track's engine changing hands its sounding notes over through the fade (env_tick). */

#define CTL 32                                  /* frames a control step: Felucca's CTL, signed as Felucca's (fm6_core.c
                                                 * divides signed gain steps by it: unsigned, a falling gain wraps) */
#define CTL_LOG2 5
#define NV 6u                                   /* voices, shared by the tracks */
#define NT 4u                                   /* tracks */
#define NPARAM 4u                               /* an engine's parameters: KNOB 1..4 on the SOUND page */
#define VPH 8u                                  /* a voice's phases for its engine (SUPERSAW: 7) */
#define FADE_STEP 1198373                       /* Q24 a control step: a stolen voice is silent in 14 steps (~10 ms) */
#define REL_END (1 << 15)                       /* Q24: a release ends here, 54 dB down (a voice's last ~16 LSB, -66 dBFS;
                                                 * Felucca's 1 << 12 rendered the last ~2.7 s of a 6 s release unheard) */
enum { ST_OFF, ST_ATK, ST_SUS, ST_REL, ST_FADE };
enum { ENG_VA, ENG_FM, ENG_SAMPLE, ENG_VOICE, ENG_WHEEL, ENG_PHYS, ENG_PHASE, NENG };

typedef struct {
    volatile uint32_t req;                      /* main loop: + 1 after want_* */
    volatile uint8_t want_note, want_trk, want_on;   /* main loop: start this note for this track / release */
    volatile uint8_t stage, trk, note, eng;     /* audio: ST_*, the track, the note and the engine it plays */
    volatile int32_t env;                       /* audio: Q24 */
    uint32_t seen;                              /* audio: the req it acted on */
    int32_t amp, rng;                           /* the block's end amplitude (Q15); a random state for the engine */
    uint32_t ph[VPH];                           /* the engine's phases */
    int32_t s[4];                               /* .. and its other state (VA: the filter) */
} voice_t;

typedef struct {
    const char *name;                           /* shown in the SOUND page's header and on the SOURCE page */
    const char *knob[NPARAM];                   /* its four parameters' names */
    int32_t max[NPARAM];                        /* their ranges (0 .. max) */
    void (*begin)(voice_t *v);                  /* a note from silence */
    void (*block)(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1);   /* CTL frames, added */
    void (*text)(const volatile int32_t *p, uint32_t k, char *s);   /* parameter k as the screen shows it */
} engine_t;

typedef struct {
    volatile int32_t eng;                       /* ENG_: what its notes play */
    volatile int32_t p[NENG][NPARAM];           /* every engine's four parameters (kept when the engine changes) */
    volatile int32_t level, dly, rev, grn, pan; /* MIXER 1 and 2: 0..127 (PAN 64: the middle) */
    volatile int32_t latch;                     /* SOURCE: LATCH, 0 OFF (a key sounds while held) / 1 ON */
    volatile int32_t env[4];                    /* ENV: ATTACK, DECAY, SUSTAIN, RELEASE, 0..127 */
    volatile int32_t chord, ch_type, ch_voice;  /* SEL: chord mode (0 OFF / 1 ON), CHORD TYPE, VOICING (scale.c) */
    volatile int32_t arp, arp_mode, arp_rate, arp_oct, arp_gate;   /* ARP: on (0 / 1), MODE, RATE, OCTAVES 1..4, GATE
                                                 * 10..100 % (arp.c) */
    volatile int32_t lfo_rate, lfo_depth, lfo_dest, lfo_shape;   /* LFO: RATE 0..127, DEPTH 0..100 %, DESTINATION,
                                                 * SHAPE (lfo.c) */
    volatile int32_t cut, res, fx;              /* TRACK: the track filter's CUTOFF (127: open, bypassed), RESONANCE;
                                                 * FX: its sends 0..200 % (100: as MIXER 1 sets them) (grimm.c) */
    volatile int32_t fm_alg, fm_fb, fmo[6][5];  /* FM: the track's own patch (FM ALGO, FM OP): ALGO 0..31, FEEDBACK
                                                 * 0..7; an operator's (OP1 first) RATIO 0..31, FINE, LEVEL, ATTACK,
                                                 * DECAY 0..99 (fm.c; a built-in PATCH's: fm_builtin) */
} track_t;
#define TRK_DEF {ENG_VA, {{0, 10, 90, 30}, {0, 64, 64, 20}, {0, 0, 127, 0}, {0, 12, 64, 10}, {4, 0, 40, 0}, {0, 64, 80, 64}, {0, 0, 60, 0}}, 100, 0, 100, 0, 64, 0, \
                 {22, 85, 127, 89}, 0, 0, 0, 0, 0, 4, 1, 50, 64, 0, 0, 0, 127, 0, 100, 0, 0, {{0}}}
static track_t trk[NT] = {TRK_DEF, TRK_DEF, TRK_DEF, TRK_DEF};   /* VA: SAW, DETUNE 10, CUTOFF 90, RESONANCE 30
                                                 * (Felucca's ANALOG); FM: BELL as made, DETUNE 20; SAMPLE: slot 1,
                                                 * the whole sample, forward; VOICE: A, SHIFT 0, BUZZ 64, BREATH 10
                                                 * (Felucca's); WHEEL: 3BAR, PERC OFF, CLICK 40, DRIVE 0 (Felucca's);
                                                 * PHYS: MODAL, STRUCTURE 64, DAMP 80, bowed at 50 %; PHASE: SAW,
                                                 * DCW 60, one line (Felucca's); LEVEL 100,
                                                 * DLY 0, REV 100, GRN 0, PAN the middle; LATCH OFF; ENV: ATTACK
                                                 * 5 ms, DECAY 1 s, SUSTAIN 100 %, RELEASE 2 s; chord mode OFF (SCALE, CLOSE); ARP OFF (UP, 1/16,
                                                 * 1 octave, GATE 50 %); LFO DEPTH 0 (RATE ~1.7 s, PITCH, SINE);
                                                 * the filter open, FX 100 %; FM: its patch, BELL's (fm_builtin at
                                                 * power-on) */
static voice_t vc[NV];
static uint32_t vblock;                         /* audio: + 1 every control block (fm.c: once a block a track) */

/* the screen's numbers (the engines' text functions use them too) */
static char *put_u(char *s, uint32_t v)         /* v in decimal at s; returns the end */
{
    char t[10];
    uint32_t n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *s++ = t[--n];
    *s = 0;
    return s;
}
static char *put_s(char *s, const char *a)     /* returns the end */
{
    while (*a) *s++ = *a++;
    *s = 0;
    return s;
}

/* the amplitude at frame i of a block that ramps from amp0 by damp (dsp.c amp_at: / CTL as a shift, rounded towards 0) */
static inline int32_t amp_at(int32_t amp0, int32_t damp, int32_t i)
{
    int32_t x = damp * i;
    return amp0 + ((x + ((x >> 31) & (int32_t)(CTL - 1))) >> CTL_LOG2);
}

/* the LFO's (lfo.c, the audio ISR a control step): a track's pitch offset (1/16 semitones: VP16, a voice's pitch, every
 * engine's), and a voice's parameters, one of them moved (else the track's own) */
static volatile int32_t lfo_p16[NT];
#define VP16(v) ((int32_t)(v)->note * 16 + lfo_p16[(v)->trk % NT])
static const volatile int32_t *lfo_params(uint32_t v, uint32_t t, uint32_t e);
/* SYSTEM's TUNE (main loop): every source's pitch times 1 + tune_f / 4096 (-50 .. +50 cents: -117 .. +120; finer than
 * VP16's 6.25-cent steps); vinc: a pitch's phase increment, tuned */
static volatile int32_t tune_f;
static inline uint32_t vinc(int32_t p16)
{
    uint32_t inc = pitch_inc((uint32_t)clamp(p16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * tune_f);
}

#include "analog.c"                             /* the engines */
#include "fm.c"
#include "sampler.c"
#include "formant.c"
#include "wheel.c"
#include "phys.c"
#include "phase.c"
static const engine_t *const ENGINES[NENG] = {&VA_ENGINE, &FM_ENGINE, &SAMPLE_ENGINE, &FORMANT_ENGINE, &WHEEL_ENGINE,
                                              &PHYS_ENGINE, &PHASE_ENGINE};   /* (by ENG_) */

/* main loop: voice v plays note for track t (a sounding voice fades out first; the same note of the same track
 * sounding goes on up from where it is) */
static void voice_start(uint32_t v, uint32_t t, uint32_t note)
{
    vc[v].want_note = (uint8_t)note;
    vc[v].want_trk = (uint8_t)t;
    vc[v].want_on = 1;
    vc[v].req++;
}
static void voice_release(uint32_t v)
{
    vc[v].want_on = 0;
    vc[v].req++;
}
static void voice_fade(uint32_t v)              /* audio: out in ~10 ms (the fade), no note after it */
{
    vc[v].want_on = 0;
    if (vc[v].stage != ST_OFF) vc[v].stage = ST_FADE;
}
static int voice_idle(uint32_t v) { return vc[v].stage == ST_OFF && vc[v].req == vc[v].seen; }
static int32_t voice_level(uint32_t v) { return vc[v].env; }

static void voice_begin(voice_t *v)             /* a note from silence */
{
    v->note = v->want_note;
    v->trk = v->want_trk < NT ? v->want_trk : 0u;
    if (!v->rng)
        v->rng = 1234567 + (int32_t)(v - vc);
    v->eng = (uint8_t)(trk[v->trk].eng >= 0 && trk[v->trk].eng < NENG ? trk[v->trk].eng : 0);
    ENGINES[v->eng]->begin(v);
    v->env = 0;
    v->stage = ST_ATK;
}

/* the tracks' ENV as steps, from the knobs, once a render: ATTACK a linear step (Q24 a control step), DECAY and RELEASE
 * the share of the way left a control step (Q16: 60 dB in the time), SUSTAIN (Q24). The times: ATTACK 1 ms x
 * 2^(v 20 / 192) (~9.6 s at 127), DECAY 10 ms x 2^(v 15 / 192) (~9.7 s), RELEASE 10 ms x 2^(v 16.5 / 192) (~19 s) */
typedef struct { int32_t atk, sus; uint32_t dec, rel; } env_c_t;
static env_c_t env_c[NT];
static uint32_t env_k(uint32_t t_ms_q16)        /* 60 dB in t: 6.9 / (t / 0.726 ms) of the way a control step, Q16 */
{
    uint32_t k = (uint32_t)((((uint64_t)328516u) << 16) / t_ms_q16);   /* (64 / 32: the chip's divide) */
    return k > 52429u ? 52429u : k ? k : 1u;
}
static void env_coef(env_c_t *c, const volatile int32_t *e)
{
    uint32_t ta = pow2_q16(clamp(e[0], 0, 127) * 20), td = 10u * pow2_q16(clamp(e[1], 0, 127) * 15);
    uint32_t tr = 10u * pow2_q16((clamp(e[3], 0, 127) * 33) >> 1);   /* (ms, Q16) */
    uint32_t a = (uint32_t)((((uint64_t)12173913u) << 16) / ta);       /* 2^24 / (t / 0.726 ms) */
    c->atk = (int32_t)(a > (1u << 24) ? 1u << 24 : a ? a : 1u);
    c->dec = env_k(td);
    c->rel = env_k(tr);
    c->sus = clamp(e[2], 0, 127) * 132104;     /* (2^24 / 127) */
}

/* one control step of the envelope (voice.c env_tick: ATK linear, REL exponential; GR!MM: the track's ENV, DECAY towards
 * SUSTAIN (it follows SUSTAIN when it moves), the fade) */
static int32_t env_tick(voice_t *v)
{
    const env_c_t *c = &env_c[v->trk % NT];
    int32_t e = v->env;
    if (v->req != v->seen) {                    /* the main loop's request */
        v->seen = v->req;
        if (!v->want_on) {
            if (v->stage == ST_ATK || v->stage == ST_SUS)
                v->stage = ST_REL;
        } else if (v->stage == ST_OFF) {
            voice_begin(v);
            e = 0;
        } else if (v->want_note == v->note && v->want_trk == v->trk && v->eng == trk[v->trk].eng && v->stage != ST_FADE) {
            v->stage = ST_ATK;                  /* the same note again (same engine): on up from where it is */
        } else {
            v->stage = ST_FADE;                 /* another note: out first, then begin */
        }
    }
    switch (v->stage) {
    case ST_ATK:
        e += c->atk;
        if (e >= (1 << 24)) {
            e = 1 << 24;
            v->stage = ST_SUS;
        }
        break;
    case ST_SUS: {                              /* DECAY to SUSTAIN, and SUSTAIN */
        int32_t d = mulq16(e - c->sus, c->dec);
        e -= d ? d : e > c->sus ? 1 : e < c->sus ? -1 : 0;
        break;
    }
    case ST_REL:
        e -= mulq16(e, c->rel);
        if (e < REL_END) {
            e = 0;
            v->stage = ST_OFF;
        }
        break;
    case ST_FADE:
        e -= FADE_STEP;
        if (e <= 0) {
            e = 0;
            v->stage = ST_OFF;
            if (v->want_on) {
                v->env = 0;
                voice_begin(v);
            }
        }
        break;
    default:
        e = 0;
        break;
    }
    v->env = e;
    return e >> 9;                              /* Q15 */
}

/* the audio ISR: n frames (a multiple of CTL, at most VB) of every sounding voice into tb[its track] (mono); returns
 * the tracks that sound (bit t), whose buffers it zeroed first; the others' are left as they were */
#define VB 256u
static void ctl_hook(void);                     /* grimm.c: a control block's notes (the main loop's asks, the
                                                 * sequencer), before the voices */
static uint32_t voices_render(int32_t (*tb)[VB], uint32_t n)
{
    uint32_t b, i, k, used = 0, envd = 0;
    if (n > VB)
        return 0;
    for (b = 0; b < n; b += CTL, vblock++) {
        ctl_hook();
        for (k = 0; k < NV; k++)                /* nothing sounds and nothing is asked for: nothing to do */
            if (vc[k].stage != ST_OFF || vc[k].amp || vc[k].req != vc[k].seen) break;
        if (k == NV)
            continue;
        if (!envd) {                            /* the tracks' ENV, as the knobs are now (once a render) */
            for (k = 0; k < NT; k++)
                env_coef(&env_c[k], trk[k].env);
            envd = 1;
        }
        for (k = 0; k < NV; k++) {
            voice_t *v = &vc[k];
            int32_t amp0 = v->amp, amp1 = env_tick(v);
            uint32_t t = v->trk;
            v->amp = amp1;
            if (!amp0 && !amp1)
                continue;                       /* silent: costs nothing */
            if (!(used >> t & 1u)) {            /* the track's first sound this block */
                used |= 1u << t;
                for (i = 0; i < n; i++)
                    tb[t][i] = 0;
            }
            ENGINES[v->eng]->block(v, lfo_params(k, t, v->eng), tb[t] + b, amp0, amp1);   /* (the engine it began
                                                 * with; the LFO's on one of its knobs) */
        }
    }
    return used;
}
