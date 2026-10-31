/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* The USB audio input's ring (usb.c's, GR!MM rc9: here, no registers, so tests/uac_ring_test.c runs it on a computer):
 * the render's side (uac_ring_start, uac_tap, uac_tap48: the audio ISR) and the packets' (uac_packet: TIMER5). Needs
 * FELUCCA_UAC, FELUCCA_UAC_48K, HALF_FRAMES, RING_PUBLISH and uac_fir.h's path */
#pragma once
#if FELUCCA_UAC
/* USB audio input. The I2S clock is not locked to USB (about 44,117.6 Hz against the host's 44,100):
 * the endpoint is asynchronous, so each 1 ms packet carries what the ring holds, 44 or 45 frames
 * (the 44.1 pattern) and one more or one fewer while the ring's fill drifts out of its band.
 * The render writes HALF_FRAMES frames a half buffer; the fill is kept in UA_LO..UA_HI at the low point of
 * that saw tooth. (GR!MM rc9: read where the render ends, its frames about to go in, uac_ring_low: the
 * true low point. Felucca read it where the render started, the low point for its light 128-frame renders;
 * GR!MM's 256-frame renders of 1.8 ms and more (CPU NOW past ~31 %) took a packet and more meanwhile, and
 * the ring ran dry within them.) A packet (46) must still be in hand at the low point: the ISR's start can
 * be late. */
#define UA_N 512u                /* ring, frames (16-bit L | R << 16); a power of two */
#define UA_PRIME 104u            /* silence ahead of the first rendered frame */
#define UA_LO 80u                /* a render may start late: keep a packet and more in hand */
#define UA_HI 128u
#define UAC_RATE 44100u
#if FELUCCA_UAC_48K
/* 48 kHz: the same times in 48 kHz frames (x 160 / 147); a render of HALF_FRAMES gives 139..140 frames */
#define UA_PRIME48 113u
#define UA_LO48 87u
#define UA_HI48 139u
#define UA_REND48 ((HALF_FRAMES * 160u + 146u) / 147u)
#define UA_MAXF 49u              /* frames in the largest packet: 48 + 1 */
_Static_assert(UA_HI48 + UA_REND48 + UA_MAXF < UA_N, "the ring holds the band, a render and a packet (48 kHz)");
#else
#define UA_MAXF 46u              /* frames in the largest packet */
#endif
#define UAC_MAXP (UA_MAXF * 4u)
_Static_assert(UA_HI + HALF_FRAMES + UA_MAXF < UA_N, "the ring holds the band, a render and a packet");
static uint32_t ua_ring[UA_N];
static volatile uint32_t ua_w, ua_r;
static struct {
    volatile uint8_t alt;        /* streaming interface setting: 1 = the host records */
    volatile uint8_t r48;        /* the host's rate (SET_CUR): 1 = 48 kHz, 0 = 44.1 (bus reset) */
    uint8_t ring48;              /* the rate the ring was primed for (the render's; a change re-primes) */
    volatile uint8_t go;         /* the ring is primed: the consumer reads it (0: silence packets) */
    volatile uint8_t flowing;    /* the host takes our packets (it may set alt 1 well before it reads) */
    uint8_t queued;              /* a packet was queued since the stream (re)started */
    uint8_t feed;                /* this render feeds the ring (sampled when it starts) */
    uint8_t wait;                /* polls the queued packet has waited */
    uint8_t e0_rx;               /* SET_CUR sampling frequency: data stage pending */
    volatile uint32_t fill_min;  /* ring fill where the last render ended (rc9; before: started): its low point */
    uint8_t prime, low;          /* (rc9) prime the ring where this render ends; read its fill there */
    uint32_t acc;                /* 44.1 frames per ms: 0.1 frame steps */
    uint32_t last;               /* the last frame sent, repeated on an underrun */
    uint32_t starts, pkts, frames;               /* frames in packets sent from the ring */
    uint32_t underruns, overruns, missed;        /* glitches: ring empty (repeat), ring full (drop), no packet ready */
    uint32_t stalls;                             /* the host stopped reading for > 20 ms (alt still 1) */
    uint32_t adj_up, adj_down;                   /* packets one frame longer / shorter than the pattern */
    uint32_t fill_lo, fill_hi;                   /* range of fill_min since the stream started */
    uint32_t rate_sets;                          /* SET_CUR sampling frequency requests */
} uac;
#if FELUCCA_UAC_48K
#include "uac_fir.h"
/* the resampler's input history: the last 16 frames, written twice so a window of UAC_FIR_N never wraps */
#define RS_H 16u
_Static_assert(RS_H >= UAC_FIR_N && (RS_H & (RS_H - 1u)) == 0, "the history holds a window");
static struct {
    int16_t l[2u * RS_H], r[2u * RS_H];   /* r right after l (the FIR loop reads both from one pointer) */
    uint32_t w;                  /* input frames written */
    uint32_t ph;                 /* the next output's phase, 0..159 */
} rs;
#endif
#endif

