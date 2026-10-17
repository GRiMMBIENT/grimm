/* SPDX-License-Identifier: GPL-3.0-only */
/* GR!MM changes (file names): Copyright (C) 2026 GR!MM */
/* A fake FM-1 on a computer: the app (firmware/src/grimm.c) runs unchanged on it. platform.c includes
 * this file instead of the hardware when DINGHY_HOST is set. A script presses keys over time; out come
 * the screen (PPM, at the times SHOT asks) and the sound (WAV, as the FM-1 sends it over USB). fm1_ticks() is this
 * computer's clock: the longest app_audio() goes out at the end, as a share of its 5.8 ms (the FM-1's is its own).
 *
 *   mkdir -p build/host
 *   cc -O2 -Wall -DDINGHY_HOST -Ifirmware/src -Ifirmware/gen -o build/host/grimm firmware/src/grimm.c
 *   build/host/grimm build/host/grimm        (-> build/host/grimm.wav, build/host/grimm-<ms>.ppm)
 *   build/host/grimm OUT script.txt [FPS]    (your own script; with FPS: OUT-NNNNN.ppm, a frame every 1/FPS s:
 *                                             a film of your firmware, e.g. ffmpeg -r 30 -i OUT-%05d.ppm ...)
 *
 * Change SCRIPT below to try your own app. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef APP_NAME
#define APP_NAME "DINGHY"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u

static volatile uint32_t fm1_ms;
static struct { uint32_t notes, buttons; } fm1_in;
static uint32_t host_edges, host_rel, host_tap;   /* buttons pressed, let go since the last call; tapped (up next ms) */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = host_edges;
    *released = host_rel;
    host_edges = host_rel = 0;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }   /* (the script sets fm1_in.notes) */
static int32_t host_enc[7];
static int32_t fm1_enc_take(uint32_t e) { int32_t n = host_enc[e % 7u]; host_enc[e % 7u] = 0; return n; }
#define FM1_TICKS_PER_US 24u
static uint32_t fm1_ticks(void)                 /* 24 MHz, as the FM-1's TIMER4: here the computer's clock */
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 24000000u + (uint32_t)ts.tv_nsec / 1000u * 24u;
}
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static void fm1_led_glow(uint32_t id, int on) { (void)id; (void)on; }

/* the screen: 240 x 240 RGB565, as the ST7789 gets it (gfx.c sends the bytes swapped) */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static uint32_t host_dark;                      /* (SCREEN OFF: the backlight off) */
static void lcd_light(int on) { host_dark = !on; }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
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
}
#define CV_MAX (240u * 64u)
#include "gfx.c"

static uint8_t host_store[256 + 4];
static int settings_load(void *p, uint32_t n)
{
    if (host_store[0] != 1 || n > 256u) return -1;
    memcpy(p, host_store + 4, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)
{
    host_store[0] = 1;
    memcpy(host_store + 4, p, n);
    printf("%6u ms  settings saved (%u B)\n", fm1_ms, n);
    return 0;
}

static uint8_t mq[64][3];
static uint32_t mq_w, mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    if (mq_r == mq_w) return 0;
    *st = mq[mq_r % 64u][0]; *d1 = mq[mq_r % 64u][1]; *d2 = mq[mq_r % 64u][2];
    mq_r++;
    return 1;
}
#ifdef GRIMM_STRESS
static void stress_midi_out(uint32_t st, uint32_t d1);   /* (tests/stress_check.c: what is on out there) */
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)d2; stress_midi_out(st, d1); }
#else
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { printf("%6u ms  MIDI out %02X %u %u\n", fm1_ms, st, d1, d2); }
#endif
static uint32_t midi_room(void) { return 64u; }
#ifdef GRIMM_STRESS                             /* (the stress test: a battery going round its levels, 4 s a step) */
static int32_t batt_raw(void) { return 500 + (int32_t)((fm1_ms / 4000u * 13u) % 110u); }
static int usb_powered(void) { return (fm1_ms / 60000u) % 3u == 2u; }
#else                                           /* (no battery here: no icon) */
static int32_t batt_raw(void) { return -1; }
static int usb_powered(void) { return 0; }
#endif

