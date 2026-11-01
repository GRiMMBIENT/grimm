/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 zvenson (Dinghy: the wiring). The platform it wires up: Copyright (C) 2026 Leo Kuroshita
 * (@kurogedelic), Hügelton Instruments (Felucca), with the changes of SLOOP (isod89). GR!MM changes: Copyright (C)
 * 2026 GR!MM */
/* Dinghy's platform: everything an FM-1 firmware needs before it can be fun, taken unchanged from
 * SLOOP / Felucca / sloopDX and wired up here. You should not need to touch this file.
 *
 *   boot + boot guard   a firmware that crashes at boot falls back to the USB rescue, then to the ROM
 *   USB rescue          OCT- held at power-on: no audio, only the updater (the web installer still works)
 *   updater             the M-UPGRADE SysEx path: the web installer / the hub can always reach the FM-1
 *   USB-MIDI            in and out (midi_get, midi_send)
 *   USB audio           the output as a class-compliant 16-bit stereo input, 44.1 or 48 kHz
 *   keys, knobs, LEDs   fm1_in.notes / buttons, fm1_input_edges, fm1_enc_take, fm1_led_key (hal/fm1_input.h)
 *   screen              240 x 240, lcd_fill, draw_text_box with FONT_S (16 px) / FONT_L (32 px) (gfx.c)
 *   settings            one 4 KiB flash sector with a CRC (settings_load / settings_save)
 *   crash screen        the PC of a fault, then a reboot
 *
 * The app (grimm.c) includes this file first and supplies:
 *   static void app_audio(int32_t *out, uint32_t n)   audio ISR: n stereo frames, L R L R, Q15 (+-32767), out is zero
 *   static void app_main(void)                        never returns; calls platform_poll() often (every few ms)
 */
/* the panel by name: buttons (fm1_in.buttons, fm1_input_edges, fm1_led_key: bit / id), the encoders
 * (fm1_enc_take); the 27 note keys are fm1_in.notes bit 0 (F3) .. 26 (G5), LED id 14 + key */
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1, BTN_FX = 2, BTN_SEL = 3, BTN_ENV = 4, BTN_LFO = 5, BTN_EDIT = 6,
       BTN_GLO = 7, BTN_HOME = 8, BTN_SAVE = 9, BTN_ARP = 10, BTN_SEQ = 11, BTN_PLAY = 12, BTN_REC = 13 };
enum { ENC_SELECT = 0, ENC_ALGORITHM = 1, ENC_KNOB1 = 2, ENC_KNOB2 = 3, ENC_KNOB3 = 4, ENC_KNOB4 = 5, ENC_PRESETS = 6 };
#define KEY_LED(k) (14u + (k))
#include <stdint.h>
static volatile uint32_t isr_in_audio;          /* the timer ISR's fm1_ticks inside the audio ISR, summed (GR!MM V2:
                                                 * the CPU meter's SCAN; on a computer 0) */
/* GR!MM V2: MIDI realtime, the clock and START / CONTINUE / STOP. In from USB (usb.c) and the TRS jack (midi_uart.c),
 * each with its time (rt_now: 24 a us) and its source: the timer ISR writes, the audio ISR reads (grimm.c clk_ctl, a
 * control block at a time, at the messages' time). Out (USB): the audio ISR's clock (CLOCK INT+OUT), each with the
 * time its sound plays; the timer ISR sends it then (rt_flush; on a computer: logged) */
