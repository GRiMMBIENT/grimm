/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (C) 2026 GR!MM
 * The design (the wasm in an AudioWorklet, the device clock driven by the audio the worklet asks for, the screen,
 * the lights and the saved bytes read back through exports) follows Felucca's browser build, web/emu/felucca_web.c
 * of hugelton/Felucca (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0), which
 * follows X0X's, web/emu/x0x_web.c of charlesvestal/fm1-x0x (GPL-3.0). */
/* GR!MM in a browser: a fake FM-1, as tests/host.c is one for a computer. platform.c includes this file instead of
 * the hardware when GRIMM_WEB is set; the app (firmware/src/grimm.c) runs unchanged on it.
 *
 *   web/build_web.sh   ->  web/dist/{index.html, panel.js, worklet.js, grimm.wasm}
 *
 * One call of platform_poll() is one millisecond of the device, as in host mode: the audio halves due by then
 * (app_audio, then the MASTER knob as platform.c applies it), into a ring the worklet drains. The app's main loop
 * never returns, so once the worklet has the frames it asked for, platform_poll() calls web_yield(): build_web.sh
 * runs Binaryen's Asyncify over the wasm, and web_yield (worklet.js) unwinds app_main back to the worklet and
 * rewinds into it for the next audio block. fm1_ticks() is the browser's clock (web_ticks): the app's CPU reading
 * is the browser's, not the FM-1's, and in Chromium's AudioWorklet (no performance.now) it is Date.now, in whole
 * milliseconds: a block reads 0 or 1 ms, so NOW comes out right on average but PEAK is one such step (17.2 %).
 * Not here: USB (MIDI out goes nowhere), updates, the rescue, crashes. */
#include <stdint.h>
#include <string.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

#define EXPORT __attribute__((used, visibility("default")))
extern void web_yield(void) __attribute__((import_module("env"), import_name("web_yield")));
extern uint32_t web_ticks(void) __attribute__((import_module("env"), import_name("web_ticks")));

static volatile uint32_t fm1_ms;
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void) { return web_ticks(); }   /* 24 MHz, as the FM-1's TIMER4: here the browser's clock */

/* ---- the panel: keys (bit k = key k, 0 = F3), buttons (bit = BTN_ id), encoders (ENC_ id), LEDs (fm1_led_key ids:
 * 0..13 the buttons, 14 + k the keys) */
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t web_pressed, web_released, web_notes_pressed, web_led_btn, web_led_key;
static uint32_t web_key_mask, web_key_tap;     /* the keys held; pressed and let go since the last poll */
static int32_t web_enc_steps[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released) *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % 7u]; web_enc_steps[e % 7u] = 0; return s; }
static void fm1_led_key(uint32_t id, int on)
{
    uint32_t *m = id < 14u ? &web_led_btn : &web_led_key, bit = id < 14u ? id : id - 14u;
    if (id >= 14u + 27u) return;
    if (on) *m |= 1u << bit;
    else *m &= ~(1u << bit);
}

static void fm1_led_glow(uint32_t id, int on) { (void)id; (void)on; }   /* (the glow: not shown here) */

/* ---- the screen: 240 x 240 RGB565, as host.c keeps it (gfx.c sends blits byte-swapped, as the ST7789 wants) */
static uint16_t fb[240 * 240];
static uint32_t web_draws;
static void lcd_sync(void) {}
static void lcd_light(int on) { (void)on; }     /* (SCREEN OFF: the page's screen stays lit) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
    web_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
    web_draws++;
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

/* ---- settings: up to 256 bytes, kept by the page (localStorage) and handed back before boot; as platform.c, a
 * load only succeeds with the length that was saved */
static uint8_t web_set[256];
static int32_t web_set_len = -1;
static uint32_t web_set_writes;
static int settings_load(void *p, uint32_t n)
{
    if (web_set_len < 0 || (uint32_t)web_set_len != n || n > sizeof web_set) return -1;
    memcpy(p, web_set, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    if (n > sizeof web_set) return -1;
    memcpy(web_set, p, n);
    web_set_len = (int32_t)n;
    web_set_writes++;
    return 0;
}

/* ---- MIDI: in from Web MIDI (web_midi), out nowhere */
static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }
static uint32_t midi_room(void) { return 64u; }
static int32_t batt_raw(void) { return -1; }   /* (no battery in a browser: no icon) */
static int usb_powered(void) { return 0; }