/* GR!MM: the sample area (platform.c's, 288 KiB), here RAM, erased as a new FM-1's: at boot the firmware writes its four
 * factory samples in (samples.c), as on the device; no USB, so no SysEx (no uploads) */
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
#ifdef GRIMM_STRESS                             /* (the stress test: a frame put in host_sx as from USB; the replies) */
static uint8_t host_sx[640];
static uint32_t host_sx_n, host_sx_ready;
static void stress_sysex_out(const uint8_t *p, uint32_t n);
static int sysex_get(const uint8_t **p, uint32_t *n)
{
    if (!host_sx_ready || !host_sx_n || host_sx[0] != 0x7Du) return 0;
    *p = host_sx, *n = host_sx_n;
    return 1;
}
static void sysex_done(void) { host_sx_ready = 0; }
static int sysex_send(const uint8_t *p, uint32_t n) { stress_sysex_out(p, n); return 0; }
#else
static int sysex_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void sysex_done(void) {}
static int sysex_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }
#endif

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

/* the script: at t ms, keys down (bit k = key k, 0 = F3), a button edge, a MIDI message, an encoder turned. A script
 * file (the second argument) replaces it, a line each: "T keys MASK" (hex or decimal), "T btn ID" (a tap: let go the
 * next millisecond), "T down ID" / "T up ID" (held, let go), "T midi ST D1 D2", "T enc ID STEPS" (ENC_ id, + =
 * clockwise), "T end" */
typedef struct { uint32_t t, keys; int btn; uint8_t st, d1, d2; int enc, steps; } host_ev_t;
#define CHORD1 (1u << 7 | 1u << 11 | 1u << 14)                  /* C E G (C4 .. G4 at OCTAVE +0) */
#define CHORD2 (1u << 2 | 1u << 5 | 1u << 9)                   /* G Bb D (G3 .. D4) */
#define DOWN 0x100                                              /* btn: held from now (a btn alone: a tap, 1 ms) */
#define UP 0x200                                                /* .. let go */
static const host_ev_t SCRIPT0[] = {
    /* T1: a VA chord sent to GRAIN; T2: a SAMPLE chord (slot 1: URCHIN, a factory sample) sent to DELAY; TAPE over it all at
     * moderate settings; FREEZE once; then the notes let go (the frozen cloud and the tails go on) */
    {100, 0, BTN_HOME, 0, 0, 0, 0, 0},                          /* HOME: MIXER 1 (T1) */
    {110, 0, -1, 0, 0, 0, ENC_KNOB4, 100},                      /* GRAIN SEND 0 -> 100 */
    {120, 0, BTN_FX, 0, 0, 0, 0, 0},                            /* FX: TAPE */
    {130, 0, -1, 0, 0, 0, ENC_KNOB1, 40},                       /* WOW 31 % */
    {140, 0, -1, 0, 0, 0, ENC_KNOB2, 50},                       /* SATURATION 39 % */
    {150, 0, -1, 0, 0, 0, ENC_KNOB3, 60},                       /* TONE 47 % */
    {160, 0, -1, 0, 0, 0, ENC_KNOB4, 30},                       /* HISS 24 % */
    {170, 0, BTN_FX, 0, 0, 0, 0, 0},                            /* FX: GRAIN */
    {180, 0, -1, 0, 0, 0, ENC_KNOB3, 30},                       /* SCATTER 30 -> 60 */
    {500, CHORD1, -1, 0, 0, 0, 0, 0},                           /* T1: C4 E4 G4 (VA) */
    {600, 0, -1, 0, 0, 0, 0, 0},
    {700, 0, -1, 0, 0, 0, ENC_ALGORITHM, 1},                    /* ALGORITHM: T2 */
    {710, 0, BTN_EDIT, 0, 0, 0, 0, 0},                          /* EDIT: SOUND (VA) */
    {720, 0, BTN_EDIT, 0, 0, 0, 0, 0},                          /* EDIT: SOURCE */
    {730, 0, -1, 0, 0, 0, ENC_KNOB1, 2},                        /* SOURCE: VA -> SAMPLE */
    {740, 0, BTN_EDIT, 0, 0, 0, 0, 0},                          /* EDIT: SOUND (SAMPLE: SLOT 1, the whole sample) */
    {750, 0, BTN_HOME, 0, 0, 0, 0, 0},                          /* HOME */
    {760, 0, BTN_HOME, 0, 0, 0, 0, 0},                          /* HOME: MIXER 1 (T2) */
    {770, 0, -1, 0, 0, 0, ENC_KNOB2, 80},                       /* DLY SEND 0 -> 80 */
    {800, CHORD2, -1, 0, 0, 0, 0, 0},                           /* T2: G3 Bb3 D4 (SAMPLE) */
    {900, 0, -1, 0, 0, 0, 0, 0},
    {2000, 0, BTN_HOME, 0, 0, 0, 0, 0},                         /* HOME: MIXER 2 */
    {2010, 0, BTN_HOME, 0, 0, 0, 0, 0},                         /* HOME: HOME */
    {3000, 0, BTN_FX, 0, 0, 0, 0, 0},                           /* FX: TAPE */
    {4000, 0, BTN_FX, 0, 0, 0, 0, 0},                           /* FX: GRAIN */
    {5000, 0, DOWN | BTN_FX, 0, 0, 0, 0, 0},                    /* FX held .. */
    {5010, 1u, -1, 0, 0, 0, 0, 0},                              /* .. + F3: FREEZE */
    {5020, 0, -1, 0, 0, 0, 0, 0},
    {5030, 0, UP | BTN_FX, 0, 0, 0, 0, 0},                      /* (FX let go: no page change) */
    {7000, CHORD2, -1, 0, 0, 0, 0, 0},                          /* let go: T2's notes, */
    {7100, 0, -1, 0, 0, 0, 0, 0},
    {7200, 0, -1, 0, 0, 0, ENC_ALGORITHM, -1},                  /* .. T1's */
    {7300, CHORD1, -1, 0, 0, 0, 0, 0},
    {7400, 0, -1, 0, 0, 0, 0, 0},
};
static const host_ev_t *script = SCRIPT0;
static uint32_t script_n = sizeof SCRIPT0 / sizeof SCRIPT0[0], end_ms = 16000, frame_fps;
static const uint32_t SHOT[] = {2500, 3500, 6000, 15500};   /* HOME; TAPE; GRAIN, FREEZE; the notes let go */