enum { RT_USB, RT_TRS };
#define RTQ 32u                                 /* (a block's delay holds ~3 pulses at 300 BPM) */
static volatile uint16_t rt_q[RTQ];             /* the byte, its source << 8 */
static volatile uint8_t ro_q[RTQ];
static volatile uint32_t rt_t[RTQ], rt_w, rt_r, ro_t[RTQ], ro_w, ro_r;
static volatile uint8_t rt_usb_fast;            /* the app: USB's clock wanted (CLOCK USB): its MIDI read in a render too */
static uint32_t rt_now(void);                   /* (the platform's time: fm1_ticks; a computer's: its script's) */
static void rt_in_at(uint32_t st, uint32_t src, uint32_t t)   /* timer ISR: a realtime byte from src, at t */
{
    uint32_t w = rt_w;
    if (w - rt_r >= RTQ) return;                /* (full: lost; the clock goes on from the next) */
    rt_q[w % RTQ] = (uint16_t)(st | src << 8), rt_t[w % RTQ] = t;
    __asm__ volatile("" ::: "memory");
    rt_w = w + 1u;
}
static void rt_in(uint32_t st, uint32_t src) { rt_in_at(st, src, rt_now()); }
static void rt_out(uint32_t st, uint32_t due)   /* audio ISR: a realtime byte out over USB at due */
{
    uint32_t w = ro_w;
    if (w - ro_r >= RTQ) return;                /* (nobody takes them: lost) */
    ro_q[w % RTQ] = (uint8_t)st, ro_t[w % RTQ] = due;
    __asm__ volatile("" ::: "memory");
    ro_w = w + 1u;
}

#define OUT_FS (32767 << 7)                     /* (V2) the app's output words: the DAC's 24 bits, Q15 << 7 (-6 dBFS ceiling) */
static void out_usb16(const int32_t *o, int32_t *u, uint32_t n);   /* (grimm.c: those words as 16 bit, rounded: USB) */
#if defined GRIMM_WEB
#include "fm1_web.c"                 /* in a browser: a fake FM-1 (firmware/src/fm1_web.c) */
#elif defined DINGHY_HOST
#include "host.c"                  /* on a computer: a fake FM-1 (firmware/src/host.c) */
#else
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_lcd_hw.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "bootguard.h"

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_966"                   /* package identity: GR!MM 1.x (tools/build.py) */
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif

bootguard_t bootguard __attribute__((section(".noinit")));
static volatile uint32_t fm1_ms;                /* milliseconds since power-on (TIMER5) */
/* GR!MM rc11: health. What the timer ISR, the flash and the watchdog saw (TIMER4 ticks, 24 a us), for the editor's CPU
 * command (tools/fm1_cpu.py): the longest gap between two timer ticks (10 kHz: 100 us; the key scan and the LEDs stand
 * still meanwhile), how many were past 0.2 ms, 1 ms, 10 ms, the longest tick; the battery divider's range. And, kept in
 * .noinit over a reset (a power-on starts it empty): how often it started, and the resets nobody asked for: RAM kept,
 * no restart of ours (an update, USB boot, the fault screen: clean) before it. The chip's own cause (P33 RST_SRC) stays
 * set after a watchdog reset until a power cycle: through it alone, every later restart looked like one */
static struct { uint32_t gap, late, stall, lost, dur, bat_lo, bat_hi; } hl = {0, 0, 0, 0, 0, 1023, 0};
typedef struct { uint32_t magic, boots, unexp, wdt, low, why, clean, kind; } hl_boot_t;   /* kind: 1 watchdog, 2 low voltage */
hl_boot_t hl_boot __attribute__((section(".noinit")));   /* (not static: the compiler would take its magic for 0 or HL_MAGIC) */
#define HL_MAGIC 0x484C3132u                    /* "HL12" (a new layout: a new magic) */

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "libc.c"
#include "lcd.c"
#define CV_MAX (240u * 64u)                     /* the biggest text box drawn at once */
#include "gfx.c"

#define FS 44100u                               /* the sample rate */
#define HALF_FRAMES 256u                        /* I2S half buffer: 5.8 ms (as sloopDX: the USB audio band is set for it) */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
#define FELUCCA_UAC 1
#define FELUCCA_UART 1                          /* GR!MM V2: TRS MIDI IN (midi_uart.c) */
#define MIDI_RT_IN(st) rt_in(st, RT_USB)        /* (usb.c: the clock and START / CONTINUE / STOP, timed) */
#define MIDI_RT_SOON() (ro_r != ro_w && (int32_t)(ro_t[ro_r % RTQ] - fm1_ticks()) < 28800)   /* (usb.c: a clock message
                                                 * out due within 1.2 ms: EP1 left free for it, rt_flush) */