/* GR!MM: the sample area (platform.c's, 288 KiB), here RAM, erased as a new FM-1's: at boot the firmware writes its four
 * factory samples in (samples.c), as on the device */
#define SAMPLE_AREA_LEN 0x48000u
static uint8_t fake_flash[SAMPLE_AREA_LEN];
static const uint8_t *sample_flash(void) { return fake_flash; }
static int sample_flash_erase(uint32_t off)
{
    uint32_t i;
    if ((off & 0xFFFu) || off >= SAMPLE_AREA_LEN) return -8;
    for (i = 0; i < 4096u; i++) fake_flash[off + i] = 0xFF;
    return 0;
}
static int sample_flash_write(uint32_t off, const void *p, uint32_t n)   /* NOR: a write only clears bits */
{
    const uint8_t *s = p;
    uint32_t i;
    if (off >= SAMPLE_AREA_LEN || n > SAMPLE_AREA_LEN - off) return -8;
    for (i = 0; i < n; i++) fake_flash[off + i] &= s[i];
    return 0;
}
/* GR!MM: the save sectors (store.c), here RAM, erased at the start: nothing saved */
#define SAVE_NSECT 6u
static uint8_t fake_save[SAVE_NSECT][4096];
static const uint8_t *save_flash(uint32_t i)
{
    static int init;
    if (!init) { memset(fake_save, 0xFF, sizeof fake_save); init = 1; }
    return i < SAVE_NSECT ? fake_save[i] : 0;
}
static int save_flash_erase(uint32_t i)
{
    if (i >= SAVE_NSECT) return -8;
    (void)save_flash(0);
    memset(fake_save[i], 0xFF, 4096u);
    return 0;
}
static int save_flash_write(uint32_t i, uint32_t off, const void *p, uint32_t n)   /* NOR: a write only clears bits */
{
    const uint8_t *s = p;
    uint32_t k;
    if (i >= SAVE_NSECT || off >= 4096u || n > 4096u - off) return -8;
    (void)save_flash(0);
    for (k = 0; k < n; k++) fake_save[i][off + k] &= s[k];
    return 0;
}
/* SysEx: the editor page's frames in (as USB would bring them: 7D .. without F0 / F7), the replies out (F0 .. F7) */
static uint8_t web_sx_in[640], web_sx_out[4096];
static uint32_t web_sx_in_n, web_sx_in_ready, web_sx_out_n;
static int sysex_get(const uint8_t **p, uint32_t *n)
{
    if (!web_sx_in_ready) return 0;
    if (!web_sx_in_n || web_sx_in[0] != 0x7Du) { web_sx_in_ready = 0; return 0; }
    *p = web_sx_in, *n = web_sx_in_n;
    return 1;
}
static void sysex_done(void) { web_sx_in_ready = 0; }
static int sysex_send(const uint8_t *p, uint32_t n)
{
    if (web_sx_out_n + n > sizeof web_sx_out) return -1;
    memcpy(web_sx_out + web_sx_out_n, p, n);
    web_sx_out_n += n;
    return 0;
}

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* ---- the sound: a ring of stereo floats, filled by platform_poll, drained by web_take */
#define RING 4096u
static float ring[RING * 2u];
static uint32_t ring_w, ring_r, web_want = 128, web_owed;
static int32_t web_knob = 700, web_knob_avg = 512 * 16, web_master_q12 = 2048;   /* MASTER: 0..1023, as the ADC */