static void read_script(const char *path)
{
    static host_ev_t ev[4096];
    char line[256], what[16];
    FILE *f = fopen(path, "r");
    uint32_t n = 0;
    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f) && n < 4096u) {
        host_ev_t e = {0, 0, -1, 0, 0, 0, 0, 0};
        unsigned a = 0, b = 0, c = 0;
        if (line[0] == '#' || sscanf(line, "%u %15s", &e.t, what) != 2) continue;
        if (!strcmp(what, "end")) { end_ms = e.t; continue; }
        if (!strcmp(what, "keys")) { sscanf(line, "%*u %*s %i", (int *)&e.keys); }
        else if (!strcmp(what, "btn")) { sscanf(line, "%*u %*s %d", &e.btn); }
        else if (!strcmp(what, "down")) { sscanf(line, "%*u %*s %d", &e.btn); e.btn |= 0x100; }
        else if (!strcmp(what, "up")) { sscanf(line, "%*u %*s %d", &e.btn); e.btn |= 0x200; }
        else if (!strcmp(what, "enc")) { sscanf(line, "%*u %*s %d %d", &e.enc, &e.steps); }
        else if (!strcmp(what, "midi")) { sscanf(line, "%*u %*s %i %u %u", (int *)&a, &b, &c); e.st = (uint8_t)a; e.d1 = (uint8_t)b; e.d2 = (uint8_t)c; }
        else continue;
        ev[n++] = e;
    }
    fclose(f);
    script = ev;
    script_n = n;
}

static const char *host_out = "build/host/grimm";
static int16_t *wav;
static uint32_t wav_n;