#if FELUCCA_UAC
/* (GR!MM rc9) the render's side. The fill is read where the render ENDS, its first block going in (uac_tap): the
 * ring's true low point. Read where it started (as before), a render of R ms took 48 x R frames more meanwhile, unseen:
 * past ~1.8 ms (CPU NOW ~31 %) the ring ran dry in it and the packets repeated a frame (a dropout in the recording; the
 * speaker fine). The band (UA_LO .. UA_HI) now holds the low point itself: any render length up to the block's. The
 * priming too, at the render's end (its silence then ahead of this render's frames) */
static void uac_ring_start(uint32_t feed)      /* audio ISR, before a render: does it feed the ring (the host reads) */
{
    uac.feed = (uint8_t)feed;
    if (!feed) {
        uac.go = 0;                                     /* not read (yet / any more): prime when it is */
        return;
    }
    if (!uac.go || uac.ring48 != uac.r48) uac.prime = 1;   /* ((re)start: primed where the render ends) */
    uac.low = 1;                                        /* (the fill: read as its first frames come) */
}
static void uac_ring_low(void)                 /* audio ISR, the render's end (its first block, before it goes in) */
{
    uint32_t fill;
    if (uac.prime) {                                    /* silence ahead, then this render */
        uint32_t r = ua_r, i, prime = UA_PRIME;
        uac.go = 0;
        uac.prime = 0;
#if FELUCCA_UAC_48K
        uac.ring48 = uac.r48;
        if (uac.ring48) {                               /* the resampler starts from silence */
            prime = UA_PRIME48;
            for (i = 0; i < 2u * RS_H; i++)
                rs.l[i] = rs.r[i] = 0;
            rs.w = rs.ph = 0;
        }
#endif
        for (i = 0; i < prime; i++)
            ua_ring[(r + i) & (UA_N - 1u)] = 0;
        RING_PUBLISH();
        ua_w = r + prime;
        uac.fill_min = prime;
        RING_PUBLISH();
        uac.go = 1;
        uac.low = 0;
        return;
    }
    if (!uac.low) return;
    uac.low = 0;
    fill = ua_w - ua_r;
    uac.fill_min = fill;
    if (fill < uac.fill_lo)
        uac.fill_lo = fill;
    if (fill > uac.fill_hi)
        uac.fill_hi = fill;
}

static inline int32_t uac_s16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

#if FELUCCA_UAC_48K
/* 44.1 -> 48 kHz (x 160 / 147), one block: each input frame enters the history, then the 1 or 2 output
 * frames whose newest input it is: phase ph, the window of UAC_FIR_N frames ending at it, the taps of phase
 * ph (or of 159 - ph backwards: the prototype is symmetric), Q15 into 32-bit sums (the taps' absolute sum
 * stays under 2.0: no overflow) */
static __attribute__((noinline)) void uac_tap48(const int32_t *out, uint32_t n)
{
    uint32_t w = ua_w, i, j, k = 0, h = rs.w, ph = rs.ph;
    if (UA_N - (w - ua_r) < n + n / 8u + 2u) {         /* nobody reads: drop the block (<= n * 160 / 147 + 1) */
        uac.overruns++;
        return;
    }
    for (i = 0; i < n; i++) {
        uint32_t s = h & (RS_H - 1u);
        int16_t l = (int16_t)uac_s16(out[2u * i]), r = (int16_t)uac_s16(out[2u * i + 1u]);
        rs.l[s] = rs.l[s + RS_H] = l;
        rs.r[s] = rs.r[s + RS_H] = r;
        h++;
        s = (h - UAC_FIR_N) & (RS_H - 1u);              /* the window: oldest .. this frame */
        do {
            const int16_t *x = &rs.l[s];                /* R: x[2 * RS_H] */
            int32_t al = 1 << (UAC_FIR_Q - 1), ar = 1 << (UAC_FIR_Q - 1);
            if (ph < UAC_FIR_L / 2u) {
                const int16_t *c = UAC_FIR[ph];
                for (j = UAC_FIR_N; j; j--, x++) {
                    int32_t t = *c++;
                    al += t * x[0];
                    ar += t * x[2u * RS_H];
                }
            } else {
                const int16_t *c = &UAC_FIR[UAC_FIR_L - 1u - ph][UAC_FIR_N - 1u];
                for (j = UAC_FIR_N; j; j--, x++) {
                    int32_t t = *c--;
                    al += t * x[0];
                    ar += t * x[2u * RS_H];
                }
            }
            ua_ring[(w + k++) & (UA_N - 1u)] = (uint32_t)(uint16_t)uac_s16(al >> UAC_FIR_Q) |
                                               (uint32_t)(uint16_t)uac_s16(ar >> UAC_FIR_Q) << 16;
            ph += UAC_FIR_M;
        } while (ph < UAC_FIR_L);
        ph -= UAC_FIR_L;
    }
    rs.w = h;
    rs.ph = ph;
    RING_PUBLISH();
    ua_w = w + k;
}
#endif