static void platform_poll(void)                 /* one call = one millisecond */
{
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t i, k10;
    fm1_in.notes = web_key_mask | web_key_tap;  /* a key tapped between two polls is down for this one (the FM-1 scans
                                                 * at 10 kHz: no press is shorter than its millisecond there) */
    web_key_tap = 0;
    fm1_ms++;
    web_knob_avg += (web_knob * 16 - web_knob_avg) / 8;    /* platform.c: the knob, smoothed, squared */
    k10 = (uint32_t)(web_knob_avg / 16);
    web_master_q12 = (int32_t)((k10 * k10) >> 8);
    web_owed += FS;                             /* the frames this millisecond is worth, in halves as the ISR renders */
    while (web_owed >= 1000u * HALF_FRAMES) {
        web_owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = (blk[i] * web_master_q12) >> 12;
            v = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
            ring[(ring_w * 2u + i) % (RING * 2u)] = (float)v / 65536.0f;   /* << 7 into 24 bits: -6 dBFS, as the I2S */
        }
        ring_w += HALF_FRAMES;
    }
    if (ring_w - ring_r >= web_want)
        web_yield();                            /* back to the worklet; the next block resumes here */
}

/* ---- the exports (worklet.js) */
static uint8_t web_async[16384] __attribute__((aligned(8)));   /* Asyncify's saved stack */
EXPORT uint8_t *web_async_buf(void) { return web_async; }
EXPORT uint32_t web_async_size(void) { return sizeof web_async; }
EXPORT void web_run(void)                       /* never returns; unwinds at web_yield, rewound into by the worklet */
{
    memset(fake_flash, 0xFF, sizeof fake_flash);   /* (erased, as a new FM-1's: the factory samples go in at boot; before
                                                 * the app: it is not rewound into) */
    app_main();
}

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT void web_want_frames(uint32_t n) { web_want = n > 1024u ? 1024u : n; }
EXPORT uint32_t web_ready(void) { return ring_w - ring_r >= web_want; }
EXPORT uint32_t web_avail(void) { return ring_w - ring_r; }   /* frames in the ring (web/test_web.mjs) */
EXPORT void web_take(void)                     /* web_want frames out of the ring into out_l / out_r */
{
    uint32_t k;
    for (k = 0; k < web_want; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1u];
    }
}

EXPORT void web_keys(uint32_t mask)
{
    mask &= (1u << 27) - 1u;
    web_notes_pressed |= mask & ~web_key_mask;
    web_key_tap |= mask & ~web_key_mask;
    web_key_mask = mask;
    fm1_in.notes = mask | web_key_tap;
}
EXPORT void web_buttons(uint32_t mask)
{
    mask &= (1u << 14) - 1u;
    web_pressed |= mask & ~fm1_in.buttons;
    web_released |= fm1_in.buttons & ~mask;
    fm1_in.buttons = mask;
}
EXPORT void web_enc(uint32_t e, int32_t n) { if (e < 7u) web_enc_steps[e] += n; }
EXPORT void web_master(uint32_t v) { web_knob = (int32_t)(v > 1023u ? 1023u : v); }
EXPORT void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st < 0x80u || st >= 0xF0u || mq_w - mq_r >= 64u) return;
    mq[mq_w % 64u][0] = (uint8_t)st; mq[mq_w % 64u][1] = (uint8_t)(d1 & 127u); mq[mq_w % 64u][2] = (uint8_t)(d2 & 127u);
    mq_w++;
}

EXPORT uint16_t *web_fb(void) { return fb; }
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
EXPORT uint32_t web_lit_buttons(void) { return web_led_btn; }
EXPORT uint32_t web_lit_keys(void) { return web_led_key; }
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }

EXPORT uint8_t *web_settings(void) { return web_set; }
EXPORT int32_t web_settings_len(void) { return web_set_len; }
EXPORT void web_settings_set_len(int32_t n) { web_set_len = n < 0 ? -1 : n > 256 ? 256 : n; }
EXPORT uint32_t web_settings_writes(void) { return web_set_writes; }

EXPORT uint8_t *web_sysex_in_buf(void) { return web_sx_in; }   /* (web/editor_core.js: the virtual FM-1) */
EXPORT void web_sysex_in(uint32_t n) { web_sx_in_n = n > sizeof web_sx_in ? sizeof web_sx_in : n; web_sx_in_ready = 1; }
EXPORT uint32_t web_sysex_busy(void) { return web_sx_in_ready; }
EXPORT uint8_t *web_sysex_out_buf(void) { return web_sx_out; }
EXPORT uint32_t web_sysex_out_len(void) { return web_sx_out_n; }
EXPORT void web_sysex_out_clear(void) { web_sx_out_n = 0; }
