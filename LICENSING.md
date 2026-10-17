# Licensing

GR!MM is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, full text in
`LICENSE`). One file in it, `firmware/src/fm6_core.c` (msfa), is Apache-2.0 (`LICENSES/Apache-2.0-msfa.txt`), and two,
`firmware/src/phys_dsp.c` (DaisySP) and `firmware/src/phys_symp.c` (Rings), are MIT (`LICENSES/MIT-DaisySP.txt`,
`LICENSES/MIT-Rings.txt`); Apache-2.0 and MIT code may be combined into a GPL-3.0 work, and the firmware built with it is GPL-3.0-only as a whole. The four factory samples are CC0 1.0 (`LICENSES/CC0-1.0.txt`). You may use, study, change and share it. If you give someone a firmware built from it, give them its
complete source under the same licence too.

GR!MM is a modified version of [Dinghy](https://github.com/zvenson/dinghy) (zvenson), which is based on Felucca and
SLOOP. Everything below that is not marked as a GR!MM change is theirs.

## Who made what

| | |
| --- | --- |
| The platform: `firmware/hal/`, `firmware/loader/`, `crt0.S`, `app.ld`, `firmware/src/` (`usb.c`, `ota.c`, `recovery.c`, `lcd.c`, `gfx.c`, `libc.c`, `bootguard.h`, `uac_fir.h`), `tools/` (`build.py`, `fm1pkg_make.py`, `fm1_install.py`, `lz4blk.py`, `get_toolchain.sh`, `gen_tables.py`, `sampleio.py`, `gen_uac_fir.py`: SLOOP's, after Felucca 1.1.5's, it makes `uac_fir.h`) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments ([Felucca](https://github.com/hugelton/Felucca)), with the changes of [SLOOP](https://github.com/isod89/sloop-fm1) (isod89). `firmware/gen/felucca_font.h` (the Terminus font, below) was made by Felucca's / SLOOP's `tools/gen_font.py`, which is not in this tree: see [SLOOP](https://github.com/isod89/sloop-fm1) |
| The app `firmware/src/grimm.c` (Dinghy's `dinghy.c`), the wiring `platform.c`, the host mode `tests/`, `docs/API.md`, `docs/RULES.md` | Copyright (C) 2026 zvenson ([Dinghy](https://github.com/zvenson/dinghy)) |
| GR!MM changes: the name, the package identity (`FM-1_966`), the file names, the browser build's switch in `platform.c` (`GRIMM_WEB`), the app in `firmware/src/grimm.c` (four tracks, latch, the mixer and its sends, pages, screen, CPU meter), the host script's encoders and clock in `tests/host.c` | Copyright (C) 2026 GR!MM |
| The delay `firmware/src/tape.c` (DELAY) and its wear `firmware/src/wear.c` (the wow and flutter, the saturation, shared with the TAPE effect) | Copyright (C) 2026 GR!MM. Written for GR!MM in integers; its design follows X0X's TAPE delay ([X0X](https://github.com/charlesvestal/fm1-x0x) `firmware/src/dsp/fxbus.c` `fx_delay`, charlesvestal, GPL-3.0, itself a port of 9W9's `er99_dly_tick`, GPL-3.0): wow and flutter on the read, a darker loop held by a tanh saturation, a DC block in it, feedback past unity. X0X's code is float (the FM-1 cannot run it): none of it is copied. It uses Felucca's integer helpers as `dsp.c` / `hall.c` carry them |
| The reverb `firmware/src/hall.c` (SPACE) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's HALL reverb ([Felucca](https://github.com/hugelton/Felucca) `firmware/src/fx.c`: `rev_hall`, `hall_ap`, `hall_end_lp`, `sh_tz`, `HL_RUNS`; the Paley 8 x 8 feedback matrix, the half-rate design), taken over without Felucca's buses. GR!MM changes Copyright (C) 2026 GR!MM: longer lines and 4 diffusers, DAMP on all 8 lines, decay 0.4 .. ~32 s and INF (a freeze), a soft knee in the feedback, PRE-DELAY, MIX, the gliding taps, the idle skip; its gain and decay tables in `tools/gen_tables.py` |
| The DSP `firmware/src/dsp.c`, its tables `tools/gen_tables.py` → `firmware/gen/analog_tables.h` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's `firmware/src/dsp.c` (unchanged) and `tools/gen_tables.py` ([Felucca](https://github.com/hugelton/Felucca)). GR!MM changes Copyright (C) 2026 GR!MM: the tables' GR!MM parts (drone envelope, SUPERSAW, SPACE, DELAY); the FM tables (SINE, FM6_*) are Felucca's `fm6_tables`, unchanged |
| The GRAIN effect `firmware/src/grain.c` (the GRAIN engine until step 7) | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's GRAIN engine (`firmware/src/eng_grain.c`: the grain, its length and interval from SIZE and DENSITY, the spread and jitter, the overlap level, the Hann window), taken over without Felucca's sample sets, zones, reverse grains and per-part pool. GR!MM changes Copyright (C) 2026 GR!MM: an effect over a ring buffer of the GRAIN sends instead of a sample, FREEZE, a pan for each grain, the return into the mix and SPACE |
| The SAMPLE source `firmware/src/sampler.c` (step 7's TAPE engine) | Copyright (C) 2026 GR!MM: GR!MM's own (varispeed over a sample slot, the loop and its two crossing heads, the window cache). It reads the slots with `samples.c`'s decoder and seek index and uses `pow2_q16` (Felucca's, in `samples.c`) |
| The TAPE effect `firmware/src/tapefx.c` | Copyright (C) 2026 GR!MM: GR!MM's own (the short line the wow moves, the saturation as one curve, the low-pass, the hiss, the bypass cross-over). Its wow, flutter and saturation are DELAY's (`wear.c`, after X0X's TAPE delay as above) |
| The factory samples' tool `tools/gen_samples.py` (its output `firmware/gen/grimm_samples.h`: the samples below, as a slot holds them) | Copyright (C) 2026 GR!MM | GPL-3.0-only |
| The sample slots and their upload `firmware/src/samples.c`, `tools/fm1_sample_upload.py` | Copyright (C) 2026 GR!MM. The slot layout and the upload protocol (BEGIN / WRITE / END / ERASE / INFO, pack7) follow Felucca's user sample slots and uploader (`web/EDITOR_PROTOCOL.md`, `tools/fm1_sample_upload.py`, Copyright (C) 2026 Leo Kuroshita, GPL-3.0), with GR!MM's own magic ("GSMP") and SysEx ID (7D 47 52); the IMA ADPCM decoder is Felucca's (`eng_grain.c`), as tables built at boot (GR!MM), and so is `pow2_q16` (`eng_sample.c`) |
| `tools/sampleio.py` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's, unchanged (WAV reading, resampling, IMA ADPCM encoding, the slot header) |
| The platform's sample area and app SysEx (`firmware/src/platform.c`: `SAMPLE_AREA`, `sample_flash*`, `sysex_*`) | Copyright (C) 2026 GR!MM |
| The FM engine `firmware/src/fm.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's FM6 engine (`firmware/src/eng_fm6.c`: the macros MLVL / MEG / DTUN, a note's start, a block's render), taken over without the patch formats, the factory slots, the voice bank, the editor and the presets. GR!MM changes Copyright (C) 2026 GR!MM: the four knobs (PATCH, BRIGHT, DECAY, DETUNE), the drone envelope over the patch, the engine interface |
| The FM patches `tools/gen_fm_patches.py` → `firmware/gen/grimm_fm.h` (BELL, ORGAN, SOFT PAD, REED) | Copyright (C) 2026 GR!MM: GR!MM's own, written for it; no factory data of any instrument. Felucca's F1..F8 are not included: Felucca states they are its own, but at least one (TINE EP) looks like a classic DX7 ROM voice, so their origin is not clear enough to redistribute here |
| The VA engine `firmware/src/analog.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's ANALOG engine (`firmware/src/eng_analog.c`), taken over without Felucca's framework. GR!MM changes Copyright (C) 2026 GR!MM: SUPERSAW (its detune offsets from Adam Szabo's published measurements of the JP-8000), the engine interface |
| The VOICE source `firmware/src/formant.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's VOICE engine (`firmware/src/eng_formant.c`: the Klatt-style formant oscillator, its vowel and bandwidth data from Klatt (1980) and Hillenbrand et al. (1995); its design follows klattsch by Tony Gies, MIT, design reference only, no code copied) and `sine_i` / `noise32` of Felucca's `dsp.c`, taken over without VOWL2, TALK, RAND and the modulation. GR!MM changes Copyright (C) 2026 GR!MM: the four knobs (VOWEL, SHIFT, BUZZ, BREATH), a 32-bit division where Felucca's has a 64-bit one, the level, the engine interface |
| The WHEEL source `firmware/src/wheel.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's WHEEL engine (`firmware/src/eng_wheel.c`, Felucca's own design: the nine drawbar partials, the 16 registrations and their normalisation, the percussion, the key click, the overdrive, the rotary speaker), taken over without SUB / BODY / TOP. GR!MM changes Copyright (C) 2026 GR!MM: the four knobs (DRAWBARS, PERC, CLICK, DRIVE), the rotary fixed at SLOW, the track's state made by its first voice each block, the engine interface |
| The PHYS source `firmware/src/phys.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's PHYS engine (`firmware/src/eng_phys.c`: the four models MODAL, STRING, MEMBRANE, SYMP, their parameters, the SYMP chords, the output gains and soft knee), taken over without the presets, the legacy loading and Felucca's per-part engine memory. GR!MM changes Copyright (C) 2026 GR!MM: the four knobs (MODEL, STRUCTURE, DAMP, BOW), BOW 50 % at first and x 4 when bowed (the drone attack would swallow a strike), a new strike when MODEL changes, a state slot a voice, the engine interface. Its DSP: `phys_dsp.c`, `phys_symp.c` (below) |
| The PHASE source `firmware/src/phase.c` | Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments: Felucca's PHASE engine (`firmware/src/eng_phase.c`: `pd_cos`, `pd_setup`, `pd_wave`, the render loop, the wave names), whose waveforms are a C port of the oscillator of CrispyZebra (<https://github.com/hugelton/CrispyZebra>, Leo Kuroshita, GPL-3.0, the author's own phase-distortion core), taken over without ENV -> DCW, the ring modulation and SUB. GR!MM changes Copyright (C) 2026 GR!MM: the four knobs (WAVE, WAVE2, DCW, DETUNE), the level, the engine interface |
| The tracks and voices `firmware/src/voices.c` | Copyright (C) 2026 GR!MM: the four tracks, the engine interface, the six shared voices and their stealing. Its envelope is Felucca's `voice.c` `env_tick` (and `dsp.c` `amp_at`), Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, with GR!MM's drone times and fade |
| The browser build `web/` (`fm1_web.c`, `worklet.js`, `panel.js`, `index.html`, `build_web.sh`, `test_web.mjs`; the editor page: below) | Copyright (C) 2026 GR!MM. Its design (the app as WebAssembly in an AudioWorklet, the device clock driven by the audio, the screen and lights read back), the page's panel layout and its computer-keyboard map follow Felucca's browser build `web/emu` (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, [Felucca](https://github.com/hugelton/Felucca)), which follows [X0X](https://github.com/charlesvestal/fm1-x0x)'s `web/emu` (charlesvestal); both GPL-3.0 |

## GR!MM's own parts

Copyright (C) 2026 GR!MM, GPL-3.0-only, unless a line says otherwise.

| What | Notes |
| --- | --- |
| The saves `firmware/src/store.c` (SOUNDS, slots A..D, autosave, undo) |  |
| The stress test `tests/stress.sh`, `tests/stress_gen.py`, `tests/stress_check.c`, `tests/device_names.py` |  |
| The matrix ghost test `firmware/hal/fm1_matrix.h` (the layout in it: the platform's, moved from `fm1_input.h`), its use in `fm1_input.h` (`fm1__keys`), `fm1_led_glow`; the matrix simulation in `tests/host.c` (`HOST_MATRIX`) |  |
| The FX performance layer `firmware/src/perform.c` |  |
| The editor's mailbox `firmware/src/editor.c`, its protocol `docs/EDITOR_PROTOCOL.md`, `tools/fm1_editor_test.py` | GR!MM's own code. Its approach (objects by id, read by offset and count, written as BEGIN with length and CRC-32, DATA in order, COMMIT; "stop first" refusals) follows SLOOP's editor backup ([sloop-fm1](https://github.com/isod89/sloop-fm1) `web/EDITOR_PROTOCOL.md`, commands 34..36, isod89, GPL-3.0), and its live TRACK SET follows SLOOP's live SET / TRACK_PARAM (commands 3, 31); none of its code is copied. It rides on the sample upload's SysEx head and pack7 (after Felucca's, above) |
| The editor page `web/editor.html`, `web/editor.js`, `web/editor_core.js`, `web/editor_web.c`, `web/fm1_frames.json` (the FM-1's own replies, recorded by `tools/fm1_editor_test.py --record`) | GR!MM's own code and look. Its sections in a left menu (CONNECT, SOUNDS, SOUND EDITOR, SAMPLES, BACKUP) and its way of editing (the tracks' sounds on screen, every move sent live, one request in flight and the newest value kept while dragging, `editor.html` `userSet`; the device polled to follow its knobs) follow SLOOP's editor page ([sloop-fm1](https://github.com/isod89/sloop-fm1) `web/`, isod89, GPL-3.0); none of its code is copied. The WAV reading, resampling, IMA ADPCM encoding and slot header in `editor_core.js` are a JavaScript port of Felucca's `tools/sampleio.py` (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, GPL-3.0, above) and of `tools/fm1_sample_upload.py`; the FM ALGO drawing is a port of `ui.c`'s |
| Song mode `firmware/src/song.c` |  |
| The LFO `firmware/src/lfo.c` |  |
| The arpeggiator `firmware/src/arp.c` |  |
| The scales and chords `firmware/src/scale.c` |  |
| The sequencer `firmware/src/seq.c` |  |
| The screens `firmware/src/ui.c`, after the concept designs in `docs/ui-concept` |  |
| SYSTEM and MIDI (step 20): COLOUR, MIDI CHANNEL, TUNE, SCREEN OFF, MIDI LEARN, SEQ MIDI OUT, in `firmware/src/grimm.c` and `ui.c` | |
| The moving pictures (step 21): the boot screen, HOME's eye, candles and stars, in `firmware/src/ui.c` | |
| The 40 factory sounds (step 22): `SND_FACTORY` and `SND_FNAME` in `firmware/src/store.c` | Written for GR!MM; no factory data of any instrument |
| The track filter (step 23, HOME's CUTOFF and RESONANCE) and the FM editor (step 24, FM ALGO and FM OP: each track its own patch) in `firmware/src/grimm.c`, `voices.c`, `fm.c` (GR!MM changes) and `ui.c` | The FM ALGO drawing follows the 32 algorithms' wiring in `fm6_core.c` (msfa, below) |
| The battery icon (step 26a2): `batt_poll` in `firmware/src/grimm.c`, `ui_bat_draw` in `ui.c` | The thresholds (531, 561, 591 on ADC channel 3) are the stock firmware's, as Felucca has them |
| The editor's sound names, live TRACK, the factory samples' install and RESTORE (steps 26b2, 26c): `firmware/src/editor.c`, `store.c`, `samples.c`, `grimm.c`, `web/editor*.js` | |
| `README.md`, `MANUAL.md`, `docs/BUILDING.md`, `docs/EDITOR_PROTOCOL.md`, `docs/screens/` (rendered from the firmware by host mode), `docs/ui-concept/` | |

## The factory samples

| What | Licence | Where |
| --- | --- | --- |
| GR!MM's four factory samples, URCHIN, LEAVE, NIKOLAI and M-SOFT (`assets/samples/Urchin.wav`, `Leave.wav`, `Nikolai.wav`, `M-Soft.wav`; in the firmware and the browser build's `grimm.wasm` as `firmware/gen/grimm_samples.h`): recordings, Copyright (C) 2026 GR!MM. Free to use for anything, no credit needed | CC0 1.0 Universal | `LICENSES/CC0-1.0.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| msfa by Google Inc. and Pascal Gauthier, from Dexed (<https://github.com/asb2m10/dexed>): the FM engine's synthesis, ported to integer C by Leo Kuroshita for Felucca (`firmware/src/fm6_core.c`, as Felucca has it, unchanged). Dexed itself is GPL-3.0; only msfa is used. Its start-up tables are precomputed by Felucca's `fm6_tables` in `tools/gen_tables.py` (msfa's LFO rates among them) | Apache-2.0 | `firmware/src/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the PHYS source's modal and string models and the resonator, ported to fixed point by Leo Kuroshita for Felucca (`firmware/src/phys_dsp.c`, as Felucca has it, unchanged) | MIT | `firmware/src/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>): the PHYS source's sympathetic strings, ported to fixed point by Leo Kuroshita for Felucca (`firmware/src/phys_symp.c`, as Felucca has it, unchanged) | MIT | `firmware/src/phys_symp.c`, `LICENSES/MIT-Rings.txt` |
| Press Start 2P by the Press Start 2P Project Authors (cody@zone38.net), Reserved Font Name "Press Start 2P" (<https://github.com/google/fonts/tree/main/ofl/pressstart2p>): the screens' font, `assets/fonts/PressStart2P-Regular.ttf`, rasterised at its 8 px grid by `tools/gen_pixfont.py` into `firmware/gen/grimm_pixfont.h` (a Modified Version under the OFL, so called "pixfont"; also in the browser build's `grimm.wasm`); the editor page uses the TTF itself, unchanged, copied to `web/dist/` with its licence | SIL OFL 1.1 | `LICENSES/OFL-PressStart2P.txt` (copied to `web/dist/LICENSES/`) |
| Terminus Font 8x16 (ter-u16n) by Dimitar Toshkov Zhekov, with Reserved Font Name "Terminus Font" (<https://terminus-font.sourceforge.net>), rendered into `firmware/gen/felucca_font.h` (also in the browser build's `grimm.wasm`) | SIL OFL 1.1 | `assets/fonts/Terminus-LICENSE.txt` (the font's own OFL.TXT; copied to `web/dist/`) |
| The browser build's `grimm.wasm` links a few routines of Emscripten's C library (`memset`, `memcpy`, from musl), built with Emscripten and Binaryen; nothing of them is in this tree | MIT (Emscripten; musl) | `LICENSES/MIT-Emscripten.txt`, `LICENSES/MIT-musl.txt` (copied to `web/dist/LICENSES/`) |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## The logo

Dinghy's logo (`assets/logo.svg` / `logo.png`) carries the designburg signet: Copyright (C) designburg, all rights
reserved, not under the GPL. It marks Dinghy itself and a fork uses its own, so GR!MM does not include it.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments; "M-VAVE" and "FM-1" belong to their
owners. GR!MM (like Dinghy) is not made or endorsed by them, nor by zvenson or designburg.
