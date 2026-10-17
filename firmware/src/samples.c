/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's sample slots: 4 slots of 72 KiB in the platform's sample area (platform.c: 0x97000..0xDF000), each one
 * sample, mono, 22050 Hz, IMA ADPCM (4 bits, low nibble first, from predictor 0 / step index 0): 73216 bytes of data,
 * 146432 samples, 6.6 s a slot (26.6 s in all). The layout and the upload protocol follow Felucca's user sample slots
 * (hugelton/Felucca: web/EDITOR_PROTOCOL.md, cmds 11..15; tools/sampleio.py user_slot), GPL-3.0, with GR!MM's own
 * magic and SysEx ID so neither firmware takes the other's data or commands:
 *   header at 0 (480 bytes): magic "GSMP", u16 version 1, u8 zone count (1), u8 0; name, 8 ASCII bytes; u32 data
 *   length, u32 CRC-32 (zlib) of the data, 8 bytes 0; one zone of 28 bytes: u32 off (0), n (samples), loop start, loop
 *   end, rate (Hz / 44100 x 65536: 32768), i16 root x 16, predictor; u8 step index, lo, hi, looped. All little endian.
 *   data at 512.
 * A slot plays once its header is good, its data's CRC matches and its seek index is built (smp[k].ready): the
 * ADPCM decoder state every SMP_SEG samples, so a read starts anywhere with at most SMP_SEG - 1 decodes. The main loop
 * checks and indexes a little at a time (samples_poll), after boot and after an upload. An empty, unfinished or broken
 * slot is never ready: it plays nothing.
 *
 * Upload (tools/fm1_sample_upload.py): SysEx F0 7D 47 52 <cmd> <args> F7 ("GR"), answered with the same head and cmd:
 *   11 BEGIN  slot                      -> slot, rc   the slot is empty from now on (its first sector erased)
 *   12 WRITE  slot, off (3 x 7 bit), pack7 data (<= 256 B)  -> slot, off (3 x 7), rc   off from 512 on, each one
 *             where the last ended, a multiple of 256; a write at a 4 KiB boundary erases that sector first
 *   13 END    slot, pack7 header (480 B) -> slot, rc   the data's CRC checked, the header written last
 *   14 ERASE  slot                      -> slot, rc   the whole slot (~1 s)
 *   15 INFO   (none)                    -> slots, then a slot each: state (0 empty, 1 ready, 2 checking), name
 *             (0-terminated), samples (3 x 7 bit), the slot's capacity in samples (3 x 7 bit); then (26c) a slot
 *             each: its root note (MIDI)
 *   16 FACTORY (none)                   -> rc   all four slots their factory samples again (~5 s)
 *   rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 not begun / out of order, 5 header or CRC, 6 busy: something sounds
 *   (the flash stops the audio while it writes: notes, a release, DELAY's or SPACE's tail), 7 no flash.
 * pack7: groups of up to 7 bytes, each after one byte of their top bits (bit j = bit 7 of byte j), as Felucca's.
 * Factory samples (tools/gen_samples.py: the four of assets/samples/, as an upload makes them): at power-on each slot
 * that is erased (every byte 0xFF: it holds nothing, not even another firmware's data) gets its own; a slot that
 * holds anything is never touched there. RESTORE (SYSTEM 2's SAMPLES, the editor's FACTORY) writes all four. */
#include "grimm_samples.h"

#define SMP_SLOTS 4u
#define SMP_SLOT_SIZE (72u * 1024u)             /* 72 KiB (so written: the build takes 0x12000 for a register address) */
#define SMP_DATA_OFF 512u
#define SMP_HDR_LEN 480u
#define SMP_MAX_DATA (SMP_SLOT_SIZE - SMP_DATA_OFF)   /* 73216 B */
#define SMP_MAX_N (SMP_MAX_DATA * 2u)           /* 146432 samples: 6.64 s at 22050 Hz */
#define SMP_SEG_LOG2 8
#define SMP_SEG (1u << SMP_SEG_LOG2)            /* the seek index's step, samples */
#define SMP_NSEG (SMP_MAX_N / SMP_SEG)          /* 572 */
#define SMP_MAGIC 0x504D5347u                   /* "GSMP" */
#define SMP_RATE_Q16 32768u                     /* 22050 / 44100 */
#define SMP_SCAN_SEGS 4u                        /* a main loop pass checks and indexes this many segments */
_Static_assert(SMP_SLOTS * SMP_SLOT_SIZE <= SAMPLE_AREA_LEN, "the sample slots in the platform's sample area");
_Static_assert(SMP_MAX_N % SMP_SEG == 0u, "whole segments");

static const int16_t IMA_STEP[89] = {           /* IMA ADPCM (Felucca's eng_sample.c) */
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

/* Felucca's gr_dec as two tables (ima_init, at boot, before any slot is checked): for each step index and the code's
 * low 3 bits, the difference (step / 8 + step, step / 2, step / 4 as the bits say) and the next step index (clamped) */
static uint16_t ima_vd[89u * 8u];                /* (up to 1.875 x 32767: unsigned) */
static uint8_t ima_nx[89u * 8u];
static void ima_init(void)
{
    uint32_t i, c;
    for (i = 0; i < 89u; i++)
        for (c = 0; c < 8u; c++) {
            int32_t step = IMA_STEP[i], vd = step >> 3;
            if (c & 4u)
                vd += step;
            if (c & 2u)
                vd += step >> 1;
            if (c & 1u)
                vd += step >> 2;
            ima_vd[i * 8u + c] = (uint16_t)vd;
            ima_nx[i * 8u + c] = (uint8_t)clamp((int32_t)i + IMA_IDX[c], 0, 88);
        }
}
/* one IMA ADPCM code (4 bits) into the decoder pred, idx */
static inline int32_t ima_nib(uint32_t code, int32_t *pred, int32_t *idx)
{
    uint32_t k = (uint32_t)*idx * 8u + (code & 7u);
    int32_t vd = (int32_t)ima_vd[k], m = -(int32_t)((code >> 3) & 1u);
    *pred = clamp(*pred + ((vd ^ m) - m), -32768, 32767);
    *idx = ima_nx[k];
    return *pred;
}
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) (Felucca's eng_sample.c, as GR!MM's
 * GRAIN had it: the SAMPLE source's speed, the GRAIN effect's grain lengths and intervals) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

/* one IMA ADPCM sample at index pos of data */
static inline int32_t ima_dec(const uint8_t *data, uint32_t pos, int32_t *pred, int32_t *idx)
{
    uint32_t b = data[pos >> 1];
    return ima_nib((pos & 1u) ? (b >> 4) : (b & 15u), pred, idx);
}

enum { SS_EMPTY, SS_READY, SS_SCAN };
typedef struct {
    volatile uint8_t ready;                     /* main loop: it plays (the audio ISR reads data, n, root16) */
    uint8_t state;                              /* main loop: SS_ */
    uint16_t seg;                               /* SS_SCAN: the next segment */
    const uint8_t *data;                        /* its ADPCM (XIP) */
    uint32_t n, len, crc, want;                 /* samples, data bytes; the CRC so far, the header's */
    int32_t root16, pred, idx;                  /* the root (1/16 semitone); SS_SCAN: the decoder */
    char name[9];
} smp_slot_t;
static smp_slot_t smp[SMP_SLOTS];
static int16_t smp_ipred[SMP_SLOTS][SMP_NSEG] __attribute__((section(".pool")));   /* the decoder before segment k */
static uint8_t smp_iidx[SMP_SLOTS][SMP_NSEG] __attribute__((section(".pool")));

static uint32_t rd16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }
static uint32_t smp_crc(uint32_t c, const uint8_t *p, uint32_t n)   /* CRC-32 (zlib), c = ~crc so far */
{
    uint32_t k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return c;
}