static void write_ppm_as(const char *name)
{
    FILE *f;
    uint32_t i;
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fprintf(f, "P6 240 240 255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}
static uint32_t host_cpu_max, host_cpu_now;     /* app_audio(): the longest, the last 86 blocks' average (ticks) */
static void write_ppm(uint32_t t)
{
    char name[512];
    snprintf(name, sizeof name, "%s-%u.ppm", host_out, t);
    write_ppm_as(name);
    printf("%6u ms  screen -> %s   (app_audio() now %u.%02u us a block, %u.%03u%%, this computer)\n", t, name,
           host_cpu_now / 24u, host_cpu_now * 100u / 24u % 100u, host_cpu_now * 100u / 139320u,
           host_cpu_now * 100000u / 139320u % 1000u);
}

static void write_wav(void)
{
    char name[512];
    FILE *f;
    uint32_t bytes = wav_n * 4u, v[] = {16, 0x00020001u, FS, FS * 4u, 0x00100004u};
    snprintf(name, sizeof name, "%s.wav", host_out);
    if (!(f = fopen(name, "wb"))) { perror(name); exit(1); }
    fwrite("RIFF", 1, 4, f); bytes += 36; fwrite(&bytes, 4, 1, f); bytes -= 36;
    fwrite("WAVEfmt ", 1, 8, f); fwrite(v, 4, 5, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(wav, 4, wav_n, f);
    fclose(f);
    printf("sound  -> %s (%u frames)\n", name, wav_n);
    printf("cpu    app_audio() at most %u us of 5805 (%u%%, this computer)\n", host_cpu_max / FM1_TICKS_PER_US,
           (host_cpu_max * 100u + 69660u) / 139320u);
}

#ifdef HOST_MATRIX
/* the stress test: the FM-1's matrix (firmware/hal/fm1_matrix.h). The script's keys and buttons are what the hands
 * hold (hm_); the matrix reads them, the encoders' contacts resting closed (KNOB1 A, KNOB3 A, ALGORITHM B: they join
 * rows 0 and 1 to columns 3, 6 and 8) and every ghost they make; the input layer's test takes the ghosts out, as on the
 * FM-1 (fm1_input.h fm1__keys): what is left is fm1_in, its edges the app's */
#include "../firmware/hal/fm1_matrix.h"
static uint32_t hm_notes, hm_buttons, hm_bad;   /* (hm_bad: a press taken that no hand made, its id + 1) */
static uint32_t fm1_ghosts;                     /* (the presses taken as ghosts) */
static int hm_down(uint32_t id, uint32_t n, uint32_t b) { return id >= 14u ? (int)(n >> (id - 14u) & 1u) : (int)(b >> id & 1u); }
static void hm_set(uint32_t id, int on)
{
    uint32_t *m = id >= 14u ? &fm1_in.notes : &fm1_in.buttons, bit = 1u << (id >= 14u ? id - 14u : id);
    *m = on ? *m | bit : *m & ~bit;
}
static void host_matrix(void)
{
    static const uint8_t ENCC[FM1_NCOL] = {0, 0, 0, 1, 0, 0, 1, 0, 2, 0, 0};
    uint8_t phys[FM1_NCOL], rd[FM1_NCOL];
    uint32_t r, c, b0 = fm1_in.buttons;
    for (c = 0; c < FM1_NCOL; c++) {
        phys[c] = ENCC[c];
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] >= 0 && hm_down((uint32_t)FM1_KEYMAP[r][c], hm_notes, hm_buttons)) phys[c] |= 1u << r;
    }
    for (c = 0; c < FM1_NCOL; c++) {            /* what the matrix reads: held, or a ghost */
        rd[c] = phys[c];
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] >= 0 && fm1_ghost(phys, r, c)) rd[c] |= 1u << r;
    }
    for (c = 0; c < FM1_NCOL; c++)              /* the input layer, column by column, as on the FM-1 */
        for (r = 1; r < 5u; r++) {
            int32_t id = FM1_KEYMAP[r][c];
            if (id < 0) continue;
            if (!(rd[c] >> r & 1u)) hm_set((uint32_t)id, 0);
            else if (!hm_down((uint32_t)id, fm1_in.notes, fm1_in.buttons)) {
                if (fm1_ghost(rd, r, c)) fm1_ghosts++;
                else {
                    hm_set((uint32_t)id, 1);
                    if (!hm_down((uint32_t)id, hm_notes, hm_buttons)) hm_bad = (uint32_t)id + 1u;
                }
            }
        }
    host_edges |= fm1_in.buttons & ~b0, host_rel |= b0 & ~fm1_in.buttons;
}
#endif

