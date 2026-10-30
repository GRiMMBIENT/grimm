/* SPDX-License-Identifier: GPL-3.0-only */
/* HX (hx.h): Optimist's BLE-MIDI stack (ble_stack.c, ble_vm.c; w0ts/optimist) and fm1-dock's glue around
 * it, after Optimist's io/midi/midi_ble.c without what fm1-dock does not have (CLOCK OUT, the mirror, the
 * central role, the second core):
 *   in   the stack decodes a central's writes in the BLE interrupts into USB-MIDI event packets (channel
 *        messages, clock and transport); they wait in in_q until TIMER5 (poll) hands them to the host's
 *        MIDI ring, where a module gets them as it gets USB's. No timing: they play when they arrive.
 *   out  the host's midi_out_event (the audio ISR) queues each packet with its millisecond while a central
 *        listens; the stack packs them into notifications at the next connection event.
 *   OFF  (the default) the radio is never started: no BLE / RF register written. A central's notes are
 *        ended with note-offs when it goes.
 * The settings record (HX_KEPT, the host keeps it): the random static address made once, so a central
 * finds the same device after a power-off, and the radio's trims copy (ble_vm.c). */
#define BLE_API static
#define BLE_DEVICE_NAME "GR!MM"                  /* GR!MM: was "FM1-DOCK" */
#define FM1_IRQ_UNIT 1                          /* hal/fm1_irq.h: registers and attach only (the host has the rest) */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "ble_stack.c"
#include "ble_vm.c"
#include "hx.h"

#define BMQ 64u
#define ADDR_KEPT 0xA5u                         /* kept[6]: kept[0..5] is the address */
static const hx_host_t *host;
static uint32_t in_q[BMQ], out_q[BMQ], out_ms[BMQ];
static volatile uint32_t in_w, in_r, out_w, out_r, hx_ms;
static volatile uint8_t out_on, release;
static uint8_t was_conn, on, up, rf_src, held_boot, kept_new;
static uint8_t held[16][16];                    /* the notes a central has on: bit per note, per channel (TIMER5's) */
static uint8_t kept[HX_KEPT];
static struct ble_rf_trims use;

BLE_API void ble_app_state(void)                /* the BLE interrupts, or the main loop with them held */
{
    uint8_t c = (uint8_t)ble_connected();
    out_on = (uint8_t)ble_midi_ready();
    if (!out_on)
        out_r = out_w;                          /* nothing waits for a central that does not listen */
    if (was_conn && !c)
        release = 1;
    was_conn = c;
}

BLE_API int ble_app_midi_peek(uint32_t *pkt, uint32_t *t_ms)
{
    if (out_r == out_w)
        return 0;
    RING_PUBLISH();                             /* the slot read after the index (the audio ISR fills it) */
    *pkt = out_q[out_r % BMQ];
    *t_ms = out_ms[out_r % BMQ];
    return 1;
}

BLE_API void ble_app_midi_pop(void)
{
    RING_PUBLISH();
    out_r++;
}

BLE_API void ble_app_midi_in(uint32_t pkt, uint16_t ts, uint16_t last_ts)
{
    uint32_t cin = pkt & 15u, st = (pkt >> 8) & 0xFFu;
    (void)ts, (void)last_ts;
    if (!on || (!(cin >= 8u && cin <= 0xEu) && !(cin == 0xFu && (st == 0xF8u || (st >= 0xFAu && st <= 0xFCu)))))
        return;                                 /* channel messages, clock and transport only (as usb.c's) */
    if (in_w - in_r >= BMQ)
        return;
    in_q[in_w % BMQ] = pkt;
    RING_PUBLISH();
    in_w++;
}

static void note_seen(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 15u, n = (pkt >> 16) & 0x7Fu;
    if (st == 0x90u && ((pkt >> 24) & 0x7Fu))
        held[ch][n >> 3] |= (uint8_t)(1u << (n & 7u));
    else if (st == 0x80u || st == 0x90u)
        held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
}

static void notes_end(void)                     /* a note-off for each note a central left on (the next tick if full) */
{
    uint32_t ch, n, left = 0;
    for (ch = 0; ch < 16u; ch++)
        for (n = 0; n < 128u; n++) {
            if (!(held[ch][n >> 3] & (1u << (n & 7u))))
                continue;
            if (host->midi_in(8u | (0x80u | ch) << 8 | n << 16))
                held[ch][n >> 3] &= (uint8_t)~(1u << (n & 7u));
            else
                left = 1;
        }
    if (!left)
        release = 0;
}