static uint32_t rt_now(void) { return fm1_ticks(); }
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif
/* the clock's messages due by now out over USB-MIDI (EP1) when it is free: the timer ISR, nested in the audio ISR too
 * (as uac_service: the audio ISR never touches the SIE, usb_poll never runs then); each within ~0.1 ms of its time */
static void rt_flush(void)
{
    uint32_t now = fm1_ticks(), n = 0, csr;
    if (ro_r == ro_w || (int32_t)(now - ro_t[ro_r % RTQ]) < 0)
        return;
    if (!usb.config || usb.suspended) {         /* (nobody there: dropped) */
        ro_r = ro_w;
        return;
    }
    sie_wr(S_INDEX, 1);
    csr = sie_rd(S_TXCSR1);
    if (csr & 0x01u)
        return;                                 /* (a packet still going: the next tick) */
    if (csr & 0x80u)
        sie_wr(S_TXCSR1, csr & ~0x80u);
    while (ro_r != ro_w && n < 64u && (int32_t)(now - ro_t[ro_r % RTQ]) >= 0) {
        ep1tx[n] = 0x0Fu, ep1tx[n + 1u] = (uint8_t)ro_q[ro_r % RTQ], ep1tx[n + 2u] = ep1tx[n + 3u] = 0;
        n += 4u;
        ro_r++;
    }
    fm1_usb_ep_send(1, ep1tx, n);
    sie_wr(S_TXCSR1, sie_rd(S_TXCSR1) | 0x01u);
    usb.tx_pkts++;
}

/* ---- flash: read anywhere; erase / program only the update's staging area, the settings sector and (GR!MM) the
 * sample area: 0x97000..0xDF000, in what Felucca and its family keep their data in (0x97000..0xFBFFF: theirs, not
 * ours; a firmware finds its own magic missing and starts fresh), below the update's staging area (0xE0000). An
 * update writes the flash up to 0x93000 only: the samples stay. XIP reads it as plain data (fl_plain_window_init) */
#define SETTINGS_AT 0x000FC000u                 /* one 4 KiB sector of Felucca's globals area */
#define SAMPLE_AREA 0x00097000u                 /* GR!MM: the sample slots (samples.c) */
#define SAMPLE_AREA_LEN 0x00048000u             /* 288 KiB: up to 0xDF000 */
/* GR!MM's saves (store.c): six sectors where Felucca keeps data an app writes (its storage.c flash map: its preset banks'
 * last sector, its autosave pair and voice bank above the update's staging, its globals beside the settings), none in the
 * sample area */
#define SAVE_NSECT 6u
static const uint32_t SAVE_SECT[SAVE_NSECT] = {0xDF000u, 0xE5000u, 0xE6000u, 0xE7000u, 0xFD000u, 0xFE000u};
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || \
                             FL_IN(off, n, SAMPLE_AREA, SAMPLE_AREA + SAMPLE_AREA_LEN) || FL_IN(off, n, 0xDF000u, 0xE0000u) || \
                             FL_IN(off, n, 0xE5000u, 0xE8000u) || FL_IN(off, n, 0xFD000u, 0xFF000u))
#include "fm1_flash.h"
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

static uint8_t recovery_active;
#define OTA_IDENTITY (recovery_active ? "FM-1_000" : FELUCCA_ID)
#include "ota.c"
static void recovery_poll(void);
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    fm1_wdt_feed();
    if (recovery_active) recovery_poll();
}
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return st_read(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    if (recovery_active) return;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u)
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_WHITE, 1);
    else
        draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_WHITE, 1);
}
static void ota_commit(const uint8_t *parm)
{
    bootguard_clear(&bootguard);
    usb_detach();
    fm1_delay_ms(30);
    hl_boot.clean = 1;                          /* (rc11: our own restart: not an unexpected reset) */
    fm1_enter_update(parm);
}
#include "recovery.c"