/* main loop: look at slot k's header again; a good one starts its check (SS_SCAN), else the slot is empty */
static void smp_look(uint32_t k)
{
    smp_slot_t *s = &smp[k];
    const uint8_t *f = sample_flash(), *h;
    uint32_t i, n, len;
    s->ready = 0;
    s->state = SS_EMPTY;
    s->name[0] = 0;
    if (!f)
        return;
    h = f + k * SMP_SLOT_SIZE;
    len = rd32(h + 16);
    n = rd32(h + 36);
    if (rd32(h) != SMP_MAGIC || rd16(h + 4) != 1u || h[6] < 1u || len == 0u || len > SMP_MAX_DATA ||
        rd32(h + 32) != 0u || n < 64u || n > len * 2u || rd32(h + 48) != SMP_RATE_Q16)
        return;
    for (i = 0; i < 8u; i++)
        s->name[i] = h[8 + i] >= 32u && h[8 + i] < 127u ? (char)h[8 + i] : 0;
    s->name[8] = 0;
    s->data = h + SMP_DATA_OFF;
    s->len = len;
    s->n = n;
    s->want = rd32(h + 20);
    s->root16 = (int16_t)rd16(h + 52);
    s->crc = 0xFFFFFFFFu;
    s->pred = s->idx = 0;
    s->seg = 0;
    s->state = SS_SCAN;
}

