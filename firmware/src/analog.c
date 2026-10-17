/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* GR!MM's VA engine: Felucca's ANALOG engine (hugelton/Felucca firmware/src/eng_analog.c analog_osc and
 * analog_note_on), taken over without Felucca's framework: the loop of WAVE SAW and SQR (two band-limited
 * oscillators, OSC 2 DTN cents above OSC 1, MIX at 64), the cutoff with key tracking (KTR at Felucca's default, 64),
 * the low-pass and the soft knee after it. NOISE and DRIVE stay out (Felucca's defaults: off). Its DSP: dsp.c.
 * GR!MM's own: WAVE SUPERSAW (seven saws spread by DETUNE, after Adam Szabo's analysis of the JP-8000's).
 * An engine of voices.c: va_begin, va_block (CTL frames of a voice, its track's four parameters p: WAVE, DETUNE,
 * CUTOFF, RESONANCE), va_text. */

#define NSAW 7u                                 /* SUPERSAW's saws */
#define VOICE_LV 8000                           /* a voice's peak (Q15) */
enum { WAVE_SAW, WAVE_SQR, WAVE_SUPER, NWAVE };
static const char *const WAVE_NAME[NWAVE] = {"SAW", "SQUARE", "SUPERSAW"};
_Static_assert(NSAW <= VPH, "VA: SUPERSAW's phases in a voice");

static void va_begin(voice_t *v)                /* a note from silence (Felucca analog_note_on) */
{
    uint32_t k;
    v->s[0] = v->s[1] = 0;                      /* the filter */
    for (k = 0; k < NSAW; k++) {                /* free-running saws: SUPERSAW's at random phases (xorshift32) */
        uint32_t s = (uint32_t)v->rng;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        v->rng = (int32_t)s;
        v->ph[k] = s;
    }
    v->ph[1] = v->ph[0] + 0x40000000u;          /* SAW / SQR: OSC 2 a quarter cycle on, as Felucca */
}

/* CTL frames of voice v, added to mono[] (eng_analog.c analog_osc's loop for SAW / SQR, GR!MM's SUPERSAW) */
static void va_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    int32_t wave = p[0], det = p[1], p16 = VP16(v), i;
    int32_t cut = (p[2] << 8) + ((64 * (p16 - 60 * 16)) >> 4);   /* KTR 64 */
    int32_t ic1 = v->s[0], ic2 = v->s[1], damp = amp1 - amp0;
    uint32_t inc1 = vinc(p16);
    tsvf_t flt;
    tsvf_coef(&flt, cut, p[3]);
    if (wave == WAVE_SUPER) {
        uint32_t inc[NSAW], ph[NSAW], k;
        int32_t spread = det * 258;             /* DETUNE 0..127 -> Q15 of the full spread */
        for (k = 0; k < NSAW; k++) {
            inc[k] = inc1 + (uint32_t)((int32_t)(inc1 >> 15) * ((SSAW_OFF_Q15[k] * spread) >> 15));
            ph[k] = v->ph[k];
        }
        for (i = 0; i < (int32_t)CTL; i++) {
            int32_t mid = 0, side = 0, s, y;
            for (k = 0; k < NSAW; k++) {
                int32_t o = osc_saw(ph[k], inc[k]);
                if (k == NSAW / 2u)
                    mid = o;
                else
                    side += o;
                ph[k] += inc[k];
            }
            s = mulq15(mid, 9830) + mulq15(side, 4915);                 /* the middle saw 0.3, each side one 0.15 */
            y = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) << 1;
            mono[i] += mulq15(mulq15(y, amp_at(amp0, damp, i)), VOICE_LV);
        }
        for (k = 0; k < NSAW; k++)
            v->ph[k] = ph[k];
    } else {
        uint32_t inc2 = det ? cents_inc(p16, det, tune_f) : inc1;          /* DTN in cents */
        uint32_t ph0 = v->ph[0], ph1 = v->ph[1];
        for (i = 0; i < (int32_t)CTL; i++) {
            int32_t a, b, s, y;
            if (wave == WAVE_SQR) {
                a = osc_pulse(ph0, inc1, 0x80000000u);
                b = osc_pulse(ph1, inc2, 0x80000000u);
            } else {
                a = osc_saw(ph0, inc1);
                b = osc_saw(ph1, inc2);
            }
            ph0 += inc1;
            ph1 += inc2;
            s = mulq15(a, 16255) + mulq15(b, 16512);                    /* MIX 64: m1 = 32767 - 64 * 258, m2 */
            y = soft_knee(tsvf_lp(&flt, s >> 1, &ic1, &ic2), 16000) << 1;
            mono[i] += mulq15(mulq15(y, amp_at(amp0, damp, i)), VOICE_LV);
        }
        v->ph[0] = ph0;
        v->ph[1] = ph1;
    }
    v->s[0] = ic1;
    v->s[1] = ic2;
}

static void va_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = p[k];
    char *e;
    switch (k) {
    case 0: put_s(s, WAVE_NAME[clamp(v, 0, NWAVE - 1)]); return;
    case 1: e = put_u(s, (uint32_t)v); if (p[0] != WAVE_SUPER) put_s(e, " ct"); return;   /* (SUPERSAW: a spread) */
    case 2: put_s(put_u(s, CUTOFF_HZ[clamp(v, 0, 127)]), " Hz"); return;
    default: put_u(s, (uint32_t)v); return;
    }
}

static const engine_t VA_ENGINE = {"VA", {"WAVE", "DETUNE", "CUTOFF", "RESONANCE"}, {NWAVE - 1, 127, 127, 127},
                                va_begin, va_block, va_text};
