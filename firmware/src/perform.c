/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's FX performance layer: FX held + the white keys G3 .. F4, each trick on while its key is held (several at
 * once), over the whole output, after TAPE (app_audio: perform_render); FREEZE (F3) is GRAIN's (grain.c).
 *   REV THROW, ECHO THROW: every track's REV / DLY send full (grimm.c's mix, its gains ramped); the tails ring on
 *   TAPE STOP: the output slows to a stop over ~1 s (its pitch falling); let go: back up to speed in ~0.3 s, in time
 *   REVERSE: the last beat (at most the history's 0.75 s) backwards, round and round
 *   STUTTER: the last 1/16 at the tempo, round and round
 *   FILTER DOWN, FILTER UP: a low-pass (high-pass) on the output, closing (opening) over ~2 s; let go: back in ~0.5 s
 * The history: the output (mono, at 22.05 kHz) the last 0.75 s, in what was GRAIN's 2 s ring (GRAIN keeps 1.25 s);
 * TAPE STOP, REVERSE and STUTTER play from it (mono, at 22.05 kHz while they sound). Into the trick and out of it:
 * ~10 ms crossfades (between two of them: out of one, into the other); the loops' ends: ~3 ms fades. Nothing saved:
 * all off at power-on. Audio ISR; the main loop sets pf_held (grimm.c: PF_, G3 .. F4) and reads the state (the PERFORM
 * screen). */

#define PF_LEN 16537u                           /* the history: 0.75 s at 22.05 kHz */
#define PF_EDGE 64u                             /* a loop's end fades (history samples, ~3 ms) */
#define PF_XF 75                                /* the crossfade's step a frame (Q15): ~10 ms */
enum { PR_NONE, PR_TAPE, PR_BACK, PR_STUT };    /* the reader: what it plays */
enum { TS_RUN, TS_STOP, TS_DIP, TS_UP };        /* TAPE: its speed falling, at a jump, rising */

static int16_t pf_buf[PF_LEN] __attribute__((section(".pool")));
static uint32_t pf_wc;                          /* audio: history samples written (the next: pf_wc % PF_LEN) */
static uint32_t pf_cur, pf_want;                /* audio: the reader's trick; the one wanted (crossfade between) */
static int32_t pf_m;                            /* audio: the reader's share of the output (Q15) */
static uint32_t pf_rp, pf_rf;                   /* audio: the reader's place (history samples, + 1/65536) */
static uint32_t pf_ls, pf_ll;                   /* audio: a loop's start (REVERSE: its end), its length */
static int32_t pf_v, pf_tg;                     /* audio: TAPE's speed (Q16), its gain (Q15) */
static uint32_t pf_ts, pf_dt;                   /* audio: TAPE's state; at a jump: the speed's rise a sample */
static int32_t pf_half, pf_y;                   /* audio: the history's even frame (a pair averaged); the reader's last */
static int32_t pf_lc = 127 << 8, pf_hc;         /* audio: FILTER DOWN's, UP's cutoff (tsvf's 0..127 << 8) */
static int32_t pf_lw, pf_hw;                    /* audio: their wet shares (Q15) */
static int32_t pf_st[2][4];                     /* audio: the filters' states (L, R: low-pass 2, high-pass 2) */

static int32_t pf_at(uint32_t i, uint32_t f)    /* history sample i (+ f / 65536), between the two */
{
    int32_t a = pf_buf[i % PF_LEN], b = pf_buf[(i + 1u) % PF_LEN];
    return a + (int32_t)(((int64_t)(b - a) * (int32_t)f) >> 16);
}
static uint32_t pf_beat(void) { return 1323000u / (uint32_t)clamp(seq_bpm, 20, 300); }   /* a beat (history samples) */
static void pf_begin(uint32_t r)                /* audio: the reader starts trick r (its loop from the history now) */
{
    pf_cur = r, pf_rf = 0;
    if (r == PR_STUT) {                         /* the last 1/16, forward */
        pf_ll = pf_beat() / 4u;
        pf_ll = pf_ll > PF_LEN - 2u * PF_EDGE ? PF_LEN - 2u * PF_EDGE : pf_ll;
        pf_ls = pf_wc - pf_ll, pf_rp = pf_ls;
    } else if (r == PR_BACK) {                  /* the last beat, backwards */
        pf_ll = pf_beat();
        pf_ll = pf_ll > PF_LEN - 2u * PF_EDGE ? PF_LEN - 2u * PF_EDGE : pf_ll;
        pf_ls = pf_wc - 1u, pf_rp = pf_ls;
    } else if (r == PR_TAPE) {                  /* from now, slowing */
        pf_rp = pf_wc - 2u, pf_v = 65536, pf_tg = 32767, pf_ts = TS_STOP;
    }
}
static int32_t pf_read(void)                    /* audio: the reader's sample, a history sample on (half a frame:
                                                 * called every other frame, the frame between interpolated) */
{
    uint32_t k;
    int32_t y, e;
    switch (pf_cur) {
    case PR_STUT:
        k = pf_rp - pf_ls;
        y = pf_buf[pf_rp % PF_LEN];
        if (++pf_rp - pf_ls >= pf_ll) pf_rp = pf_ls;
        break;
    case PR_BACK:
        k = pf_ls - pf_rp;
        y = pf_buf[pf_rp % PF_LEN];
        if (pf_ls - --pf_rp >= pf_ll) pf_rp = pf_ls;
        break;
    default:                                    /* TAPE: at its speed; its gain down with it (no thump at 0) */
        y = pf_at(pf_rp, pf_rf);
        pf_rf += (uint32_t)pf_v;
        pf_rp += pf_rf >> 16, pf_rf &= 0xFFFFu;
        if (pf_ts == TS_STOP) pf_v = pf_v > 3 ? pf_v - 3 : 0;   /* (1 to 0 over ~1 s: 65536 / 22050) */
        else if (pf_ts == TS_UP && (pf_v += (int32_t)pf_dt) >= 65536) pf_v = 65536, pf_ts = TS_RUN;
        e = pf_v < 16384 ? pf_v * 2 : 32767;
        if (pf_ts == TS_DIP) {                  /* let go: out, the jump (back to where a rise from this speed ends
                                                 * in time), in */
            if ((pf_tg -= 512) <= 0) {
                uint32_t up = (uint32_t)(65536 - pf_v) * 6615u >> 16;   /* (the rise: 0.3 s from 0) */
                pf_tg = 0, pf_ts = TS_UP;
                pf_dt = up ? (uint32_t)(65536 - pf_v) / up + 1u : 65536u;
                pf_rp = pf_wc - 1u - (uint32_t)((uint64_t)(65536 - pf_v) * up >> 17), pf_rf = 0;
            }
        } else if (pf_tg < 32767)
            pf_tg = pf_tg + 512 > 32767 ? 32767 : pf_tg + 512;
        return (y * e >> 15) * pf_tg >> 15;
    }
    e = k < PF_EDGE ? (int32_t)k * 512 : 32767;     /* (a loop's ends faded) */
    k = pf_ll - k;
    if (k < PF_EDGE && (int32_t)k * 512 < e) e = (int32_t)k * 512;
    return y * e >> 15;
}
static int32_t pf_toward(int32_t a, int32_t t, int32_t st)   /* a, st nearer t */
{
    return a < t ? (a + st > t ? t : a + st) : (a - st < t ? t : a - st);
}
static int32_t pf_hp(const tsvf_t *c, int32_t k, int32_t in, int32_t *ic1, int32_t *ic2)   /* tsvf's high-pass */
{
    int32_t v3 = in - *ic2, v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13, v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return in - ((k * v1) >> 12) - v2;
}

static void perform_render(int32_t *out, uint32_t n)   /* audio: the tricks on the output (n stereo frames) */
{
    uint32_t h = pf_held, i, c;
    int32_t y;
    tsvf_t lc, hc;
    if (pf_cur == PR_TAPE && !(h >> PF_TAPE & 1u) && pf_ts == TS_STOP) pf_ts = TS_DIP;   /* (TAPE let go: back up; */
    if (pf_cur == PR_TAPE && (h >> PF_TAPE & 1u) && pf_ts != TS_STOP) pf_ts = TS_STOP;   /* held again: down) */
    pf_want = h >> PF_STUT & 1u ? PR_STUT : h >> PF_BACK & 1u ? PR_BACK :
              (h >> PF_TAPE & 1u) || (pf_cur == PR_TAPE && pf_ts != TS_RUN) ? PR_TAPE : PR_NONE;
    for (i = 0; i < n; i++) {
        int32_t l = out[2u * i], r = out[2u * i + 1u], s = (l + r) >> 1;
        if (i & 1u) {                           /* the history: a pair a sample, 22.05 kHz (a loop playing: kept) */
            if (pf_cur != PR_STUT && pf_cur != PR_BACK) {
                pf_buf[pf_wc % PF_LEN] = (int16_t)clamp((pf_half + s) >> 1, -32767, 32767);
                pf_wc++;
            }
        } else
            pf_half = s;
        if (pf_cur != pf_want) {                /* into another trick: out of this one first */
            if (pf_cur == PR_NONE || (pf_m -= PF_XF) <= 0) {
                pf_m = 0;
                if (pf_want != PR_NONE) pf_begin(pf_want);
                else pf_cur = PR_NONE;
            }
        } else if (pf_cur != PR_NONE && pf_m < 32767)
            pf_m = pf_m + PF_XF > 32767 ? 32767 : pf_m + PF_XF;
        if (pf_cur == PR_NONE && !pf_m) continue;
        if (!(i & 1u)) {                        /* at 22.05 kHz: the even frame half way from the last */
            int32_t yn = pf_cur != PR_NONE ? pf_read() : 0;
            y = (pf_y + yn) >> 1, pf_y = yn;
        } else
            y = pf_y;
        out[2u * i] = l + ((y - l) * (pf_m >> 3) >> 12);
        out[2u * i + 1u] = r + ((y - r) * (pf_m >> 3) >> 12);
    }
    /* FILTER DOWN (low-pass), UP (high-pass): the cutoff moves a block at a time; wet in and out ~5 ms */
    pf_lc = h >> PF_FDOWN & 1u ? (pf_lc > (20 << 8) + 82 ? pf_lc - 82 : 20 << 8) :      /* (~2 s down, */
            (pf_lc < (127 << 8) - 330 ? pf_lc + 330 : 127 << 8);                         /* ~0.5 s back) */
    pf_hc = h >> PF_FUP & 1u ? (pf_hc < (96 << 8) - 63 ? pf_hc + 63 : 96 << 8) : (pf_hc > 250 ? pf_hc - 250 : 0);
    if (pf_lc >= (127 << 8) && !pf_lw && pf_hc <= 0 && !pf_hw) {   /* (both open, dry: nothing to do) */
        for (c = 0; c < 2u; c++) pf_st[c][0] = pf_st[c][1] = pf_st[c][2] = pf_st[c][3] = 0;
        return;
    }
    tsvf_coef_k(&lc, pf_lc, 4096);              /* (a little resonance: a DJ's filter) */
    tsvf_coef_k(&hc, pf_hc, 4096);
    for (i = 0; i < n; i++) {
        int32_t lt = pf_lc < (127 << 8) ? 32767 : 0, ht = pf_hc > 0 ? 32767 : 0;
        pf_lw = pf_toward(pf_lw, lt, 150), pf_hw = pf_toward(pf_hw, ht, 150);   /* (~5 ms) */
        for (c = 0; c < 2u; c++) {
            int32_t x = out[2u * i + c], f;
            x = clamp(x, -140000, 140000);
            f = tsvf_lp(&lc, x, &pf_st[c][0], &pf_st[c][1]);
            x += (f - x) * (pf_lw >> 3) >> 12;
            f = pf_hp(&hc, 4096, x, &pf_st[c][2], &pf_st[c][3]);
            out[2u * i + c] = x + ((f - x) * (pf_hw >> 3) >> 12);
        }
    }
}
static int perform_idle(void)                   /* nothing held, nothing on (the stress test's checks) */
{
    return !pf_held && pf_cur == PR_NONE && !pf_m && pf_lc >= (127 << 8) && pf_hc <= 0 && !pf_lw && !pf_hw;
}
static int32_t perform_semis(void)              /* TAPE's pitch in semitones (the PERFORM screen; -99: stopped) */
{
    int32_t v = pf_cur == PR_TAPE ? pf_v : 65536, s = 0;
    if (v < 1024) return -99;
    while (v < 65536) v = v * 1059 / 1000, s--;   /* (a semitone: x 1.059) */
    return s;
}