static void hx_poll(void)                       /* TIMER5, 2 kHz */
{
    static uint32_t last, acc;
    uint32_t t = fm1_ticks();
    acc += t - last;
    last = t;
    while (acc >= 1000u * FM1_TICKS_PER_US) {   /* the milliseconds of the BLE-MIDI timestamps */
        acc -= 1000u * FM1_TICKS_PER_US;
        hx_ms++;
    }
    while (in_r != in_w) {
        RING_PUBLISH();
        if (!host->midi_in(in_q[in_r % BMQ]))
            return;                             /* the host's ring is full: the next tick */
        note_seen(in_q[in_r % BMQ]);
        RING_PUBLISH();
        in_r++;
    }
    if (release)
        notes_end();                            /* (after what the central had sent) */
}

static void hx_out(uint32_t pkt)                /* the audio ISR */
{
    if (!out_on || out_w - out_r >= BMQ)
        return;
    out_q[out_w % BMQ] = pkt;
    out_ms[out_w % BMQ] = hx_ms;
    RING_PUBLISH();
    out_w++;
}

static int vm_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)   /* the flash as stored (no XIP decryption) */
{
    (void)ctx;
    return host->flash_read(off, dst, n);
}

/* the radio, the baseband and the stack, the first time BLUETOOTH is ON, the BLE interrupts masked -> 1 */
static int radio_start(void)
{
    uint8_t a[6], rnd;
    uint32_t i;
    if (up)
        return 1;
    if (rf_src == BLE_RF_NONE)
        return 0;
    rnd = ble_hw_addr(a);                       /* (0: the VM's public address) */
    if (rnd && kept[6] == ADDR_KEPT && (kept[5] >> 6) == 3u) {
        for (i = 0; i < 6u; i++)
            a[i] = kept[i];
    } else if (rnd) {
        for (i = 0; i < 6u; i++)
            kept[i] = a[i];
        kept[6] = ADDR_KEPT;
        kept_new = 1;
    }
    ble_init(a, rnd);
    ble_rf_copy_get(kept + 8, &use);            /* (the VM's set: ble_rf_choose made the copy from it) */
    ble_hw_wl82_start(&use);
    up = 1;
    return 1;
}

static void hx_set(uint32_t want)               /* the main loop */
{
    want = want ? 1u : 0u;
    if (want == on)
        return;
    on = (uint8_t)want;
    if (on && (held_boot || !radio_start()))
        return;                                 /* (no trims, or a failed boot: only the setting) */
    if (!up)
        return;
    fm1_ble_irqs_hold(1);
    ble_enable((int)on);
    if (!on)
        release = 1;
    fm1_ble_irqs_hold(0);
}

static uint32_t hx_state(void)
{
    if (!on)
        return HX_S_OFF;
    if (!up)
        return rf_src == BLE_RF_NONE ? HX_S_NOCAL : HX_S_HELD;
    return ble_midi_ready() ? HX_S_READY : ble_connected() ? HX_S_LINK : HX_S_ADV;
}

static int hx_kept(uint8_t rec[HX_KEPT])
{
    uint32_t i;
    if (!kept_new)
        return 0;
    kept_new = 0;
    for (i = 0; i < HX_KEPT; i++)
        rec[i] = kept[i];
    return 1;
}

static void hx_stop(void)                       /* before flash under HX is written: nothing of HX runs after it */
{
    fm1_ble_irqs_hold(1);
    out_on = 0;
}

static uint32_t hx_init(const hx_host_t *h, const uint8_t rec[HX_KEPT], uint32_t want, uint32_t failed)
{
    struct ble_vm_info info;
    struct ble_rf_trims vm;
    uint32_t i;
    int complete, save;
    host = h;                                   /* (GR!MM: linked into the image, its .data is crt0's) */
    for (i = 0; i < HX_KEPT; i++)
        kept[i] = rec[i];
    complete = ble_vm_scan(vm_rd, 0, &info, &vm, 0, 0);
    rf_src = (uint8_t)ble_rf_choose(complete, &vm, info.area, kept + 8, &use, &save);
    if (save)
        kept_new = 1;
    ble_hw_wl82_attach();                       /* the two vectors, masked: no BLE / RF register written */
    held_boot = (uint8_t)(want && rf_src != BLE_RF_NONE && !ble_boot_radio((int)want, failed, 1));
    on = 0;
    if (want && !held_boot)
        hx_set(1);
    else
        on = (uint8_t)(want ? 1u : 0u);
    return hx_state();
}

/* GR!MM: linked into the image (ble.o; app.ld puts its .bss in RAMTEXT), not at a place of its own: no header
 * section, platform.c calls it by this name */
const hx_api_t hx_head ={HX_MAGIC, 0, 0, HX_ABI, hx_init, hx_set, hx_poll, hx_out, hx_state, hx_kept, hx_stop};
