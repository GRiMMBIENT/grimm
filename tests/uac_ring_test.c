/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM rc9: the USB audio ring (firmware/src/uac_ring.h, usb.c's) on a computer, in time: a render every 5.805 ms
 * (the FM-1's clock +-200 ppm off the computer's) that takes R ms (0.5 .. 5.5: CPU NOW 9 .. 95 %), its frames in where
 * it ends (uac_tap, 8 blocks of 32, as platform.c); a packet a ms (uac_packet, as TIMER5's uac_service). 60 s each;
 * after the start: no underrun (a repeated frame: a dropout in the recording), no overrun (a block dropped); at 44.1
 * kHz the frames received a counter, each one on from the last (none lost, none twice). And the rc8 way (the fill read
 * where the render STARTS): dropouts from R ~1.8 ms on, so the test sees the bug it guards.
 *   cc -O2 -Wall -Ifirmware/src -o build/host/uac_ring_test tests/uac_ring_test.c && build/host/uac_ring_test */
#include <stdint.h>
#include <stdio.h>
#define FELUCCA_UAC 1
#define FELUCCA_UAC_48K 1
#define HALF_FRAMES 256u
#define RING_PUBLISH() ((void)0)
#include "uac_ring.h"

static int run(int r48, double R_ms, double ppm, int old, uint32_t *under, uint32_t *over, uint32_t *bad)
{
    double P = 256.0 * 1e6 / 44100.0 * (1.0 - ppm * 1e-6), t_blk = 0, t_pkt = 1000, t_end = -1;
    uint32_t cnt = 0, expect = 0, d[UA_MAXF], i, started = 0;
    int32_t out[64];
    uac.r48 = (uint8_t)r48, uac.go = 0, uac.feed = 0, uac.underruns = uac.overruns = 0;
    ua_w = ua_r = 0;
    *bad = 0;
    while (t_blk < 60e6) {
        if (t_end >= 0 && t_end <= t_pkt && t_end <= t_blk) {   /* a render ends: its frames in */
            uint32_t b, k;
            for (b = 0; b < HALF_FRAMES; b += 32u) {
                for (k = 0; k < 32u; k++) {
                    int32_t v = (int32_t)(int16_t)(uint16_t)(cnt++ & 0xFFFFu);
                    out[2u * k] = v, out[2u * k + 1u] = v;
                }
                uac_tap(out, 32u);
            }
            t_end = -1;
        } else if (t_blk <= t_pkt) {            /* a render starts */
            uac_ring_start(1);
            if (old) uac_ring_low();            /* (rc8: the fill read here) */
            t_end = t_blk + R_ms * 1000.0, t_blk += P;
        } else {                                /* a packet */
            uint32_t n = uac_packet(d);
            if (uac.go) {
                for (i = 0; i < n; i++) {
                    uint32_t f = d[i] & 0xFFFFu;
                    if (!started) {
                        if (f == 0u && (d[i] >> 16) == 0u) continue;   /* (the priming silence) */
                        started = 1, expect = f;
                    }
                    if (!r48 && f != (expect & 0xFFFFu)) (*bad)++, expect = f;
                    expect++;
                }
            }
            t_pkt += 1000.0;
        }
    }
    *under = uac.underruns, *over = uac.overruns;
    return 0;
}

int main(void)
{
    static const double RS[] = {0.5, 1.0, 1.5, 1.8, 2.0, 2.5, 3.0, 4.0, 5.0, 5.5};
    uint32_t u, o, b, fails = 0, old_bad = 0;
    unsigned k, r48, q;
    for (r48 = 0; r48 < 2u; r48++)
        for (q = 0; q < 2u; q++)
            for (k = 0; k < sizeof RS / sizeof RS[0]; k++) {
                double ppm = q ? 200.0 : -200.0;
                run((int)r48, RS[k], ppm, 0, &u, &o, &b);
                if (u || o || b) {
                    fails++;
                    printf("  FAIL  uac ring: %s, a render of %.1f ms, clock %+.0f ppm: %u underruns, %u overruns, %u "
                           "frames out of order\n", r48 ? "48 kHz" : "44.1 kHz", RS[k], ppm, u, o, b);
                }
                run((int)r48, RS[k], ppm, 1, &u, &o, &b);   /* (the rc8 way) */
                if (RS[k] >= 2.5 && u) old_bad++;
            }
    printf("  %s  uac ring: renders of 0.5 .. 5.5 ms (CPU NOW 9 .. 95 %%), 44.1 and 48 kHz, the clock +-200 ppm, 60 s each: "
           "no underrun, no overrun, every frame once in order\n", fails ? "FAIL" : "ok  ");
    printf("  %s  uac ring: read where the render starts (rc8), renders of 2.5 ms and more ran dry (%u of 20 runs)\n",
           old_bad == 20u ? "ok  " : "FAIL", old_bad);
    fails += old_bad != 20u;
    printf("uac ring test: %s\n", fails ? "FAILED" : "passed");
    return fails ? 1 : 0;
}
