/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM in the browser, for the editor page (web/editor.html): what it shows, from the firmware's own tables
 * (web_meta: the sources and their knobs, the categories, the chords, the LFO's names, the FM patches as built and the
 * algorithms' wiring, the factory sounds; as JSON), a value in words (web_knob_text, web_env_text, web_lfo_text),
 * and web_panic, which lets every note go (the sounds reach the tracks as on the FM-1: editor.c's TRACK).
 * Included at the end of firmware/src/grimm.c in the web build only (GRIMM_WEB); the FM-1 never has it. */

static char wm[32768];
static uint32_t wm_n;
static void wm_s(const char *s)
{
    while (*s && wm_n < sizeof wm - 1u) wm[wm_n++] = *s++;
    wm[wm_n] = 0;
}
static void wm_q(const char *s) { wm_s("\""), wm_s(s ? s : ""), wm_s("\""); }   /* (the names: no " or \ in them) */
static void wm_u(uint32_t v) { char b[12]; put_u(b, v), wm_s(b); }
static void wm_bytes(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    wm_s("[");
    for (i = 0; i < n; i++) wm_u(p[i]), wm_s(i + 1u < n ? "," : "]");
}
static void wm_names(const char *const *v, uint32_t n)
{
    uint32_t i;
    wm_s("[");
    for (i = 0; i < n; i++) wm_q(v[i]), wm_s(i + 1u < n ? "," : "]");
}
static void web_fm_builtin(uint8_t *q, uint32_t pt)   /* a built-in PATCH as a track's FM patch (store.c's pk_fm) */
{
    const uint8_t *p = FM_PATCH[pt % FM_NPATCH];
    uint32_t i;
    q[0] = (uint8_t)(0x80u | (p[FP_ALG] & 31u)), q[1] = (uint8_t)(p[FP_FB] & 7u);
    for (i = 0; i < 6u; i++) {
        const uint8_t *op = p + (5u - i) * FP_OP;
        q[2 + i * 5u] = op[FP_FC] & 31u, q[3 + i * 5u] = op[FP_FF] % 100u, q[4 + i * 5u] = op[FP_OL] % 100u;
        q[5 + i * 5u] = (uint8_t)(99u - op[FP_R1] % 100u), q[6 + i * 5u] = (uint8_t)(99u - op[FP_R1 + 1] % 100u);
    }
}
EXPORT char *web_meta(void)
{
    static const char *const DEST[NLFODEST] = {"PITCH", "VOLUME", "PAN", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4",
                                               "REV SEND", "GRAIN SEND", "TRK CUTOFF"};
    uint8_t q[FM_PK];
    uint32_t e, k;
    if (wm_n) return wm;
    wm_s("{\"proto\":"), wm_u(ED_PROTO), wm_s(",\"fmt\":"), wm_u(ED_FMT), wm_s(",\"sndN\":"), wm_u(SND_N);
    wm_s(",\"version\":"), wm_q(APP_VER), wm_s(",\"engines\":[");
    for (e = 0; e < NENG; e++) {
        wm_s("{\"name\":"), wm_q(ENGINES[e]->name), wm_s(",\"knobs\":");
        wm_names(ENGINES[e]->knob, NPARAM);
        wm_s(",\"max\":[");
        for (k = 0; k < NPARAM; k++) wm_u((uint32_t)ENGINES[e]->max[k]), wm_s(k + 1u < NPARAM ? "," : "]");
        wm_s(e + 1u < NENG ? "}," : "}]");
    }
    wm_s(",\"cats\":"), wm_names(CAT_NAME, NCAT);
    wm_s(",\"chords\":"), wm_names(CH_SHORT, NCHORD);
    wm_s(",\"voicings\":"), wm_names(VC_NAME, NVOICING);
    wm_s(",\"lfoDest\":"), wm_names(DEST, NLFODEST);
    wm_s(",\"lfoShape\":"), wm_names(LFO_SHAPE_NAME, NLFOSHAPE);
    wm_s(",\"fmPatches\":[");
    for (k = 0; k < FM_NPATCH; k++) {
        web_fm_builtin(q, k);
        wm_s("{\"name\":"), wm_q(FM_PATCH_NAME[k]), wm_s(",\"fm\":"), wm_bytes(q, FM_PK);
        wm_s(k + 1u < FM_NPATCH ? "}," : "}]");
    }
    wm_s(",\"fmAlg\":[");
    for (k = 0; k < 32u; k++) wm_bytes(FM6_ALG[k], 6), wm_s(k + 1u < 32u ? "," : "]");
    wm_s(",\"factory\":[");
    for (k = 0; k < SND_NFACT; k++) {
        wm_s("{\"name\":"), wm_q(SND_FNAME[k]), wm_s(",\"rec\":"), wm_bytes(ed_bytes(EO_FACT + k), SND_SIZE);
        wm_s(k + 1u < SND_NFACT ? "}," : "}]");
    }
    wm_s("}");
    return wm;
}
static char wt[48];
EXPORT char *web_knob_text(uint32_t e, int32_t a, int32_t b, int32_t c, int32_t d, uint32_t k)   /* source e's knob k */
{
    int32_t p[4];
    p[0] = a, p[1] = b, p[2] = c, p[3] = d;
    wt[0] = 0;
    ENGINES[e % NENG]->text(p, k & 3u, wt);
    return wt;
}
EXPORT char *web_env_text(uint32_t k, int32_t v)   /* ENV's ATTACK, DECAY, SUSTAIN, RELEASE as knob_text has them */
{
    v = clamp(v, 0, 127);
    if (k == 0u) put_ms(wt, pow2_q16(v * 20));
    else if (k == 1u) put_ms(wt, 10u * pow2_q16(v * 15));
    else if (k == 2u) put_pc(wt, v);
    else put_ms(wt, 10u * pow2_q16((v * 33) >> 1));
    return wt;
}
EXPORT char *web_lfo_text(int32_t v) { lfo_rate_text(wt, v, " "); return wt; }   /* the LFO's RATE */
EXPORT void web_panic(void)                     /* every track's notes let go (latched ones too) */
{
    uint32_t t;
    for (t = 0; t < NT; t++) nq_put(NQ_RELEASE, t, 0);
}