/* main loop: a few segments of slot k's CRC and seek index; at the end it plays, or is empty if the CRC is wrong */
static void smp_scan(uint32_t k)
{
    smp_slot_t *s = &smp[k];
    uint32_t j;
    for (j = 0; j < SMP_SCAN_SEGS && s->state == SS_SCAN; j++) {
        uint32_t p0 = (uint32_t)s->seg << SMP_SEG_LOG2, p, b0, b1;
        if (p0 >= s->n) {                       /* every segment indexed: the rest of the CRC, then the verdict */
            b0 = (p0 >> 1) < s->len ? p0 >> 1 : s->len;
            s->crc = smp_crc(s->crc, s->data + b0, s->len - b0);
            if (~s->crc == s->want) {
                s->state = SS_READY;
                s->ready = 1;
            } else
                s->state = SS_EMPTY;
            return;
        }
        smp_ipred[k][s->seg] = (int16_t)s->pred;
        smp_iidx[k][s->seg] = (uint8_t)s->idx;
        for (p = p0; p < p0 + SMP_SEG && p < s->n; p++)
            ima_dec(s->data, p, &s->pred, &s->idx);
        b0 = p0 >> 1;
        b1 = (p0 + SMP_SEG) >> 1;
        b1 = b1 < s->len ? b1 : s->len;
        s->crc = smp_crc(s->crc, s->data + b0, b1 > b0 ? b1 - b0 : 0u);
        s->seg++;
    }
}

/* ---- the upload */
#define SX_HEAD 3u                              /* 7D 47 52 */
static uint8_t sx_buf[SMP_HDR_LEN + 8u];        /* a decoded data block or header */
#define SX_INFO (1u + SMP_SLOTS * 17u)          /* INFO's reply: slots, then each: state, name (8), 0, n (3), max (3);
                                                 * then each: its root */
static uint8_t sx_out[SX_INFO + 6u];            /* (head, cmd, F7 around the longest reply: INFO) */
static int32_t up_slot = -1;                    /* the slot being written, -1 none */
static uint32_t up_next;                        /* the next offset WRITE must bring */

static uint32_t unpack7(const uint8_t *p, uint32_t n, uint8_t *out, uint32_t max)   /* -> bytes, or max + 1 = bad */
{
    uint32_t i = 0, o = 0, j;
    while (i < n) {
        uint32_t top = p[i++], g = n - i < 7u ? n - i : 7u;
        if (!g || (top >> g))
            return max + 1u;                    /* a mask with no byte after it, or bits for bytes that are not there */
        for (j = 0; j < g; j++) {
            if (o >= max)
                return max + 1u;
            out[o++] = (uint8_t)(p[i + j] | ((top >> j) & 1u) << 7);
        }
        i += g;
    }
    return o;
}
static void sx_reply(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i, w = 0;
    sx_out[w++] = 0xF0;
    sx_out[w++] = 0x7D;
    sx_out[w++] = 0x47;
    sx_out[w++] = 0x52;
    sx_out[w++] = (uint8_t)cmd;
    for (i = 0; i < n && w < sizeof sx_out - 1u; i++)
        sx_out[w++] = a[i] & 0x7Fu;
    sx_out[w++] = 0xF7;
    sysex_send(sx_out, w);
}

/* slot k := its factory sample, as an upload writes one: erased, the data, the header last, then checked; 0 ok, or rc
 * (2 erase, 3 write) */
