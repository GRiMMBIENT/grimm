# The platform API (Dinghy's)

Everything [platform.c](../firmware/src/platform.c) gives your app. One compilation unit: `grimm.c` includes
`platform.c` first, so all of this is just there. Nothing here is a library you link against; read the source
when in doubt, it is short.

- [The contract](#the-contract)
- [Time](#time)
- [Keys, buttons, encoders](#keys-buttons-encoders)
- [LEDs](#leds)
- [Screen](#screen)
- [Sound](#sound)
- [MIDI](#midi)
- [Settings](#settings)
- [Memory and flash](#memory-and-flash)
- [Build options](#build-options)

## The contract

Your app supplies two functions:

```c
static void app_audio(int32_t *out, uint32_t n);   /* the audio interrupt: fill n stereo frames */
static void app_main(void);                        /* your main loop; never returns */
```

`app_main` must call **`platform_poll()`** often: every few milliseconds, and never less than once every
couple of seconds. It feeds the watchdog (8 s, then the FM-1 resets), runs USB (MIDI, the updater, a computer
asking for the update mode) and reads the MASTER knob. A long job (drawing a lot, saving) is fine; a loop that
waits for something must poll.

```c
static void app_main(void)
{
    /* set up */
    for (;;) {
        platform_poll();
        /* read the panel, MIDI; draw what changed */
    }
}
```

Names the platform defines that you can change from the build: `APP_NAME` (the rescue and crash screens,
default `"DINGHY"`), `APP_VER` (set by `./build.sh --release`), `FELUCCA_ID` (the package identity:
GR!MM's is `FM-1_966`, set in `tools/build.py`).

## Time

| | |
| --- | --- |
| `fm1_ms` | milliseconds since power-on (`volatile uint32_t`, counted by the 10 kHz timer) |
| `fm1_ticks()` | a free-running 24 MHz counter: `FM1_TICKS_PER_US` (24) per microsecond; wraps in ~179 s, so subtract: `fm1_ticks() - t0` |
| `fm1_micros()` | microseconds, from the same counter |
| `fm1_delay_ms(ms)`, `fm1_delay_us(us)` | busy waits; keep them short (they do not poll) |

To time your audio code: `t0 = fm1_ticks()` before, `(fm1_ticks() - t0) / FM1_TICKS_PER_US` after.

## Keys, buttons, encoders

The panel is scanned at 10 kHz in the background and debounced.

| | |
| --- | --- |
| `fm1_in.notes` | the 27 keys held now: bit 0 = F3 ... bit 26 = G5 |
| `fm1_in.buttons` | the 14 buttons held now: bit = `BTN_...` |
| `fm1_input_edges(&released)` | buttons **pressed** since the last call (bits), and those released into `released` |
| `fm1_input_note_edges()` | keys pressed since the last call (bits): a press shorter than a loop pass still counts |
| `fm1_enc_take(ENC_...)` | detents turned since the last call, + = clockwise |
| `fm1_adc_read(FM1_ADC_MASTER)` | the MASTER knob, 0..1023 (-1 = not ready). platform.c already makes it the volume |
| `fm1_adc_read(FM1_ADC_BATT)` | the battery voltage, raw |

Buttons (ids 0..13):

| id | | id | | id | |
| --- | --- | --- | --- | --- | --- |
| 0 | `BTN_OCT_DOWN` | 5 | `BTN_LFO` | 10 | `BTN_ARP` |
| 1 | `BTN_OCT_UP` | 6 | `BTN_EDIT` | 11 | `BTN_SEQ` |
| 2 | `BTN_FX` | 7 | `BTN_GLO` | 12 | `BTN_PLAY` |
| 3 | `BTN_SEL` | 8 | `BTN_HOME` | 13 | `BTN_REC` |
| 4 | `BTN_ENV` | 9 | `BTN_SAVE` | | |

`BTN_OCT_DOWN` held at power-on is the USB rescue; your app sees it like any button otherwise.

Encoders: `ENC_SELECT` (0), `ENC_ALGORITHM` (1), `ENC_KNOB1` .. `ENC_KNOB4` (2..5), `ENC_PRESETS` (6).

A key down and up, the usual way:

```c
uint32_t keys = fm1_in.notes, down = keys & ~prev, up = prev & ~keys;
prev = keys;
```

## LEDs

| | |
| --- | --- |
| `fm1_led_key(id, on)` | a button (`BTN_...`) or a key (`KEY_LED(k)`, k = 0..26) lit or not |
| `fm1_led[col]`, `fm1_led_dim[col]` | the raw layers, for many LEDs at once (see `hal/fm1_input.h`); `_dim` glows at ~1/24 |

## Screen

240 x 240 pixels, 16-bit colour (`RGB(r, g, b)` with 0..255 each; `C_BLACK`, `C_WHITE`). The screen keeps
what you draw: draw only what changed.

Straight to the screen:

| | |
| --- | --- |
| `lcd_fill(x, y, w, h, colour)` | a rectangle |
| `draw_text_box(x, y, w, font, "text", colour, align)` | text in a box of width `w`, cleared to black first; align 0 left, 1 centre, 2 right |

Fonts: `FONT_S` (16 px, ASCII 32..126) and `FONT_L` (32 px, ASCII 32..95: capitals, digits, signs; small
letters show as `?`).

For anything more, a canvas: draw into a buffer, then send it in one go (no flicker):

```c
cv_begin(240, 56, C_BLACK);                 /* w x h, cleared; w * h <= CV_MAX (240 x 64) */
cv_rect(x, y, w, h, colour);
cv_line(x0, y0, x1, y1, colour);
cv_pset(x, y, colour);
cv_text(x, y, &FONT_S, "text", colour);     /* returns x after the text; text_w(&FONT_S, "text") measures */
cv_blit(0, 140);                            /* to the screen at (0, 140) */
```

A bigger canvas: raise `CV_MAX` in platform.c (2 bytes a pixel, in the pool: 240 x 240 is 113 KB of 336).

## Sound

```c
static void app_audio(int32_t *out, uint32_t n)
```

- Runs in the audio interrupt, every 5.8 ms, with `n` = 256 frames at **44.1 kHz** (`FS`).
- `out` is `L R L R ...`, **already zero**: add your sound to it. Full scale is **±32767** (Q15); more is clipped.
- After you: the MASTER knob, the clip, the USB audio stream (the computer records exactly this), the DAC.
  The DAC gets it 6 dB under its full scale, so the FM-1's output stage does not distort.
- Keep it fast and simple: no flash, no screen, no waiting. Talk to the main loop through `volatile` variables
  (one writer each). If a render runs late the sound breaks up; see [RULES.md](RULES.md).

Pitch: a 32-bit phase that wraps is an oscillator with no division at run time: `inc = f * 2^32 / 44100`,
`ph += inc` each frame. GR!MM's `pitch_inc()` (Felucca's, in `firmware/gen/analog_tables.h`) keeps a table for one
octave and shifts it for the others.

## MIDI

USB-MIDI, class compliant, cable 0.

| | |
| --- | --- |
| `midi_get(&status, &d1, &d2)` | 1 and the next message received, or 0. Call it from the main loop until 0 |
| `midi_send(status, d1, d2)` | send one message (from the main loop only) |
| `batt_raw()` | the battery divider (PB1, ADC channel 3), raw 0..1023, or -1. No calibration to volts: GR!MM uses the stock firmware's thresholds (531, 561, 591, as Felucca) |
| `usb_powered()` | 1 while a USB host has the FM-1 configured (it is powered then); there is no charger status line, so a charger alone is not seen |
| `midi_room()` | messages `midi_send` can still queue now (0: the next would be dropped; no host connected: all of it) |

`status` includes the channel: `0x90` note on channel 1, `0x93` channel 4; a note on with velocity 0 is a
note off. SysEx is the platform's (the updater), but for GR!MM's: a frame `F0 7D .. F7` (7D, the non-commercial ID) is the
app's: `sysex_get(&p, &n)` gives its 7-bit bytes between F0 and F7, `sysex_done()` lets it go (left a second, the
platform drops it: the updater always gets through), `sysex_send(p, n)` answers (F0 .. F7). GR!MM's sample upload
uses it (`samples.c`).

## Settings

```c
static int settings_load(void *p, uint32_t n);         /* 0 = loaded; else *p is left alone */
static int settings_save(const void *p, uint32_t n);   /* 0 = saved */
```

Up to 256 bytes, kept with their length and a CRC32 in one 4 KiB flash sector (`SETTINGS_AT`, 0xFC000). A
change of size or a broken save reads as "nothing saved": start from your defaults then.

**Save when nothing sounds.** Erasing the sector holds the interrupts off for tens of milliseconds: the sound
drops out. `grimm.c` saves 2 s after the last change, once all voices are silent. Flash wears out after about
100,000 erases of a sector: never save in a loop.

## Memory and flash

| | | |
| --- | --- | --- |
| Code + constants | 568 KB | runs straight from flash (`const` tables cost no RAM) |
| RAM `.data` + `.bss` | 96 KB | your globals and statics |
| Pool | 336 KB | big buffers, zeroed at boot: `static int16_t delay[65536] __attribute__((section(".pool")));` |
| Stack | 24 KB | keep big arrays off it: `static` |

The build prints what you use and fails above the limits.

Flash (1 MiB) as Dinghy uses it: the app, the update staging area (0xE0000, the platform's) and the settings
sector (0xFC000). The other firmwares of the family keep their data in 0x97000..0xFBFFF; a new firmware simply
finds its own magic missing and starts fresh. To store more (samples, patterns), widen `FL_RANGE_OK` in
platform.c to the range you pick: it is the guard that keeps a bug from erasing anything else.

GR!MM's saves (`store.c`): six 4 KiB sectors where Felucca keeps data an app writes, none in the sample area:
0xDF000 (the sounds), 0xE5000 / 0xE6000 (the work, two copies in turn: the newest good one loads), 0xE7000, 0xFD000,
0xFE000 (the slots A..D, one copy, a stream over the three: saving a slot rewrites the sectors it lives in; a bad
sector empties only its slots); each sector a header with a count and a CRC-32, read back after writing; `save_flash(i)` reads sector i through
XIP, `save_flash_erase(i)` and `save_flash_write(i, off, p, n)` change it (in `FL_RANGE_OK`); an erase stops the audio
~50 ms.

GR!MM's sample area: 0x97000..0xDF000 (`SAMPLE_AREA`, 288 KiB, in `FL_RANGE_OK`), four slots of 72 KiB (`samples.c`;
6.64 s each of 22050 Hz IMA ADPCM, 26.6 s in all). An install writes the flash up to 0x93000 only: the samples stay.
`sample_flash()` is the area through XIP (plain: `fl_plain_window_init` at boot), `sample_flash_erase(off)` and
`sample_flash_write(off, p, n)` change it; an erase or a write stops the audio (only while nothing sounds).

## Build options

In platform.c, before `#include "usb.c"`:

| | | |
| --- | --- | --- |
| `FELUCCA_UAC` | 1 | USB audio input (the output to the computer) |
| `FELUCCA_CDC` | 0 | a USB serial console for debugging (some hosts then hide the audio; see usb.c) |
| `FELUCCA_OTA` | 1 | the updater. **Leave it on**: without it nothing can install over your firmware but the rescue |