/* ---- settings: [magic][length][crc32] then the bytes; settings_save erases the sector (~50 ms with
 * the interrupts off: the sound drops out), so save when nothing plays, not on every knob step */
#define SETTINGS_MAGIC 0x59474E44u              /* "DNGY" */
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static int settings_load(void *p, uint32_t n)   /* 0: loaded; else p is untouched */
{
    static uint8_t buf[256];
    uint32_t h[3];
    if (!flash_ok || n > sizeof buf || st_read(SETTINGS_AT, h, sizeof h) || h[0] != SETTINGS_MAGIC || h[1] != n)
        return -1;
    if (st_read(SETTINGS_AT + sizeof h, buf, n) || crc32(buf, n) != h[2])
        return -1;
    memcpy(p, buf, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)   /* p in RAM, n <= 256 */
{
    uint32_t h[3] = {SETTINGS_MAGIC, n, 0}, took;
    int rc;
    if (!flash_ok || n > 256u)
        return -1;
    h[2] = crc32(p, n);
    if ((rc = fl_erase4k(SETTINGS_AT, &took)) != 0)
        return rc;
    if ((rc = fl_write(SETTINGS_AT + sizeof h, p, n)) != 0)
        return rc;
    return fl_write(SETTINGS_AT, (const uint8_t *)h, sizeof h);   /* the header last: a cut save stays invalid */
}

/* ---- GR!MM: the sample area (samples.c) and SysEx for the app. An erase or a write stops everything (the
 * interrupts are off, ~50 ms an erase): only while nothing sounds */
static const uint8_t *sample_flash(void) { return flash_ok ? fm1_xip_ptr(SAMPLE_AREA) : 0; }   /* 0: no flash */
static int sample_flash_erase(uint32_t off)     /* the 4 KiB sector at off (in the area) */
{
    uint32_t took;
    if (!flash_ok || (off & 0xFFFu) || off >= SAMPLE_AREA_LEN)
        return -8;
    fm1_wdt_feed();                             /* (a slot is 18 erases, RESTORE 72: the watchdog fed between) */
    return fl_erase4k(SAMPLE_AREA + off, &took);
}
static int sample_flash_write(uint32_t off, const void *p, uint32_t n)   /* p in RAM; into erased flash */
{
    if (!flash_ok || off >= SAMPLE_AREA_LEN || n > SAMPLE_AREA_LEN - off)
        return -8;
    return fl_write(SAMPLE_AREA + off, (const uint8_t *)p, n);
}
/* GR!MM's saves (store.c): sector i of SAVE_SECT, read through XIP; an erase (~50 ms) or a write stops the audio a moment */
static const uint8_t *save_flash(uint32_t i) { return flash_ok && i < SAVE_NSECT ? fm1_xip_ptr(SAVE_SECT[i]) : 0; }
static int save_flash_erase(uint32_t i)
{
    uint32_t took;
    if (!flash_ok || i >= SAVE_NSECT) return -8;
    return fl_erase4k(SAVE_SECT[i], &took);
}
static int save_flash_write(uint32_t i, uint32_t off, const void *p, uint32_t n)   /* p in RAM; into erased flash */
{
    if (!flash_ok || i >= SAVE_NSECT || off >= 4096u || n > 4096u - off) return -8;
    return fl_write(SAVE_SECT[i] + off, (const uint8_t *)p, n);
}
/* a SysEx frame F0 7D .. F7 (7D: the non-commercial ID; the 7-bit bytes between F0 and F7) is the app's: it takes
 * it with sysex_get / sysex_done (from the main loop) and answers with sysex_send (F0 .. F7, 7-bit). The updater's
 * frames (00 59 ..) are the platform's as before. A frame of the app's left for a second goes: the updater's next
 * one gets through whatever the app does */
static uint32_t sx_app_t0;                      /* when the frame waiting for the app was first seen (0: none seen);
                                                 * (V2) 0 again as each is taken: requests back to back (a backup's
                                                 * ~1000 GETs) each get their own second, not the first one's */
static int sysex_get(const uint8_t **p, uint32_t *n) { return ota_frame_get(p, n) && *n && (*p)[0] == 0x7Du; }
static void sysex_done(void) { ota_frame_done(), sx_app_t0 = 0; }
static int sysex_send(const uint8_t *p, uint32_t n) { return ota_wire_send(p, n); }

/* ---- MIDI over USB: 4-byte USB-MIDI packets, cable 0 */
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)   /* 1: a message (status, data 1, data 2) */
{
    uint32_t pk;
    if (mi_r == mi_w)
        return 0;
    pk = midi_in_q[mi_r % MQ];
    mi_r++;
    *st = (uint8_t)(pk >> 8);
    *d1 = (uint8_t)(pk >> 16);
    *d2 = (uint8_t)(pk >> 24);
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2)   /* from the main loop only */
{
    midi_out_event((st >> 4) | st << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}
static int32_t batt_raw(void) { return fm1_adc_read(FM1_ADC_BATT); }   /* the battery divider (PB1, ADC ch3): raw
                                                 * 0..1023 (-1: no reading) */
static uint32_t usb_drops(void) { return uac.underruns + uac.overruns; }   /* (rc9) the USB recording's dropouts so far:
                                                 * a frame repeated (ring dry), a block dropped (full) */
#define HL_N 25u
static void hl_read(uint32_t *v)                /* (rc11) the CPU command's health values, in this order */
{
    v[0] = uac.underruns, v[1] = uac.overruns, v[2] = uac.missed, v[3] = uac.stalls;   /* USB audio: ring dry, ring full,
                                                 * no packet ready, the host stopped reading */
    v[4] = usb.resets, v[5] = usb.suspends, v[6] = usb.frame_stalls;   /* USB: bus resets (re-enumeration), suspends,
                                                 * the frame number frozen for 100 ms */
    v[7] = fm1_wdt_gap, v[8] = hl.gap, v[9] = hl.late, v[10] = hl.stall, v[11] = hl.lost, v[12] = hl.dur;
    v[13] = fl_irq_max, v[14] = fl_irq_slow;    /* flash: the longest the interrupts were off; the times past 1 ms */
    v[15] = hl.bat_lo, v[16] = hl.bat_hi;       /* the battery divider's lowest and highest reading (0 .. 1023) */
    v[17] = hl_boot.boots, v[18] = hl_boot.unexp, v[19] = hl_boot.wdt, v[20] = hl_boot.low;   /* starts since power-on;
                                                 * resets nobody asked for: all, the watchdog's, a supply that sagged */
    v[21] = hl_boot.why, v[22] = fm1_boot.p3_rst | hl_boot.kind << 8;   /* the last four P33 RST_SRC bytes; this start's, and
                                                 * its kind (above them) */
    v[23] = fm1_crash.magic == FM1_CRASH_MAGIC ? fm1_crash.count : 0u, v[24] = fm1_ms;   /* faults, uptime */
}
static void hl_clear(void)                      /* the peaks and counts since now (the starts stay) */
{
    hl.gap = hl.late = hl.stall = hl.lost = hl.dur = hl.bat_hi = 0, hl.bat_lo = 1023u;
    fm1_wdt_gap = fl_irq_max = fl_irq_slow = 0;
}
static int usb_recording(void) { return usb.config && !usb.suspended && uac.alt && uac.flowing; }   /* (rc11) a computer
                                                 * reads the USB audio now (a DAW with the FM-1 as its input, recording
                                                 * or not): the stream goes on all the time */
static int usb_powered(void) { return usb.config && !usb.suspended; }   /* a USB host has it (and powers it); there is
                                                 * no charger line: a charger alone is not seen */
static uint32_t midi_room(void)                 /* messages midi_send can still queue (0: one more would be dropped;
                                                 * no host: they go nowhere, all the room there is) */
{
    return usb.config ? MQ - (mo_w - mo_r) : MQ;
}

/* ---- the timer: 10 kHz key scan, milliseconds, USB at 2 kHz. It outranks the audio ISR; nested in a
 * render it only scans, counts and feeds the USB audio stream (as sloopDX main.c); GR!MM V2: and the MIDI clock's own
 * out (rt_flush), the TRS jack (2 kHz) and, for CLOCK USB, USB's MIDI in (2 kHz): a pulse's time within 0.5 ms */
static volatile uint8_t in_audio;
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u, d;
    fm1_timer5_ack();
    fm1_input_tick();
    if (last) {                                 /* (rc11: the gap since the tick before) */
        d = t0 - last;
        if (d > hl.gap) hl.gap = d;
        hl.late += d > 200u * FM1_TICKS_PER_US;
        hl.stall += d > 1000u * FM1_TICKS_PER_US;
        hl.lost += d > 10000u * FM1_TICKS_PER_US;
    }
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (usb_due)
        owed |= 1u;
    if (++sub == 10u)
        sub = 0;
    if (in_audio) {
        if (usb_due) {
            uac_service();
            if (rt_usb_fast && usb.config)
                ep1_rx();                       /* (one endpoint, its own SIE index: as uac_service) */
        }
        if (sub % 5u == 3u)
            uart_midi_poll();
        rt_flush();
        d = fm1_ticks() - t0;
        isr_in_audio += d;
        if (d > hl.dur) hl.dur = d;
        return;
    }
    rt_flush();
    if (owed & 1u)
        usb_poll();
    if (sub % 5u == 3u)
        uart_midi_poll();                       /* (2 kHz: ~7 bytes a call at most, 31250 baud) */
    owed = 0;
    d = fm1_ticks() - t0;
    if (d > hl.dur) hl.dur = d;
}
extern void isr_timer5(void);