static void platform_poll(void)                 /* one call = one millisecond */
{
    static uint32_t step, shot, owed, frame;
    static int32_t blk[2u * HALF_FRAMES];
#ifdef HOST_MATRIX
    uint32_t acc_n = fm1_in.notes, acc_b = fm1_in.buttons, e0 = host_edges, r0 = host_rel;
    fm1_in.notes = hm_notes, fm1_in.buttons = hm_buttons;   /* (the script works on the hands) */
#endif
    fm1_ms++;
    if (host_tap) {                             /* last millisecond's taps: let go */
        fm1_in.buttons &= ~host_tap;
        host_rel |= host_tap;
        host_tap = 0;
    }
    while (step < script_n && script[step].t <= fm1_ms) {
        if (script[step].st) {
            mq[mq_w % 64u][0] = script[step].st; mq[mq_w % 64u][1] = script[step].d1; mq[mq_w % 64u][2] = script[step].d2;
            mq_w++;
        } else if (script[step].btn >= 0) {
            uint32_t m = 1u << (script[step].btn & 0xFF);
            if (script[step].btn & 0x200) {     /* up */
                fm1_in.buttons &= ~m;
                host_rel |= m;
            } else {                            /* down, or a tap */
                fm1_in.buttons |= m;
                host_edges |= m;
                if (!(script[step].btn & 0x100)) host_tap |= m;
            }
        }
        else if (script[step].steps)
            host_enc[script[step].enc % 7] += script[step].steps;
        else
            fm1_in.notes = script[step].keys;
        step++;
    }
#ifdef HOST_MATRIX
    hm_notes = fm1_in.notes, hm_buttons = fm1_in.buttons;   /* the hands; then what the matrix lets through */
    fm1_in.notes = acc_n, fm1_in.buttons = acc_b, host_edges = e0, host_rel = r0;
    host_matrix();
#endif
    owed += FS;                                 /* the frames this millisecond is worth, in blocks as the ISR renders */
    while (owed >= 1000u * HALF_FRAMES) {
        uint32_t i;
        owed -= 1000u * HALF_FRAMES;
        memset(blk, 0, sizeof blk);
        {
            uint32_t t0 = fm1_ticks(), dt;
            app_audio(blk, HALF_FRAMES);
            dt = fm1_ticks() - t0;
            static uint32_t sum, cnt;
            if (dt > host_cpu_max) host_cpu_max = dt;
            sum += dt;
            if (++cnt == 86u) { host_cpu_now = sum / 86u; sum = cnt = 0; }   /* as grimm.c's NOW: ~0.5 s */
        }
        wav = realloc(wav, (wav_n + HALF_FRAMES) * 4u);
        for (i = 0; i < 2u * HALF_FRAMES; i++) {
            int32_t v = blk[i];                 /* (USB audio at the MASTER knob all the way up) */
            wav[2u * wav_n + i] = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
        }
        wav_n += HALF_FRAMES;
    }
    if (frame_fps) {                            /* a film: a frame every 1/fps s, as the screen is then */
        if ((uint64_t)frame * 1000u <= (uint64_t)fm1_ms * frame_fps) {
            char name[512];
            snprintf(name, sizeof name, "%s-%05u.ppm", host_out, frame++);
            write_ppm_as(name);
        }
    } else if (shot < sizeof SHOT / sizeof SHOT[0] && fm1_ms == SHOT[shot])
        write_ppm(SHOT[shot++]);
    if (fm1_ms >= end_ms) {
        write_wav();
        exit(0);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1) host_out = argv[1];
    if (argc > 2) read_script(argv[2]);
    if (argc > 3) frame_fps = (uint32_t)atoi(argv[3]);
    memset(fake_flash, 0xFF, sizeof fake_flash);   /* (erased: the factory samples go in at boot) */
    app_main();
    return 0;
}
