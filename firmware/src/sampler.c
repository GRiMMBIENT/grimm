/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's SAMPLE source: a sample slot (samples.c) played as a loop, as a tape loop plays. GR!MM's own:
 *   varispeed: the key sets the speed, as on tape (the slot's root key plays it as recorded, an octave down at half the
 *   speed and an octave lower); no time-stretching;
 *   the loop: from START, LENGTH long (50 ms .. to the sample's end), forward or (REVERSE) backward. Two heads: before
 *   the loop's end the second one starts at its beginning and they cross over (equal power, up to 100 ms): no click.
 *   SLOT, START, LENGTH and REVERSE take effect there, at the next pass: a turned knob never clicks either;
 *   reading: the ADPCM a 256-sample segment at a time into a window, from samples.c's seek index, so a head reads
 *   either way at about one decode per source sample (a speed above 4, two octaves above the root, is held at 4). A
 *   voice keeps its last SP_NW windows: a loop of up to ~3 segments, once round, plays from them with no decoding.
 *   (Step 7's TAPE engine; its wear is now the TAPE effect over the whole mix, tapefx.c.)
 *   SLOT      the sample slot, 1..4 (EMPTY: it plays nothing)
 *   START     where the loop begins: 0 .. 100 % of the sample
 *   LENGTH    the loop: 50 ms .. the rest of the sample (squared: fine at the short end)
 *   REVERSE   OFF / ON: the loop backward
 * State: sp[voice]: the loop playing and the next one, the heads, the windows' keys; sp_w: the windows (in the pool). */

#define SP_MIN_LEN 1102u                        /* the shortest loop: 50 ms at 22050 Hz */
#define SP_XF_MAX 2205u                         /* the crossing: at most 100 ms (a quarter of a short loop) */
#define SP_STEP_MAX (2u << 16)                  /* source samples an output sample: two octaves above the root */
#define SP_LV 9000                              /* a voice's level (Q15) */
#define SP_NW 4u                                /* the windows a voice keeps */
enum { SP_PLAY, SP_ARMED, SP_CROSS };           /* a pass: playing; the next loop chosen; the heads crossing */

typedef struct {
    uint32_t l0, len, xf;                       /* the loop's first sample, its length, its crossing (samples) */
    uint8_t rev, slot, ok;
} sp_loop_t;
typedef struct {
    sp_loop_t cur, nxt;                         /* the loop playing; the next one (the knobs, as the crossing nears) */
    const uint8_t *wd[SP_NW];                   /* the windows: which slot's data, */
    int32_t ws[SP_NW];                          /* .. which segment (-1: none) */
    int32_t r[2];                               /* heads A and B: their place in their window, Q16 */
    uint8_t hw[2], a, st, wnext;                /* .. their windows; which head is A; SP_; the next window to reuse */
    uint32_t ui, uf;                            /* head A in its loop: whole samples, Q16 fraction */
    uint32_t ev, xs, xinv, kq;                  /* the next event (at ui); the crossing: its start, 256 / its length
                                                 * (Q16), how far it is (Q16, 0 .. 256) */
} sp_voice_t;
static sp_voice_t sp[NV];
static int16_t sp_w[NV][SP_NW][SMP_SEG + 1u] __attribute__((section(".pool")));   /* a segment and the next sample */

static uint32_t sp_slot(const volatile int32_t *p) { return (uint32_t)clamp(p[0], 0, SMP_SLOTS - 1); }

/* the loop the knobs ask for (0: the slot is empty) */
static int sp_loop(const volatile int32_t *p, sp_loop_t *L)
{
    uint32_t slot = sp_slot(p), n, l0, av, k;
    const smp_slot_t *s = &smp[slot];
    L->ok = 0;
    if (!s->ready || s->n < SP_MIN_LEN + 2u)
        return 0;
    n = s->n;
    l0 = n * (uint32_t)clamp(p[1], 0, 127) / 128u;
    av = n - l0;
    if (av < SP_MIN_LEN) {
        l0 = n - SP_MIN_LEN;
        av = SP_MIN_LEN;
    }
    k = (uint32_t)clamp(p[2], 0, 127);
    L->len = SP_MIN_LEN + (av - SP_MIN_LEN) * (k * k) / 16129u;
    L->l0 = l0;
    L->xf = L->len / 4u < SP_XF_MAX ? L->len / 4u : SP_XF_MAX;
    L->rev = (uint8_t)(p[3] != 0);
    L->slot = (uint8_t)slot;
    L->ok = 1;
    return 1;
}

/* the window holding segment seg of the slot: one kept, or the oldest decoded anew (never `keep`: the other head's) */
static uint32_t sp_win(sp_voice_t *t, uint32_t vi, uint32_t slot, int32_t seg, uint32_t keep)
{
    const smp_slot_t *s = &smp[slot];
    const uint8_t *d = s->data;
    uint32_t k, q, m, b;
    int32_t pred, idx;
    int16_t *w;
    for (k = 0; k < SP_NW; k++)
        if (t->ws[k] == seg && t->wd[k] == d)
            return k;
    k = t->wnext == keep ? (keep + 1u) % SP_NW : t->wnext;
    t->wnext = (uint8_t)((k + 1u) % SP_NW);
    w = sp_w[vi][k];
    pred = smp_ipred[slot][seg];                /* from the index: a segment starts on a whole byte */
    idx = smp_iidx[slot][seg];
    q = (uint32_t)seg << SMP_SEG_LOG2;
    m = s->n - q < SMP_SEG + 1u ? s->n - q : SMP_SEG + 1u;
    for (b = 0; b + 1u < m; b += 2u) {          /* two samples a byte */
        uint32_t c = d[(q + b) >> 1];
        w[b] = (int16_t)ima_nib(c & 15u, &pred, &idx);
        w[b + 1u] = (int16_t)ima_nib(c >> 4, &pred, &idx);
    }
    if (b < m) {
        w[b] = (int16_t)ima_nib(d[(q + b) >> 1] & 15u, &pred, &idx);
        b++;
    }
    for (; b <= SMP_SEG; b++)                   /* (past the sample's end: its last sample) */
        w[b] = w[b - 1u];
    t->ws[k] = seg;
    t->wd[k] = d;
    return k;
}
/* head h to (u + f / 65536) from the beginning (forward) or the end (REVERSE) of loop L */
static void sp_seat(sp_voice_t *t, uint32_t vi, uint32_t h, const sp_loop_t *L, uint32_t u, uint32_t f)
{
    int32_t i = (int32_t)(L->rev ? L->l0 + L->len - 1u - u : L->l0 + u), seg = i >> SMP_SEG_LOG2;
    int32_t r = ((i & (int32_t)(SMP_SEG - 1u)) << 16) + (L->rev ? -(int32_t)f : (int32_t)f);
    if (r < 0) {                                /* (backward, between a segment's first sample and the one before) */
        if (seg > 0) {
            seg--;
            r += (int32_t)SMP_SEG << 16;
        } else
            r = 0;
    }
    t->hw[h] = (uint8_t)sp_win(t, vi, L->slot, seg, t->hw[h ^ 1u]);
    t->r[h] = r;
}
/* head h past its window's end (or before its beginning): the next segment's window */
static void sp_move(sp_voice_t *t, uint32_t vi, uint32_t h, uint32_t slot)
{
    int32_t r = t->r[h], d = (r >> 16) >> SMP_SEG_LOG2, seg = t->ws[t->hw[h]] + d,
            last = (int32_t)((smp[slot].n - 1u) >> SMP_SEG_LOG2);
    r -= d * ((int32_t)SMP_SEG << 16);
    if (seg < 0) {
        seg = 0;
        r = 0;
    } else if (seg > last) {
        seg = last;
        r = (int32_t)(SMP_SEG - 1u) << 16;
    }
    t->hw[h] = (uint8_t)sp_win(t, vi, slot, seg, t->hw[h ^ 1u]);
    t->r[h] = r;
}
/* head A at the next event (ui >= ev): the next loop chosen; head B in, the crossing begun; the pass over */
static void sp_event(sp_voice_t *t, uint32_t vi, const volatile int32_t *p)
{
    while (t->ui >= t->ev) {
        if (t->st == SP_PLAY) {                 /* the next pass, from the knobs as they are now */
            uint32_t x;
            if (!sp_loop(p, &t->nxt))
                t->nxt = t->cur;
            x = t->cur.xf < t->nxt.len / 2u ? t->cur.xf : t->nxt.len / 2u;
            x = x ? x : 1u;
            t->xs = t->cur.len - x;
            t->xinv = (256u << 16) / x;
            t->st = SP_ARMED;
            t->ev = t->xs;
        } else if (t->st == SP_ARMED) {         /* head B in at the next loop's beginning */
            uint32_t u = t->ui - t->xs;
            sp_seat(t, vi, t->a ^ 1u, &t->nxt, u, t->uf);
            t->kq = u * t->xinv + (((t->uf >> 4) * (t->xinv >> 4)) >> 8);
            t->st = SP_CROSS;
            t->ev = t->cur.len;
        } else {                                /* A out: B goes on as A, in the next loop */
            t->ui -= t->xs;
            t->a ^= 1u;
            t->cur = t->nxt;
            t->st = SP_PLAY;
            t->ev = t->cur.len - t->cur.xf;
        }
    }
}

static void sampler_begin(voice_t *v)           /* a note from silence: the loop from the knobs at the first block */
{
    sp_voice_t *t = &sp[v - vc];
    uint32_t k;
    t->cur.ok = t->nxt.ok = 0;
    for (k = 0; k < SP_NW; k++)                 /* (a slot's data may have changed since: an upload, in silence) */
        t->ws[k] = -1;
    t->a = 0;
    t->st = SP_PLAY;
}

/* CTL frames of voice v, added to mono[] */
static void sampler_block(voice_t *v, const volatile int32_t *p, int32_t *mono, int32_t amp0, int32_t amp1)
{
    uint32_t vi = (uint32_t)(v - vc), i, step, ui, uf, ev, kq, kinc, a;
    sp_voice_t *t = &sp[vi];
    int32_t buf[CTL], d16, a0, da, ra, rb, dsa, dsb;
    const int16_t *wa, *wb;
    if (!t->cur.ok) {                           /* (the first block, or the slot was empty: is it there now?) */
        if (!sp_loop(p, &t->cur))
            return;                             /* an empty slot: nothing at all */
        t->ui = t->uf = 0;
        t->st = SP_PLAY;
        t->ev = t->cur.len - t->cur.xf;
        sp_seat(t, vi, t->a, &t->cur, 0, 0);
    }
    if (!smp[t->cur.slot].ready) {
        t->cur.ok = 0;
        return;
    }
    /* the speed: the key against the root (varispeed) */
    d16 = VP16(v) - smp[t->cur.slot].root16;
    step = (pow2_q16(clamp(d16, -2304, 384)) >> 8) * (SMP_RATE_Q16 >> 8);
    step += (uint32_t)((int32_t)(step >> 12) * tune_f);   /* (TUNE) */
    step = step > SP_STEP_MAX ? SP_STEP_MAX : step < 256u ? 256u : step;

    /* the heads: A, and B as they cross; their places move by the step, backward in a REVERSE loop */
#define SP_LOAD()                                                                                                      \
    do {                                                                                                               \
        a = t->a, ui = t->ui, uf = t->uf, ev = t->ev, kq = t->kq;                                                      \
        kinc = ((step >> 4) * (t->xinv >> 4)) >> 8;                                                                    \
        ra = t->r[a], rb = t->r[a ^ 1u];                                                                               \
        wa = sp_w[vi][t->hw[a]], wb = sp_w[vi][t->hw[a ^ 1u]];                                                         \
        dsa = t->cur.rev ? -(int32_t)step : (int32_t)step, dsb = t->nxt.rev ? -(int32_t)step : (int32_t)step;          \
    } while (0)
#define SP_STORE() (t->ui = ui, t->uf = uf, t->kq = kq, t->r[a] = ra, t->r[a ^ 1u] = rb)
    /* in runs: as far as no event and no window's edge can come (a step is at most 2 source samples: a run of m moves
     * a head at most 2 m, so m <= distance x 31 / 64 is safe), with no tests inside */
#define SP_RUN(m, r, ds)                                                                                               \
    do {                                                                                                               \
        uint32_t room_ = (ds) < 0 ? (uint32_t)((r) >> 16) : SMP_SEG - 1u - (uint32_t)((r) >> 16);                      \
        room_ = ((room_ * 31u) >> 6) + 1u;                                                                             \
        m = m < room_ ? m : room_;                                                                                     \
    } while (0)
    SP_LOAD();
    for (i = 0; i < (uint32_t)CTL;) {
        uint32_t m = (uint32_t)CTL - i, j, e;
        int32_t n;
        if (ui >= ev) {
            SP_STORE();
            sp_event(t, vi, p);
            SP_LOAD();
        }
        if ((uint32_t)(ra >> 16) >= SMP_SEG) {  /* (into the next segment) */
            SP_STORE();
            sp_move(t, vi, a, t->cur.slot);
            SP_LOAD();
        }
        e = ((ev - ui) * 31u) >> 6;             /* (to the event: at least one sample, as ui < ev) */
        m = m < e + 1u ? m : e + 1u;
        SP_RUN(m, ra, dsa);
        if (t->st != SP_CROSS) {
            for (j = 0; j < m; j++, ra += dsa) {
                n = ra >> 16;
                buf[i + j] = wa[n] + (((wa[n + 1] - wa[n]) * ((ra >> 1) & 0x7FFF)) >> 15);
            }
        } else {                                /* equal power: A out, B in */
            if ((uint32_t)(rb >> 16) >= SMP_SEG) {
                SP_STORE();
                sp_move(t, vi, a ^ 1u, t->nxt.slot);
                SP_LOAD();
            }
            SP_RUN(m, rb, dsb);
            for (j = 0; j < m; j++, ra += dsa, rb += dsb, kq += kinc) {
                uint32_t k = kq >> 16;
                int32_t y, yb;
                n = ra >> 16;
                y = wa[n] + (((wa[n + 1] - wa[n]) * ((ra >> 1) & 0x7FFF)) >> 15);
                n = rb >> 16;
                yb = wb[n] + (((wb[n + 1] - wb[n]) * ((rb >> 1) & 0x7FFF)) >> 15);
                k = k > 256u ? 256u : k;
                buf[i + j] = mulq15(y, SINE[256u - k]) + mulq15(yb, SINE[k]);
            }
        }
        i += m;
        uf += m * step;                         /* (head A in its loop) */
        ui += uf >> 16;
        uf &= 0xFFFFu;
    }
    SP_STORE();
#undef SP_LOAD
#undef SP_STORE
#undef SP_RUN

    a0 = mulq15(amp0, SP_LV);                   /* the level and the envelope as one gain */
    da = mulq15(amp1, SP_LV) - a0;
    for (i = 0; i < (uint32_t)CTL; i++)
        mono[i] += mulq15(buf[i], amp_at(a0, da, (int32_t)i));
}

static void sampler_text(const volatile int32_t *p, uint32_t k, char *s)   /* parameter k as the screen shows it */
{
    int32_t v = p[k];
    uint32_t slot = sp_slot(p), t;
    sp_loop_t L;
    switch (k) {
    case 0:                                     /* the slot and its name, or what is wrong with it */
        s = put_u(s, slot + 1u);
        if (smp[slot].state == SS_SCAN) put_s(s, " CHECKING");
        else if (!smp[slot].ready) put_s(s, " EMPTY");
        else if (smp[slot].name[0]) put_s(put_s(s, " "), smp[slot].name);
        return;
    case 1: put_s(put_u(s, ((uint32_t)v * 100u + 63u) / 127u), "%"); return;
    case 2:                                     /* the loop's length in seconds, as the slot's sample allows */
        if (!sp_loop(p, &L)) { put_s(s, "-"); return; }
        s = put_u(s, L.len / 22050u);
        *s++ = '.';
        t = (L.len % 22050u) * 100u / 22050u;
        *s++ = (char)('0' + t / 10u);
        *s++ = (char)('0' + t % 10u);
        put_s(s, " s");
        return;
    default: put_s(s, v ? "ON" : "OFF"); return;
    }
}

static const engine_t SAMPLE_ENGINE = {"SAMPLE", {"SLOT", "START", "LENGTH", "REVERSE"}, {SMP_SLOTS - 1, 127, 127, 1},
                                       sampler_begin, sampler_block, sampler_text};