/* ---- the sound: ALNK0 I2S, two halves; the app renders (V2: and scales by the MASTER knob, fm1_master: its output
 * stage, straight into the 24-bit words: Q15 << 7, the -6 dBFS ceiling as before; USB audio a 16-bit copy) */
static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
static volatile int32_t master_q12 = 2048;
static int32_t fm1_master(void) { return master_q12; }   /* the MASTER knob, Q12, squared (0 .. 4092) */
static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    in_audio = 1;
    p = fm1_audio_pending();
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        int32_t *o = &abuf[fm1_audio_free_half() * HALF_WORDS];
        uint32_t i, b;
        for (i = 0; i < HALF_WORDS; i++)
            o[i] = 0;
        uac_render_start();
        app_audio(o, HALF_FRAMES);
        for (i = 0; i < HALF_WORDS; i++)        /* (V2: MASTER applied by the app; its 24-bit words, kept in range: the
                                                 * limit's sign from the word's, -OUT_FS a constant the ROM check flags) */
            if ((o[i] < 0 ? -o[i] : o[i]) > OUT_FS) o[i] = (o[i] >> 31) ^ OUT_FS;
        for (b = 0; uac.feed && b < HALF_FRAMES; b += 32u) {   /* the computer records what the speaker plays (16 bit,
                                                 * rounded; not recording: nothing to make) */
            int32_t u[64];
            out_usb16(o + 2u * b, u, 64u);
            uac_tap(u, 32u);
        }
        fm1_audio_ack_half();
    }
    in_audio = 0;
}
extern void isr_alnk0(void);