static uint32_t smp_factory(uint32_t k)
{
    uint32_t i, c, len = SMP_FACT_LEN[k];
    smp[k].ready = 0;
    smp[k].state = SS_EMPTY;
    smp[k].name[0] = 0;
    if ((int32_t)k == up_slot)
        up_slot = -1;
    for (i = 0; i < SMP_SLOT_SIZE; i += 0x1000u)
        if (sample_flash_erase(k * SMP_SLOT_SIZE + i))
            return 2;
    for (i = 0; i < len; i += c) {              /* (through RAM: the flash does not read itself while it writes) */
        c = len - i < 256u ? len - i : 256u;
        memcpy(sx_buf, SMP_FACT_DATA[k] + i, c);
        if (sample_flash_write(k * SMP_SLOT_SIZE + SMP_DATA_OFF + i, sx_buf, c))
            return 3;
    }
    memcpy(sx_buf, SMP_FACT_HDR[k], SMP_HDR_LEN);
    if (sample_flash_write(k * SMP_SLOT_SIZE, sx_buf, SMP_HDR_LEN))
        return 3;
    smp_look(k);
    return 0;
}
static uint32_t smp_erased(void)                /* power-on: the slots that hold nothing (every byte 0xFF), a bit each */
{
    const uint8_t *f = sample_flash();
    uint32_t k, i, m = 0;
    for (k = 0; f && k < SMP_SLOTS; k++) {
        const uint32_t *w = (const uint32_t *)(const void *)(f + k * SMP_SLOT_SIZE);
        for (i = 0; i < SMP_SLOT_SIZE / 4u && w[i] == 0xFFFFFFFFu; i++)
            ;
        if (i == SMP_SLOT_SIZE / 4u)
            m |= 1u << k;
    }
    return m;
}

