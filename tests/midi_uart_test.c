/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * GR!MM changes: Copyright (C) 2026 GR!MM */
/* The TRS jack's MIDI parser (firmware/src/midi_uart.c) on a computer, after SLOOP's tests/midi_uart_test.c (its
 * cases: running status, realtime inside a message, SysEx and system common dropped, the ring read by content: an MPC
 * Sample's pad, byte by byte round the ring, a received FD). GR!MM: the clock and START / STOP go to platform.c's
 * realtime ring with the jack as their source. tests/run_host.sh builds and runs it. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define MQ 64u
static uint32_t midi_in_q[MQ];
static volatile uint32_t mi_w, mi_r;
enum { RT_USB, RT_TRS };
static uint32_t rt_got[16], rt_n;               /* (platform.c's ring: what came, byte | source << 8) */
static void rt_in(uint32_t st, uint32_t src) { if (rt_n < 16u) rt_got[rt_n++] = st | src << 8; }
#include "../firmware/src/midi_uart.c"

static int check(const char *what, int ok)
{
    printf("  %-4s  trs: %s\n", ok ? "ok" : "FAIL", what);
    return ok ? 0 : 1;
}
static uint32_t dma_w;                          /* the RX DMA: bytes land in the ring in order */
static void dma_put(const uint8_t *p, uint32_t n)
{
    while (n--) um_ring[dma_w] = *p++, dma_w = (dma_w + 1u) & (UM_RING - 1u);
}
static int ring_clean(void)
{
    uint32_t i;
    for (i = 0; i < UM_RING; i++)
        if (um_ring[i] != UM_EMPTY) return 0;
    return 1;
}

int main(void)
{
    static const uint8_t in[] = {
        0x90, 60, 100, 62, 101,                 /* note on + running status */
        0xF8, 64, 0xFE, 102,                    /* realtime inside a message: the clock to the ring, FE dropped */
        0xC1, 5, 6,                             /* program change + running */
        0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7, 70, 71,   /* SysEx dropped; it cancels running status */
        0xB0, 7, 0x7F, 0xF2, 1, 2, 9, 9,        /* CC, song position (dropped), data without a status */
        0xFA, 0xFB, 0xFC, 0x80, 60, 0,          /* START, CONTINUE, STOP; a note-off */
    };
    static const uint32_t want[] = {0x643C9009u, 0x653E9009u, 0x66409009u, 0x0005C10Cu, 0x0006C10Cu, 0x7F07B00Bu,
                                    0x003C8008u};
    static const uint8_t pad[] = {0x90, 0x3A, 0x11, 0xA0, 0x3A, 0x75, 0x3A, 0x7F, 0x3A, 0x06, 0x80, 0x3A, 0x00};
    uint32_t i, k, w0, ok, n = sizeof want / sizeof want[0];
    int bad = 0;
    for (i = 0; i < sizeof in; i++) um_byte(in[i]);
    ok = mi_w == n;
    for (i = 0; i < n && i < mi_w; i++) ok &= midi_in_q[i] == want[i];
    bad += check("running status, realtime inside a message, SysEx and system common dropped", (int)ok);
    bad += check("the clock, START, CONTINUE, STOP to the realtime ring, from the jack",
                 rt_n == 4u && rt_got[0] == (0xF8u | RT_TRS << 8) && rt_got[1] == (0xFAu | RT_TRS << 8) &&
                 rt_got[2] == (0xFBu | RT_TRS << 8) && rt_got[3] == (0xFCu | RT_TRS << 8));
    {                                           /* the channel in the packet as USB's: cable 0, CIN = status >> 4 */
        static const uint8_t chs[] = {0x90, 60, 1, 0x91, 61, 2, 0x94, 62, 3, 0x99, 36, 4, 0x9F, 63, 5, 0x89, 36, 0};
        w0 = mi_w, ok = 1;
        for (i = 0; i < sizeof chs; i++) um_byte(chs[i]);
        for (i = 0; i < 6u; i++) {
            uint32_t pkt = midi_in_q[(w0 + i) % MQ], st = chs[3u * i];
            ok &= mi_w == w0 + 6u && (pkt & 0xFFu) == (st >> 4) && ((pkt >> 8) & 0xFFu) == st &&
                  ((pkt >> 16) & 0x7Fu) == chs[3u * i + 1u] && (pkt >> 24) == chs[3u * i + 2u];
        }
        bad += check("channels 1, 2, 5, 10, 16 as USB-MIDI packets", (int)ok);
    }
    memset(&um, 0, sizeof um);                  /* the ring, read by content */
    for (i = 0; i < UM_RING; i++) um_ring[i] = UM_EMPTY;
    mi_r = mi_w;
    um_drain();
    bad += check("nothing landed, nothing read", mi_w == mi_r && um.rd == 0 && um.bytes == 0);
    dma_put(pad, sizeof pad);
    um_drain();
    bad += check("a pad's burst: its last message (the note-off) at once",
                 mi_w - mi_r == 5u && midi_in_q[(mi_w - 1u) % MQ] == 0x003A8008u && ring_clean());
    mi_r = mi_w;
    for (ok = 1, k = 0; k < 40u; k++) {         /* byte by byte, round the ring many times */
        w0 = mi_w;
        for (i = 0; i < sizeof pad; i++) {
            dma_put(pad + i, 1);
            um_drain();
            if (i == 2u) ok &= mi_w == w0 + 1u;  /* (the note-on with its third byte) */
        }
        ok &= mi_w == w0 + 5u && midi_in_q[(mi_w - 1u) % MQ] == 0x003A8008u && um.rd == dma_w;
        mi_r = mi_w;
    }
    bad += check("byte by byte, across the ring's wrap", (int)ok && ring_clean());
    dma_put((const uint8_t[]){0x90, 0x3C, 0x40, UM_EMPTY}, 4);   /* line noise as the last byte */
    um_drain();
    ok = mi_w - mi_r == 1u && um.rd != dma_w;   /* (the stray byte waits for the next) */
    dma_put((const uint8_t[]){0x80, 0x3C, 0x00}, 3);
    um_drain();
    ok &= mi_w - mi_r == 2u && midi_in_q[(mi_w - 1u) % MQ] == 0x003C8008u && um.rd == dma_w;
    bad += check("a received FD passed over, not taken for an empty slot", (int)ok && ring_clean());
    return bad != 0;
}