/* ---- the main loop's duties: watchdog, USB, the installer's update request, the MASTER knob */
static void platform_poll(void)
{
    static int32_t knob_avg = 512 * 16;
    int32_t a;
    fm1_wdt_feed();
    usb_retry(fm1_ms);
    {                                           /* (rc11: the battery divider ~100 times a second: a supply that sags) */
        static uint32_t bt;
        if (fm1_ms - bt >= 10u) {
            bt = fm1_ms;
            if ((a = batt_raw()) >= 0) {
                if ((uint32_t)a < hl.bat_lo) hl.bat_lo = (uint32_t)a;
                if ((uint32_t)a > hl.bat_hi) hl.bat_hi = (uint32_t)a;
            }
        }
    }
    {   /* the updater's frames to ota_service; an F0 7D frame waits for the app (sysex_get), a second at most */
        const uint8_t *fp;
        uint32_t fn;
        if (sysex_get(&fp, &fn)) {
            if (!sx_app_t0)
                sx_app_t0 = fm1_ms | 1u;
            else if (fm1_ms - sx_app_t0 > 1000u)
                sysex_done();
        } else {
            sx_app_t0 = 0;
            ota_service();
        }
    }
    if (usb.ota_req) {
        usb.ota_req = 0;
        if (flash_ok)
            ota_session();                      /* returns only if nothing was committed */
    }
    if (usb.uboot_req) {
        fm1_audio_stop();
        lcd_fill(0, 0, 240, 240, C_BLACK);
        draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
        usb_detach();
        fm1_delay_ms(30);
        bootguard.pending = 0;
        hl_boot.clean = 1;
        fm1_enter_uboot();
    }
    if (fm1_ms >= 30000u && bootguard.pending)
        bootguard_clear(&bootguard);            /* 30 s up: this firmware boots */
    if ((a = fm1_adc_read(FM1_ADC_MASTER)) >= 0) {
        uint32_t k10;
        knob_avg += (a * 16 - knob_avg) / 8;
        k10 = (uint32_t)(knob_avg / 16);
        master_q12 = (int32_t)((k10 * k10) >> 8);
    }
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t i, t0, v[3] = {c->vec, c->pc, c->rets};
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 20, 240, &FONT_S, APP_NAME " CRASH", C_WHITE, 1);
    for (i = 0; i < 3u; i++) {
        uint32_t k;
        for (k = 0; k < 8u; k++) b[k] = "0123456789ABCDEF"[(v[i] >> (28u - 4u * k)) & 15u];
        b[8] = 0;
        draw_text_box(10, 60 + i * 20, 220, &FONT_S, b, C_WHITE, 0);
    }
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 8000u * 1000u * FM1_TICKS_PER_US)
        fm1_wdt_feed();
    hl_boot.clean = 1;                          /* (the fault is counted: fm1_crash) */
    fm1_reboot();
}