static __attribute__((noinline)) void uac_tap(const int32_t *out, uint32_t n)   /* audio ISR: one block (Q15) */
{
    uint32_t w, i;
    if (!uac.feed)
        return;
    uac_ring_low();                                     /* (rc9: the render's end: the fill's low point; priming) */
    w = ua_w;
#if FELUCCA_UAC_48K
    if (uac.ring48) {
        uac_tap48(out, n);
        return;
    }
#endif
    if (UA_N - (w - ua_r) < n) {                        /* nobody reads: drop the block */
        uac.overruns++;
        return;
    }
#if FELUCCA_UAC_TONE
    {   /* bench: triangles of period 100 frames instead of the music (L, R in antiphase): every
         * recorded frame must follow the one before, so a lost or repeated frame shows exactly */
        static uint32_t ph;
        for (i = 0; i < n; i++, ph = ph == 99u ? 0u : ph + 1u) {
            uint32_t q = ph < 50u ? ph : 100u - ph, q2 = ph < 50u ? 50u - ph : ph - 50u;
            ua_ring[(w + i) & (UA_N - 1u)] = (uint32_t)(uint16_t)(int16_t)((int32_t)q * 1000 - 25000) |
                                             (uint32_t)(uint16_t)(int16_t)((int32_t)q2 * 1000 - 25000) << 16;
        }
        (void)out;
    }
#else
    for (i = 0; i < n; i++)
        ua_ring[(w + i) & (UA_N - 1u)] = (uint32_t)(uint16_t)uac_s16(out[2u * i]) |
                                         (uint32_t)(uint16_t)uac_s16(out[2u * i + 1u]) << 16;
#endif
    RING_PUBLISH();
    ua_w = w + n;
}

/* the next packet into d: 44 / 45 frames (+-1 to hold the ring's fill), from the ring or, until it is
 * primed, silence. An empty ring repeats the last frame. Returns the frame count. */
static uint32_t uac_packet(uint32_t *d)
{
    uint32_t n = 44u, r, fill, k, i, lo = UA_LO, hi = UA_HI;
#if FELUCCA_UAC_48K
    if (uac.r48) {                                      /* 48 frames a ms, no pattern */
        n = 48u;
        lo = UA_LO48;
        hi = UA_HI48;
    } else
#endif
    {
        uac.acc += UAC_RATE % 1000u;
        if (uac.acc >= 1000u) {
            uac.acc -= 1000u;
            n++;
        }
    }
    if (!uac.go || uac.ring48 != uac.r48) {             /* not primed (at this rate) yet */
        for (i = 0; i < n; i++)
            d[i] = 0;
        return n;
    }
    if (uac.fill_min > hi) {
        n++;
        uac.adj_up++;
    } else if (uac.fill_min < lo) {
        n--;
        uac.adj_down++;
    }
    r = ua_r;
    fill = ua_w - r;
    RING_PUBLISH();                                     /* the slots after the index */
    k = fill < n ? fill : n;
    for (i = 0; i < k; i++)
        d[i] = ua_ring[(r + i) & (UA_N - 1u)];
    if (k < n) {
        uint32_t last = k ? d[k - 1u] : uac.last;
        uac.underruns++;
        for (; i < n; i++)
            d[i] = last;
    }
    uac.last = d[n - 1u];
    RING_PUBLISH();
    ua_r = r + k;
    uac.pkts++;
    uac.frames += n;
    return n;
}
#endif