/* one frame (7-bit bytes after F0, the head included); silent: nothing sounds (the flash may stop the audio) */
static void smp_frame(const uint8_t *f, uint32_t n, int silent)
{
    uint32_t cmd, slot, rc = 0, i;
    uint8_t r[SX_INFO];
    if (n < SX_HEAD + 1u || f[1] != 0x47 || f[2] != 0x52)
        return;                                 /* another 7D frame: not ours */
    cmd = f[3];
    f += SX_HEAD + 1u;
    n -= SX_HEAD + 1u;
    if (cmd == 15u) {                           /* INFO */
        uint32_t w = 0, k;
        r[w++] = SMP_SLOTS;
        for (k = 0; k < SMP_SLOTS; k++) {
            r[w++] = smp[k].state;
            for (i = 0; smp[k].name[i] && i < 8u; i++)
                r[w++] = (uint8_t)smp[k].name[i];
            r[w++] = 0;
            r[w++] = (uint8_t)(smp[k].ready ? smp[k].n & 0x7Fu : 0u);
            r[w++] = (uint8_t)(smp[k].ready ? (smp[k].n >> 7) & 0x7Fu : 0u);
            r[w++] = (uint8_t)(smp[k].ready ? (smp[k].n >> 14) & 0x7Fu : 0u);
            r[w++] = SMP_MAX_N & 0x7Fu;
            r[w++] = (SMP_MAX_N >> 7) & 0x7Fu;
            r[w++] = (SMP_MAX_N >> 14) & 0x7Fu;
        }
        for (k = 0; k < SMP_SLOTS; k++)         /* (26c: the roots, after what older tools read) */
            r[w++] = (uint8_t)(smp[k].state != SS_EMPTY ? clamp((smp[k].root16 + 8) >> 4, 0, 127) : 60);
        sx_reply(cmd, r, w);
        return;
    }
    if (cmd == 16u) {                           /* FACTORY: all four, while nothing sounds */
        uint32_t k;
        rc = !sample_flash() ? 7u : !silent ? 6u : 0u;
        for (k = 0; k < SMP_SLOTS && !rc; k++)
            rc = smp_factory(k);
        r[0] = (uint8_t)rc;
        sx_reply(cmd, r, 1);
        return;
    }
    if (cmd < 11u || cmd > 14u || n < 1u)
        return;
    slot = f[0];
    r[0] = (uint8_t)slot;
    if (slot >= SMP_SLOTS)
        rc = 1;
    else if (!sample_flash())
        rc = 7;
    else if (!silent) {
        rc = 6;
        up_slot = -1;                           /* (an upload cut by sound must begin again) */
    }
    if (cmd == 12u) {                           /* WRITE */
        uint32_t off = n >= 4u ? f[1] | (uint32_t)f[2] << 7 | (uint32_t)f[3] << 14 : 0u, d = 0;
        r[1] = (uint8_t)(off & 0x7Fu);
        r[2] = (uint8_t)((off >> 7) & 0x7Fu);
        r[3] = (uint8_t)((off >> 14) & 0x7Fu);
        if (!rc && n >= 4u) {
            d = unpack7(f + 4, n - 4u, sx_buf, 256u);
            if (d > 256u || !d || off + d > SMP_SLOT_SIZE)
                rc = 1;
            else if ((int32_t)slot != up_slot || off != up_next || (off & 0xFFu))
                rc = 4;
            else if (!(off & 0xFFFu) && sample_flash_erase(slot * SMP_SLOT_SIZE + off))
                rc = 2;
            else if (sample_flash_write(slot * SMP_SLOT_SIZE + off, sx_buf, d))
                rc = 3;
            else
                up_next = off + d;
        } else if (!rc)
            rc = 1;
        r[4] = (uint8_t)rc;
        sx_reply(cmd, r, 5);
        return;
    }
    if (!rc && cmd == 11u) {                    /* BEGIN: empty at once */
        smp[slot].ready = 0;
        smp[slot].state = SS_EMPTY;
        smp[slot].name[0] = 0;
        if (sample_flash_erase(slot * SMP_SLOT_SIZE))
            rc = 2;
        else {
            up_slot = (int32_t)slot;
            up_next = SMP_DATA_OFF;
        }
    } else if (!rc && cmd == 13u) {             /* END: the header, if it is the data's */
        uint32_t d = unpack7(f + 1, n - 1u, sx_buf, SMP_HDR_LEN), len, crc;
        const uint8_t *data = sample_flash() + slot * SMP_SLOT_SIZE + SMP_DATA_OFF;
        if ((int32_t)slot != up_slot)
            rc = 4;
        else if (d != SMP_HDR_LEN || rd32(sx_buf) != SMP_MAGIC || (len = rd32(sx_buf + 16)) != up_next - SMP_DATA_OFF)
            rc = 5;
        else if ((crc = ~smp_crc(0xFFFFFFFFu, data, len)) != rd32(sx_buf + 20))
            rc = 5;
        else if (sample_flash_write(slot * SMP_SLOT_SIZE, sx_buf, SMP_HDR_LEN))
            rc = 3;
        else {
            up_slot = -1;
            smp_look(slot);                     /* checked and indexed from now on, then it plays */
        }
    } else if (!rc && cmd == 14u) {             /* ERASE */
        smp[slot].ready = 0;
        smp[slot].state = SS_EMPTY;
        smp[slot].name[0] = 0;
        for (i = 0; i < SMP_SLOT_SIZE && !rc; i += 0x1000u)
            if (sample_flash_erase(slot * SMP_SLOT_SIZE + i))
                rc = 2;
        if ((int32_t)slot == up_slot)
            up_slot = -1;
    }
    r[1] = (uint8_t)rc;
    sx_reply(cmd, r, 2);
}

static int ed_frame(const uint8_t *f, uint32_t n, int silent);   /* (editor.c: the editor's commands 20..22) */
/* main loop, every pass: a SysEx frame (the editor's, or the upload's), then a little of the slots' checks. silent:
 * nothing sounds */
static void samples_poll(int silent)
{
    static uint8_t booted;
    const uint8_t *f;
    uint32_t n, k;
    if (!booted) {                              /* the slots as the flash holds them */
        booted = 1;
        for (k = 0; k < SMP_SLOTS; k++)
            smp_look(k);
    }
    if (sysex_get(&f, &n)) {
        if (!ed_frame(f, n, silent)) smp_frame(f, n, silent);
        sysex_done();
    }
    for (k = 0; k < SMP_SLOTS; k++)
        if (smp[k].state == SS_SCAN) {
            smp_scan(k);
            break;                              /* one slot a pass */
        }
}