void fm1_cstart(void)
{
    extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
    extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];
    uint32_t *s, *d, p3, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    {                                           /* (rc11) how it started: RAM kept and no restart of ours before it: the
                                                 * watchdog (P33 bit 2), else a supply that sagged (bits 1, 3, 5) */
        uint32_t fresh = hl_boot.magic != HL_MAGIC, bad;
        if (fresh)
            hl_boot.magic = HL_MAGIC, hl_boot.boots = hl_boot.unexp = hl_boot.wdt = hl_boot.low = hl_boot.why = hl_boot.clean = 0;
        bad = !fresh && !hl_boot.clean;
        hl_boot.clean = 0;
        hl_boot.kind = bad ? ((p3 & 4u) ? 1u : (p3 & 0x2Au) ? 2u : 0u) : 0u;
        hl_boot.boots++, hl_boot.unexp += bad, hl_boot.wdt += hl_boot.kind == 1u, hl_boot.low += hl_boot.kind == 2u;
        hl_boot.why = hl_boot.why << 8 | p3;
    }
    fm1_wdt_arm(0x0D);
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);
    boot_mode = bootguard_begin(&bootguard);
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++) *d = 0;
    for (d = _pool_start; d < _pool_end; d++) *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++) *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++) *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                        /* OCT- at power-on: the USB rescue */
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    if (flash_ok)
        fl_plain_window_init();                 /* GR!MM: the flash above 0x93000 (the samples) reads plain through XIP */
    ota_boot_cleanup();
    lcd_init();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    fm1_input_init();
    fm1_adc_init();
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);   /* (abuf is zero: .bss) */
    usb_start();
    uart_midi_init();                           /* (GR!MM V2: the TRS jack; before TIMER5: port H's RMW) */
    fm1_timer5_start(isr_timer5, 4);
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    app_main();
    for (;;)
        platform_poll();
}
#endif
