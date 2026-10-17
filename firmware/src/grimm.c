/* SPDX-License-Identifier: GPL-3.0-only */
/* GR!MM: an FM-1 firmware, built on Dinghy (zvenson). Step 26a2: four tracks (sources:
 * VA, FM, SAMPLE over uploaded samples, VOICE, WHEEL, PHYS, PHASE), effects under FX (TAPE over the whole mix,
 * GRAIN, DELAY, SPACE), the mixer under HOME.
 * GR!MM changes Copyright (C) 2026 GR!MM; Dinghy by zvenson; the platform by Felucca / SLOOP; the DSP
 * (dsp.c), the VA engine (analog.c), the FM engine (fm.c, around msfa: fm6_core.c, Apache-2.0), the GRAIN effect's grain
 * (grain.c) and the envelope (voices.c) are Felucca's, the reverb (hall.c) Felucca's HALL; the delay (tape.c), its wear
 * (wear.c), the TAPE effect (tapefx.c) and the SAMPLE source (sampler.c) are GR!MM's, the delay's design after X0X's
 * TAPE (see LICENSING.md).
 *
 * Dinghy: the smallest FM-1 firmware to build your own on. This is the file to change. Everything below it
 * (platform.c) is done: updates, the USB rescue, USB-MIDI, USB audio, keys, knobs, LEDs, the screen, settings.
 *
 *   tracks     T1..T4, each its source (see EDIT), its notes, ENV, LATCH, LEVEL, PAN, and three sends: DLY, REV,
 *              GRAIN; ALGORITHM selects one; 6 voices shared by all
 *   keys       play the selected track: LATCH OFF (as at first), a note while its key is held; LATCH ON, a key starts
 *              its note, the same key again stops it; up to 6 notes in all (a 7th drops the oldest, wherever it is:
 *              its voice fades out in ~10 ms); the selected track's notes light their keys and go out over USB-MIDI
 *   buttons    the control map: a tap opens the button's first page, again the next (round); the header says
 *              "BUTTON > PAGE" (and the track, FREEZE). Buttons with a hold function (SEL GLO HOME SAVE ARP SEQ FX)
 *              change page when let go. Pages not built yet show their knobs' names and NOT BUILT YET.
 *   EDIT       SOUND (KNOB 1..4: the source's four: VA: WAVE saw / square / supersaw, DETUNE, CUTOFF, RESONANCE;
 *              FM: PATCH, BRIGHT, DECAY, DETUNE; SAMPLE: SLOT, START, LENGTH, REVERSE; VOICE: VOWEL, SHIFT, BUZZ,
 *              BREATH; WHEEL: DRAWBARS, PERC, CLICK, DRIVE; PHYS: MODEL, STRUCTURE, DAMP, BOW; PHASE: WAVE, WAVE2,
 *              DCW, DETUNE) -> SOURCE (SOURCE: its notes go over, faded, each source's settings kept; LATCH: OFF /
 *              ON, ON -> OFF releases the track's notes); an FM track: SOUND -> FM ALGO (ALGO: the 32 algorithms,
 *              FEEDBACK, OP 1..6, FINE: the operator's) -> FM OP (that operator's RATIO 0.5 .. 31, LEVEL, ATTACK,
 *              DECAY) -> SOURCE: the track's own patch (fm.c), PATCH loading a built-in one into it (undoable)
 *   ENV        ENV: ATTACK (~1 ms .. ~10 s), DECAY (~10 ms .. ~10 s), SUSTAIN, RELEASE (~10 ms .. ~20 s), the track's
 *              (kept across sources; a source's own envelope runs inside it)
 *   LFO        LFO (the track's: RATE ~60 s a cycle .. ~20 Hz, free-running; DEPTH 0..100 %, 0 off; DESTINATION PITCH
 *              (+-1 octave), VOLUME, PAN, the source's four knobs by their names, REV SEND, GRAIN SEND: moved around the
 *              knob, the knob not; SHAPE SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE, RANDOM, DRIFT) (lfo.c)
 *   SEL        SCALE (ROOT C .. B, SCALE: OFF and 13 scales; the project's, for all tracks: the keys, MIDI in and what
 *              they record snap to its nearest note, GRID's NOTE steps through it) -> CHORD (the track's CHORD TYPE: SCALE
 *              (the scale's triad on the note: in key), MIN .. CLUSTER; VOICING: CLOSE, OPEN, SPREAD, +OCT, DROP); held:
 *              the track's chord mode on / off, as SEL is let go (no page change; a knob turned meanwhile: not): a key (a
 *              MIDI note, a note step) plays its chord (scale.c); a step recorded in chord mode keeps its chord (a chord
 *              step, in the accent); SEQ's pages, SEL held: the knobs lock the selected step's sound (the source's four;
 *              a mark in the grid), + OCT-: its locks cleared
 *   GLO        SONG (the chain: up to 16 steps, each a slot A..D and its REPEATS, or END; SECTION, PATTERN, REPEATS,
 *              MODE OFF / SONG / SONG LOOP; REC on it: song record, the slots switched to as they are played) (song.c);
 *              held + white keys 1..4: mute T1..T4, 5..8: solo them (on / off; lit), no page change
 *   HOME       HOME (the track's VOLUME (its LEVEL), CUTOFF and RESONANCE of its low-pass after the source and ENV
 *              (open: bypassed), FX: its three sends together 0 .. 200 %; the header: the track and its sound, a
 *              knob turned: what it changed a moment; the battery beside the tempo; the slabs: T1 .. T4, their
 *              LEVEL) ->
 *              MIXER 1 (the track's LEVEL, DLY SEND, REV SEND, GRAIN SEND) -> MIXER 2 (PAN);
 *              held: SYSTEM (COLOUR: RED, AMBER, TEAL, VIOLET, the accent everywhere; MIDI CHANNEL: "1-4" (T1 .. T4 on
 *              channels 1..4) .. "13-16", ALL > SEL (any channel plays the selected track), for MIDI in and out; TUNE
 *              -50 .. +50 cents, every source; SCREEN OFF: NEVER, 1, 5, 15, 30 MIN untouched, the screen dark, all else
 *              running, the first touch only wakes it), HOME again: SYSTEM 2 (MIDI LEARN: LEARN, a knob turned on any
 *              page, then a CC: linked, 32 at most; CLEAR ALL + OCT+; OCT- cancels; SEQ MIDI OUT: the steps' and the
 *              arps' notes out on their tracks' channels too; SAMPLES: RESTORE + OCT+ writes the four factory
 *              samples over the slots, OCT- keeps them); the FM-1's settings, not the project's
 *   SAVE       SOUNDS (64 slots: a track's source, its four, ENV, LATCH (FM: its own patch); SLOT, CATEGORY, TYPE (the list by source),
 *              ACTION; playing: SAVE / CLEAR wait for the stop) -> PROJECTS (the slots A..D: the four tracks' sounds,
 *              sequences and the mixer; SLOT, ACTION LOAD / SAVE / ERASE / NEW); OCT+ does the ACTION, OCT- backs out;
 *              held + white keys 1..4 (from F3): recall A..D (playing: at the next bar), 5..8: save into A..D (used:
 *              again within 3 s to overwrite); the header shows the slots in use (store.c)
 *   autosave   the work (all of it: tracks, mixer, FX, tempo, sequences, slots) saves itself when stopped and untouched
 *              2.5 s, at most every 20 s, only if changed, never while playing nor while a voice sounds (a release, a
 *              latched note: SAVES WHEN SILENT, as SOUNDS' SAVE / CLEAR); at power-on it comes back as left. A watchdog
 *              restart says WATCHDOG RESET at boot (a fault: platform.c's red crash screen, its PC)
 *   ARP        ARP (the track's: MODE UP, DOWN, UP-DOWN, RANDOM, AS PLAYED, CHORD; RATE 1/1 .. 1/32, 1/4T .. 1/16T of
 *              the tempo; OCTAVES 1..4; GATE 10..100 %); held: the track's arp on / off (no page change): its keys and
 *              MIDI notes (their chords) arpeggiated, on the sequencer's clock while it plays (arp.c)
 *   SEQ        GRID (the track's 64 steps; KNOB: the selected step's NOTE (all down: OFF), CHANCE, RATCHET,
 *              LENGTH) -> PATTERN SETTINGS (STEPS 1..64, DIVISION 1/4 .. 1/32 and triplets, SWING, PLAY MODE); the
 *              sequencer: seq.c; held + white keys F3 .. F4: CLEAR, RANDOM, SHIFT LEFT, SHIFT RIGHT, COPY, PASTE, DOUBLE,
 *              MUTATE (the selected track's pattern; undoable; no page change; the eight keys lit)
 *   FX         TAPE -> GRAIN -> DELAY -> SPACE:
 *              TAPE:  WOW, SATURATION, TONE, HISS, over the whole mix (all 0: bypassed)
 *              GRAIN: SIZE (10 .. 390 ms), DENSITY (1 .. 98 a second), SCATTER, MIX (return); FX held + the lowest
 *                     white key (F3): FREEZE on or off
 *              DELAY: TIME (20 .. 3500 ms), FEEDBACK (0 .. 110 %), WEAR (darker, more wobbly repeats), MIX (return)
 *              SPACE: SIZE (the decay, 0.4 .. ~32 s, then INF), DAMP, MIX (return), PRE-DELAY (0 .. 250 ms)
 *              held ~0.3 s: PERFORM (let go: back where it was); held + G3 .. F4, while held: REV THROW, ECHO THROW,
 *              TAPE STOP, REVERSE, FILTER DOWN, FILTER UP, STUTTER (the tricks' keys glow, lit while on; perform.c)
 *   sound      each track -> LEVEL, PAN -> out; its GRAIN send -> GRAIN -> out and SPACE; its DLY send -> DELAY -> out and
 *              SPACE; its REV send -> SPACE -> out; then all of it through TAPE, the FX layer's tricks, then MASTER
 *   PLAY       start / stop all four tracks' sequences together
 *   REC        record on / off: stopped, step record (a key writes its note into the selected step, the cursor on);
 *              playing, live record (a key's note lands on the nearest step of the track); REC off: the keys play
 *   SELECT     SEQ's pages: the cursor (the selected step); SEQ held: the track's STEPS (no page change); HOME: the
 *              tempo, 20 .. 300 BPM
 *   PRESETS    SEQ's pages: the tempo (as HOME's SELECT); elsewhere the sounds of the track's category (the 40 factory
 *              sounds first, by name, then the saved ones; past the end the next category), loaded into the track as
 *              it turns
 *   knobs      accelerated: a slow turn a step a detent, a fast one bigger steps (a fast sweep: ~one turn)
 *   OCT- OCT+  the octave (kept in flash; SAVE's pages: back, confirm; MIDI LEARN: cancel, CLEAR ALL); EDIT held: undo, redo (one level: a
 *              recording, a step's edit or clear, STEPS, ERASE, NEW, a sound loaded, SOURCE); both held: CPU PEAK to 0
 *   MASTER     the volume (platform.c)
 *   USB-MIDI   note-on / note-off on a track's channel (SYSTEM's MIDI CHANNEL; ALL > SEL: the selected track), as its
 *              keys (LATCH ON: a note-on toggles); a CC: MIDI LEARN's link; out: the keys' notes, SEQ MIDI OUT's
 *   screen     ui.c, after the concept designs (docs/ui-concept): the boot screen (3 s: the eye opens, the drips run),
 *              HOME's picture moving with the music (the candles, the stars, the eye), then on every page the header
 *              (the button, the page and the track, the page's number or FREEZE), the page's picture and four slabs,
 *              one a knob; SYSTEM 2's last row: CPU NOW, app_audio()'s average over the last ~0.5 s, and PEAK, its
 *              longest block since the reset (the first 2 s after boot left out), as shares of its 5.8 ms; OCT- and
 *              OCT+ show the octave in the header a moment
 *   samples    come in over USB (tools/fm1_sample_upload.py, samples.c), only while nothing sounds
 *   editor     an editor reads and writes the sounds, slots A..D and settings over USB SysEx (editor.c,
 *              docs/EDITOR_PROTOCOL.md), writing only while stopped and silent
 *
 * Rules of the road (the chip has no FPU and no libgcc): integers only, no float, no 64-bit division.
 * tests/host.c runs this file on a computer (the screen as a PNG, the sound as a WAV), web/fm1_web.c in a browser.
 * What platform.c offers: docs/API.md. The chip's rules: docs/RULES.md. */
#define APP_NAME "GR!MM"                        /* the app, the rescue and the crash screen say this */
#include "platform.c"
#include "dsp.c"
#include "wear.c"
#include "samples.c"
#include "voices.c"
#include "grain.c"
#include "hall.c"
#include "tape.c"
#include "tapefx.c"
#include "scale.c"
#include "lfo.c"

/* ---- the sound (runs in the audio ISR: keep it short, no flash, no screen): the voices, the mix, the effects, timed.
 * The time is the block's wall time in fm1_ticks: on the FM-1 the 10 kHz timer ISR (key scan, USB audio) outranks the
 * audio ISR and lands inside it, ~58 times a block, so NOW never reads 0 there */
#define BUDGET_TICKS 139320u                    /* 256 frames at 44.1 kHz: 5805 us, in fm1_ticks (24 a us) */
#define NOW_BLOCKS 86u                          /* NOW: the average over 86 blocks, ~0.5 s */
#define PEAK_AFTER_MS 2000u                     /* PEAK leaves out the first 2 s (boot: cold caches, USB coming up) */
static volatile uint32_t cpu_now, cpu_peak;     /* audio: ticks a block, the last ~0.5 s's average; the longest since the reset */
static volatile uint32_t cpu_reset;             /* main loop: + 1 sets PEAK back to 0 */
static uint32_t cpu_over;                       /* audio: blocks in a row over the budget (the overload guard) */
static int32_t tbuf[NT][VB], dbus[VB], rbus[VB], gbus[VB];   /* the tracks (mono); the DLY, REV and GRAIN buses */
static volatile uint32_t trk_mute, trk_solo;    /* main loop: GLO held + white keys 1..4, 5..8 (bit t: track t) */
enum { PF_REV, PF_ECHO, PF_TAPE, PF_BACK, PF_FDOWN, PF_FUP, PF_STUT, NPF };   /* the FX layer's tricks: G3 .. F4 */
static volatile uint32_t pf_held;               /* main loop: bit PF_: its key held (FX held + it; perform.c) */
static void perform_render(int32_t *out, uint32_t n);
static volatile uint32_t trk_quiet;             /* audio: the tracks silent (muted, or another soloed: mute_watch) */
static volatile uint32_t vis_pk[NT], vis_lr[2]; /* audio: each track's peak (before LEVEL), the output's left and right,
                                                 * the highest since the main loop last took them (HOME's moving
                                                 * picture, ui.c; every 16th frame looked at) */

/* a x b >> 15 for a track's sum (its voices, a chord: past 16 bits, up to ~2^20), b Q15: exact, no 32-bit overflow
 * (mulq15's a * b wraps there: a crackle) */
static inline int32_t mulq15w(int32_t a, int32_t b)
{
    return (a >> 15) * b + (int32_t)(((uint32_t)a & 0x7FFFu) * (uint32_t)b >> 15);
}
static int32_t mix_g[NT][5];                    /* audio: each track's gains (L, R, DLY, REV, GRAIN) as the last block
                                                 * ended: this block's ramps start there */
static void mix_add(int32_t *o, uint32_t st, const int32_t *x, int32_t g0, int32_t g1, uint32_t n)   /* o[i st] +=
                                                 * x[i] times the gain, from g0 to g1 over the block (no zipper) */
{
    uint32_t i;
    int32_t g, dg;
    if (g0 == g1) {
        if (g1)
            for (i = 0; i < n; i++) o[i * st] += mulq15w(x[i], g1);
        return;
    }
    g = g0 << 8, dg = ((g1 - g0) << 8) / (int32_t)n;   /* (8 bits more: small steps) */
    for (i = 0; i < n; i++) {
        g += dg;
        o[i * st] += mulq15w(x[i], g >> 8);
    }
}
/* the track filter (TRACK: CUTOFF, RESONANCE; the LFO's TRK CUTOFF): a low-pass after the track's voices and ENV, before
 * LEVEL, PAN and the sends. dsp.c's SVF (as VA's) at a quarter of the track's scale (a chord's sum: up to ~2^20), the
 * cutoff gliding to where it is turned (~6 ms) with its coefficients every CTL frames, RESONANCE up to a Q of ~2.5, a
 * soft knee after it: no zipper, nothing running away. Open (CUTOFF at the top, the LFO not moving it): bypassed */
static int32_t tf_cs[NT] = {127 << 8, 127 << 8, 127 << 8, 127 << 8}, tf_ic[NT][2];   /* audio: the cutoff now (<< 8),
                                                 * the states */
static uint8_t tf_on[NT];
static void trk_filter(uint32_t t, int32_t *x, uint32_t n)
{
    int32_t tgt = lfo_move(t, LD_CUT, clamp(trk[t].cut, 0, 127), 127) << 8, cs = tf_cs[t], ic1, ic2;
    int32_t r = clamp(trk[t].res, 0, 127) * 7 / 8;
    uint32_t i, j;
    tsvf_t c;
    if (tgt >= 127 << 8 && cs >= (127 << 8) - 64) { /* (open: as it is) */
        tf_cs[t] = 127 << 8, tf_on[t] = 0;
        return;
    }
    if (!tf_on[t]) tf_on[t] = 1, tf_ic[t][0] = 0, tf_ic[t][1] = x[0] >> 2;   /* (from bypassed: no step) */
    ic1 = tf_ic[t][0], ic2 = tf_ic[t][1];
    for (i = 0; i < n; i += CTL) {
        cs += (tgt - cs) >> 3;
        tsvf_coef(&c, cs, r);
        for (j = i; j < i + CTL && j < n; j++) x[j] = soft_knee(tsvf_lp(&c, x[j] >> 2, &ic1, &ic2), 100000) << 2;
    }
    tf_cs[t] = cs, tf_ic[t][0] = ic1, tf_ic[t][1] = ic2;
}
static int32_t fx_send(uint32_t t, int32_t s)   /* a send (0..127) times FX (0..200 %): x 258 its Q15 gain, to ~2x */
{
    return s * 258 * clamp(trk[t].fx, 0, 200) / 100;
}
static void mix_tracks(int32_t *out, uint32_t used, uint32_t n)   /* the sounding tracks into out and the buses (LEVEL,
                                                 * PAN and the sends as the LFO moves them, lfo.c; the sends times
                                                 * TRACK's FX) */
{
    uint32_t t, k;
    for (t = 0; t < NT; t++) {
        int32_t *x = tbuf[t];
        int32_t lv = (trk_quiet >> t & 1u) ? 0 : lfo_move(t, LD_VOLUME, clamp(trk[t].level, 0, 127), 127) * 258;   /* (a
                                                 * silent track: all its gains down, ramped) */
        int32_t pan = lfo_move(t, LD_PAN, clamp(trk[t].pan, 0, 127), 127), g[5];
        g[0] = mulq15(lv, pan <= 64 ? 32767 : (127 - pan) * 520);   /* PAN: the far side down, the near one full */
        g[1] = mulq15(lv, pan >= 64 ? 32767 : pan * 512);
        g[2] = mulq15(lv, pf_held >> PF_ECHO & 1u ? 127 * 258 : fx_send(t, clamp(trk[t].dly, 0, 127)));   /* the sends:
                                                 * after LEVEL (ECHO THROW, REV THROW held: full) */
        g[3] = mulq15(lv, pf_held >> PF_REV & 1u ? 127 * 258 : fx_send(t, lfo_move(t, LD_REV, clamp(trk[t].rev, 0, 127), 127)));
        g[4] = mulq15(lv, fx_send(t, lfo_move(t, LD_GRAIN, clamp(trk[t].grn, 0, 127), 127)));
        if (used >> t & 1u) trk_filter(t, x, n);
        else tf_on[t] = 0;                      /* (silent: from bypassed when it sounds again) */
        if (used >> t & 1u) {
            uint32_t pk = 0, a;
            for (k = 0; k < n; k += 16u)        /* (its peak, roughly: HOME's candle) */
                if ((a = (uint32_t)(x[k] < 0 ? -x[k] : x[k])) > pk) pk = a;
            if (pk > vis_pk[t]) vis_pk[t] = pk;
            mix_add(out, 2u, x, mix_g[t][0], g[0], n);
            mix_add(out + 1, 2u, x, mix_g[t][1], g[1], n);
            mix_add(dbus, 1u, x, mix_g[t][2], g[2], n);
            mix_add(rbus, 1u, x, mix_g[t][3], g[3], n);
            mix_add(gbus, 1u, x, mix_g[t][4], g[4], n);
        }
        for (k = 0; k < 5u; k++) mix_g[t][k] = g[k];
    }
}

static void app_audio(int32_t *out, uint32_t n)
{
    static uint32_t sum, cnt, reset_seen;
    uint32_t t0 = fm1_ticks(), dt, used, i;
    if (n > VB) n = VB;
    used = voices_render(tbuf, n);
    for (i = 0; i < n; i++)
        dbus[i] = rbus[i] = gbus[i] = 0;
    mix_tracks(out, used, n);
    grain_render(gbus, out, rbus, n);           /* GRAIN: the GRAIN bus in; the cloud out, and into the REV bus */
    tape_render(dbus, out, rbus, n);            /* DELAY: the DLY bus in; the echoes out, and into the REV bus */
    hall_render(rbus, out, n);                  /* SPACE: the REV bus in, the reverb out */
    tapefx_render(out, n);                      /* TAPE: all of it, */
    perform_render(out, n);                     /* then the FX layer's tricks, the last stage before MASTER */
    for (i = 0; i < 2u * n; i += 32u) {         /* (the output's peaks, left and right, roughly: HOME's eye) */
        uint32_t l = (uint32_t)(out[i] < 0 ? -out[i] : out[i]), r = (uint32_t)(out[i + 1u] < 0 ? -out[i + 1u] : out[i + 1u]);
        if (l > vis_lr[0]) vis_lr[0] = l;
        if (r > vis_lr[1]) vis_lr[1] = r;
    }
    dt = fm1_ticks() - t0;
    sum += dt;
    if (++cnt == NOW_BLOCKS) {
        cpu_now = sum / NOW_BLOCKS;
        sum = cnt = 0;
    }
    if (reset_seen != cpu_reset) {
        reset_seen = cpu_reset;
        cpu_peak = 0;
    }
    if (fm1_ms >= PEAK_AFTER_MS && dt > cpu_peak)
        cpu_peak = dt;
    cpu_over = dt > BUDGET_TICKS ? cpu_over + 1u : 0u;   /* (ctl_hook: 8 in a row, a voice out) */
}
/* ---- the notes: up to NV on in all, the oldest first, each with its track, its voice and where it came from (the keys or
 * MIDI, or the sequencer). They live in the audio ISR, a control block at a time (ctl_hook), so the sequencer starts and
 * stops its notes on time: the main loop asks through nq (a key, a MIDI note, LATCH, a new source, PLAY / STOP), and the
 * keys' USB-MIDI echoes come back through ec. LATCH ON: a key (a MIDI note-on) starts its note, the same key again
 * stops it; LATCH OFF: a note sounds while its key is held. The track's chord mode ON: a note is its chord's notes
 * (scale.c), each a voice. Every voice keeps where it came from (lat_id: the physical key, the incoming MIDI note, the
 * step's note): its key let go (struck again, LATCH ON) releases exactly those voices, whatever the octave, SCALE or
 * chord are by then; struck again while on, they start over (never a second set on top) */
enum { SRC_KEY, SRC_SEQ, SRC_ARP };              /* (SRC_ARP: arp.c's notes, no chord of their own) */
#define KEY_ID(k) (k)                           /* lat_id of a physical key (0 = F3 .. 26), */
#define MIDI_ID(n) (32u + (n))                  /* of a MIDI note in (its number as it came: 32 .. 159); SEQ: the note */
static uint8_t lat_note[NV], lat_root[NV], lat_id[NV], lat_trk[NV], lat_voice[NV], lat_src[NV];   /* audio (the
                                                 * main loop reads them to light keys); lat_root: the note before the
                                                 * chord (its USB-MIDI echo) */
static volatile uint32_t nlat;
static uint32_t sel;                            /* main loop: the selected track (ALGORITHM) */
static uint16_t key_held[27];                   /* main loop, LATCH OFF: the track (<< 8, + 0x8000) a key holds */
static uint8_t midi_held[128];                  /* .. the tracks a MIDI note holds (bit t) */
static uint16_t echo_on[128];                   /* main loop: the notes on out there over USB-MIDI (bit: the channel),
                                                 * sent and not yet off */

enum { NQ_ON, NQ_OFF, NQ_TOGGLE, NQ_RELEASE, NQ_RESTART, NQ_PLAY, NQ_STOP, NQ_SONG };   /* (PLAY, SONG: the slot in
                                                 * use as the note; SONG: song record starts (id 1) or ends (0)) */
#define NQ_LEN 64u
#define NQ_ECHO 0x80u                           /* (with a command: echo it over USB-MIDI) */
static volatile uint32_t nq[NQ_LEN];            /* an ask in one word (one store: never seen half-written): the
                                                 * command, the track << 8, the note << 16, its origin (lat_id) << 24 */
#define EC_LEN 128u
#define EC_SEQ 0x80u                            /* (with an echo's note: a step's, an arp's note: SEQ MIDI OUT) */
static volatile uint8_t ec[EC_LEN][2];          /* an echo: its status (0x90, 0x80) + the track; its note (+ EC_SEQ) */
static volatile uint32_t nq_w, nq_r, ec_w, ec_r; /* nq: the main loop writes, the ISR reads; ec (the echoes): the
                                                 * other way round. (Not mo_w / mo_r: usb.c's MIDI-out ring has those
                                                 * names, one file on the FM-1: they were one variable) */
static volatile uint32_t ec_lost, mo_seq;       /* audio: an echo found ec full; main loop: SEQ MIDI OUT on (SYSTEM 2) */
static void nq_note(uint32_t c, uint32_t t, uint32_t note, uint32_t id)   /* main loop: an ask for the audio ISR;
                                                 * full (64 asks within a control block), it takes the oldest's place:
                                                 * the ISR then skips that one (ctl_hook) */
{
    uint32_t w = nq_w;
    nq[w % NQ_LEN] = (c & 0xFFu) | (t & 3u) << 8 | (note & 127u) << 16 | (id & 0xFFu) << 24;
    nq_w = w + 1u;
}
static void nq_put(uint32_t c, uint32_t t, uint32_t note) { nq_note(c, t, note, 0); }   /* (RELEASE, RESTART, ..) */
static void ec_put(uint32_t st, uint32_t t, uint32_t note)   /* audio: a USB-MIDI echo of track t for the main loop
                                                 * to send (on t's channel); full: said (the main loop then lets go of
                                                 * what it sent, never a note left on out there) */
{
    uint32_t w = ec_w;
    if (w - ec_r >= EC_LEN) { ec_lost = 1; return; }
    ec[w % EC_LEN][0] = (uint8_t)(st | (t & 3u)), ec[w % EC_LEN][1] = (uint8_t)note;
    ec_w = w + 1u;
}

static int voice_latched(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < nlat; i++)
        if (lat_voice[i] == v) return 1;
    return 0;
}
static uint32_t free_voice(uint32_t t, uint32_t note)   /* no note on on it: one still sounding this note of this
                                                         * track, else an idle one, else the quietest release (there is
                                                         * one: nlat < NV) */
{
    uint32_t v, best = 0;
    int32_t lv = 0x7FFFFFFF;
    for (v = 0; v < NV; v++)
        if (!voice_latched(v) && !voice_idle(v) && vc[v].note == note && vc[v].trk == t) return v;
    for (v = 0; v < NV; v++) {
        if (voice_latched(v)) continue;
        if (voice_idle(v)) return v;
        if (voice_level(v) < lv) { lv = voice_level(v); best = v; }
    }
    return best;
}
/* SEQ MIDI OUT: a step's (an arp's) note goes out when it is put in the list and when it leaves it (STOP, a mute, the
 * 7th note, the overload guard: every way out is drop), once while the track has it on (a chord's, the arp's twice) */
static int out_twin(uint32_t i, uint32_t t, uint32_t note)   /* another (not i) step's or arp's note of t on note */
{
    uint32_t j;
    for (j = 0; j < nlat; j++)
        if (j != i && lat_src[j] != SRC_KEY && lat_trk[j] == t && lat_note[j] == note) return 1;
    return 0;
}
static void drop(uint32_t i)                    /* note i out of the list */
{
    uint32_t n = nlat - 1u;
    if (mo_seq && lat_src[i] != SRC_KEY && !out_twin(i, lat_trk[i], lat_note[i]))
        ec_put(0x80, lat_trk[i], lat_note[i] | EC_SEQ);
    for (; i < n; i++) {
        lat_note[i] = lat_note[i + 1u];
        lat_root[i] = lat_root[i + 1u];
        lat_id[i] = lat_id[i + 1u];
        lat_trk[i] = lat_trk[i + 1u];
        lat_voice[i] = lat_voice[i + 1u];
        lat_src[i] = lat_src[i + 1u];
    }
    nlat = n;
}
static int note_find(uint32_t t, uint32_t id, uint32_t src)   /* the place in the list of one of the voices of origin
                                                 * id, or -1 */
{
    uint32_t i;
    for (i = 0; i < nlat; i++)
        if (lat_id[i] == id && lat_trk[i] == t && lat_src[i] == src) return (int)i;
    return -1;
}
static uint32_t drop_oldest(void)               /* the oldest note out of the list (its track forgets it): its voice */
{
    uint32_t t0 = lat_trk[0], r0 = lat_root[0], i0 = lat_id[0], s0 = lat_src[0], v = lat_voice[0];
    drop(0);
    if (s0 == SRC_KEY && note_find(t0, i0, SRC_KEY) < 0) ec_put(0x80, t0, r0);   /* (its chord's last) */
    return v;
}
static void note_off(uint32_t t, uint32_t id, uint32_t src, int echo)   /* origin id's voices released (its chord) */
{
    int i = note_find(t, id, src);
    if (i < 0) return;
    if (echo) ec_put(0x80, t, lat_root[i]);     /* (the note its on was echoed with) */
    while ((i = note_find(t, id, src)) >= 0) {
        voice_release(lat_voice[i]);
        drop((uint32_t)i);
    }
}
static void note_on(uint32_t t, uint32_t id, uint32_t note, uint32_t src, int echo, int32_t ch)   /* origin id plays
                                                 * note (ch -1: the track's chord, its chord mode ON; 0: the note
                                                 * alone; a chord step's byte: that chord); sounding already: its own
                                                 * voices start over on it */
{
    uint8_t ns[CH_MAX], own[CH_MAX];
    uint32_t v, i, n, no = 0;
    int j;
    if (note > 127u || t >= NT || (trk_quiet >> t & 1u)) return;   /* (a silent track: none) */
    while ((j = note_find(t, id, src)) >= 0) {  /* (struck again: its voices out of the list, to be used again) */
        if (echo && no == 0u) ec_put(0x80, t, lat_root[j]);
        if (no < CH_MAX) own[no++] = lat_voice[j];
        else voice_release(lat_voice[j]);
        drop((uint32_t)j);
    }
    ns[0] = (uint8_t)note, n = 1;
    if (ch < 0 && trk[t].chord) n = chord_notes(t, note, ns);
    else if (ch > 0) n = chord_notes_tv(ch & 15, ch >> 4 & 7, note, ns);
    for (i = 0; i < n; i++) {
        if (i < no)                             /* its own voice again */
            v = own[i];
        else if (nlat == NV)                    /* all voices on: the oldest note goes, its voice fades and takes
                                                 * the new one */
            v = drop_oldest();
        else
            v = free_voice(t, ns[i]);
        voice_start(v, t, ns[i]);
        if (mo_seq && src != SRC_KEY && !out_twin(NV, t, ns[i])) ec_put(0x90, t, ns[i] | EC_SEQ);
        lat_note[nlat] = ns[i];
        lat_root[nlat] = (uint8_t)note;
        lat_id[nlat] = (uint8_t)id;
        lat_trk[nlat] = (uint8_t)t;
        lat_voice[nlat] = (uint8_t)v;
        lat_src[nlat] = (uint8_t)src;
        nlat = nlat + 1u;
    }
    for (; i < no; i++) voice_release(own[i]);  /* (a smaller chord now: the rest let go) */
    if (echo && n) ec_put(0x90, t, note);
}
static uint32_t tracks_latched(void)            /* bit t: track t has notes on */
{
    uint32_t i, m = 0;
    for (i = 0; i < nlat; i++)
        m |= 1u << lat_trk[i];
    return m;
}
static int voices_silent(void)                  /* no voice sounds: no note on (latched ones too), no release */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (!voice_idle(v)) return 0;
    return 1;
}
static int quiet(void)                          /* nothing sounds, not even a release or an effect's tail */
{
    return voices_silent() && tape_idle() && hall_idle() && grain_idle();   /* (TAPE's hiss is not a sound that
                                                 * waits: it stops a moment) */
}
/* audio: the overload guard. Blocks that run over their time (6 voices of the heaviest sources, chords, all the
 * effects) leave the main loop no time: the screen stops, then the watchdog restarts the FM-1. Eight blocks in a row
 * over their time (~46 ms behind, not a block near it: the key scan's interrupt counts in a block's time, the FX
 * layer's glowing keys too, and a near one is no harm): the oldest note (none: a release) fades out in ~10 ms; then
 * ~4 blocks' wait */
static void shed_voice(void)
{
    static uint32_t hold;
    uint32_t v = 0;
    if (hold) { hold--; return; }
    if (cpu_over < 8u) return;
    if (nlat) v = drop_oldest();
    else
        while (v < NV && (voice_idle(v) || vc[v].stage == ST_FADE)) v++;
    if (v < NV) voice_fade(v);
    hold = 4u * (VB / CTL);                     /* (ctl_hook runs a control step: 8 a block) */
}

#include "seq.c"                                /* the sequencer (its notes: note_on, note_off, SRC_SEQ) */

static void release_keys(uint32_t t)            /* audio: LATCH ON -> OFF: the track's key notes released */
{
    uint32_t i;
    for (i = nlat; i-- > 0;)                    /* (a chord: all its notes go at once) */
        if (i < nlat && lat_trk[i] == t && lat_src[i] == SRC_KEY) note_off(t, lat_id[i], SRC_KEY, 1);
}
static void restart_track(uint32_t t)           /* audio: a new source: the track's notes go over (a fade, a new attack) */
{
    uint32_t i;
    for (i = 0; i < nlat; i++)
        if (lat_trk[i] == t) voice_start(lat_voice[i], t, lat_note[i]);
}

#include "arp.c"                                /* the arpeggiator (its notes: note_on, note_off, SRC_ARP) */

/* audio: a key's (a MIDI note's) ask, on its track: ARP on, into or out of the arp's list; else its own notes */
static void key_ask(uint32_t c, uint32_t t, uint32_t id, uint32_t note, int echo)
{
    if (trk[t].arp) {
        if (c == NQ_ON || (c == NQ_TOGGLE && !arp_has(t, id))) arp_add(t, id, note, echo);
        else if (arp_has(t, id)) arp_take(t, id, echo);
        return;
    }
    if (c == NQ_ON || (c == NQ_TOGGLE && note_find(t, id, SRC_KEY) < 0)) note_on(t, id, note, SRC_KEY, echo, -1);
    else note_off(t, id, SRC_KEY, echo);        /* (OFF; TOGGLE: on already) */
}

/* audio: the tracks muted (or not soloed) now: their notes out (released and faded in ~10 ms), new ones not started
 * (note_on), their sound and sends down (mix_tracks); its sequence, its arp play on unheard. Back: from the next note */
static void mute_watch(void)
{
    uint32_t q = (trk_mute | ((trk_solo & 15u) ? ~trk_solo : 0u)) & 15u, n = q & ~trk_quiet, t, v, i;
    trk_quiet = q;
    for (t = 0; t < NT; t++)
        if (n >> t & 1u) {
            for (i = nlat; i-- > 0;)
                if (i < nlat && lat_trk[i] == t) note_off(t, lat_id[i], lat_src[i], lat_src[i] == SRC_KEY);
            for (v = 0; v < NV; v++)
                if (vc[v].trk == t && !voice_idle(v)) voice_fade(v);
        }
}

/* audio, a control block: the mutes, the LFOs, ARP switched, the main loop's asks, then the sequencer and the arps
 * (voices.c calls it before the voices) */
static void ctl_hook(void)
{
    mute_watch();
    lfo_ctl();
    arp_watch();
    if (nq_w - nq_r > NQ_LEN) nq_r = nq_w - NQ_LEN;   /* (overrun: the oldest asks are gone) */
    while (nq_r != nq_w) {
        uint32_t r = nq_r, q = nq[r % NQ_LEN], c = q & 0xFFu, t = q >> 8 & 3u, note = q >> 16 & 127u, id = q >> 24;
        int echo = (c & NQ_ECHO) != 0;
        c &= ~NQ_ECHO;
        switch (c) {
        case NQ_ON: case NQ_OFF: case NQ_TOGGLE: key_ask(c, t, id, note, echo); break;   /* (TOGGLE: LATCH ON) */
        case NQ_RELEASE:                        /* LATCH ON -> OFF: the track's key notes released (its arp emptied) */
            release_keys(t);
            if (trk[t].arp) arp_clear(t);
            break;
        case NQ_RESTART: restart_track(t); break;   /* a new source: the track's notes go over */
        case NQ_PLAY: seq_start(note & 3u); break;
        case NQ_SONG:                           /* song record, playing: from now, or ended */
            if (id) song_rec_begin(note & 3u);
            else song_stop();
            break;
        default: seq_stop(); break;
        }
        nq_r = r + 1u;
    }
    shed_voice();
    seq_ctl();
    arp_ctl();
}

#include "perform.c"                            /* the FX layer: the tricks on the output (FX held + G3 .. F4) */
#include "store.c"                              /* SOUNDS, the working project (autosave), slots A..D, undo (store.c) */

/* main loop: SAVE's pages and its held layer, PRESETS, undo, the header's message for a moment */
static volatile int32_t sv_slot, sv_cat, sv_type, sv_act, pj_slot, pj_act;   /* SOUNDS: SLOT, CATEGORY, TYPE (0 ALL,
                                                 * then a source + 1), ACTION; PROJECTS: SLOT (A..D), ACTION */
static const char *const SV_ACT[3] = {"LOAD", "SAVE", "CLEAR"};
static const char *const PJ_ACT[4] = {"LOAD", "SAVE", "ERASE", "NEW"};
static int32_t brw_cat[NT], brw_pos[NT] = {-1, -1, -1, -1};   /* PRESETS: a track's CATEGORY, its place in its list */
static char trk_nm[NT][16];                     /* a track's sound's name from the editor (TRACK SET), "" none; */
static int32_t trk_nm_e[NT];                    /* .. the source it is for */
static char *trk_sname(uint32_t t, char *s)     /* the name HOME shows for track t's sound (the editor's, or the one
                                                 * PRESETS or SOUNDS loaded), while its source is the sound's; "" none */
{
    const uint8_t *r = brw_pos[t] >= 0 ? cat_item((uint32_t)brw_cat[t] % NCAT, (uint32_t)brw_pos[t]) : 0;
    *s = 0;
    if (trk_nm[t][0] && trk_nm_e[t] == trk[t].eng) return put_s(s, trk_nm[t]);
    return r && r[3] == (uint32_t)trk[t].eng ? snd_rec_name(r, s) : s;
}
static void brw_forget(void)                    /* the tracks' sounds another's now (a slot, NEW): no names */
{
    uint32_t t;
    for (t = 0; t < NT; t++) brw_pos[t] = -1, trk_nm[t][0] = 0;
}
static uint32_t playing, save_held, glo_held;   /* the sequencer runs (PLAY); SAVE, GLO held (their layers on the keys) */
static uint32_t rec_on;                         /* REC (app_main's rec, for the stress test's checks) */
static uint32_t sel_held;                       /* SEL held (SEQ's pages: the knobs lock the selected step, ui.c) */
/* SYSTEM, SYSTEM 2: the FM-1's own settings, not the project's: kept in the settings sector (platform.c) with the
 * octave, loaded at power-on, never touched by a project or slot load, NEW or undo; saved 2 s after a change, when
 * silent and stopped (an erase stops the sound ~50 ms) */
#define NLINK 32u                               /* MIDI LEARN: links at most */
#define MCH_ALL 13                              /* MIDI CHANNEL: 0 "1-4" (T1 on channel 1) .. 12 "13-16", 13 ALL > SEL */
enum { LEARN_OFF, LEARN_ON, LEARN_CLEAR };
static struct {
    int8_t oct, tune;                           /* the octave; TUNE, cents */
    uint8_t col, mch, scr, out;                 /* COLOUR, MIDI CHANNEL, SCREEN OFF, SEQ MIDI OUT */
    uint8_t nlk, lk[NLINK][5];                  /* MIDI LEARN's links, the oldest first: the CC, the page, the knob, the
                                                 * track (a track's page), x (SOUND: the source; GRID: the step; SONG:
                                                 * the section) */
    uint8_t ver;                                /* SET_VER (steps 20..24 saved without it: their links moved, sys_load) */
} set;
#define SET_VER 1u
static uint32_t set_dirty, set_t;               /* the settings changed, when */
static volatile int32_t sy_col, sy_mch, sy_tune, sy_scr, sy_learn, sy_out, sy_fac;   /* SYSTEM's, SYSTEM 2's knobs
                                                 * (sy_fac: SAMPLES, 1 RESTORE: OCT+ writes the factory ones) */
static uint32_t fac_at;                         /* RESTORE confirmed: when it writes (the header said first), 0 none */
static uint32_t learn_st;                       /* MIDI LEARN: 0 none; 1 TURN A KNOB; 2 MOVE A CC (learn_to: the knob) */
static uint8_t learn_to[4];                     /* (the page, the knob, the track, x) */
static uint32_t scr_dark, scr_woke, t_hand;     /* SCREEN OFF: dark; a touch woke it (this pass); the hands' last touch */
static const uint16_t SCR_S[5] = {0, 60, 300, 900, 1800};   /* SCREEN OFF: NEVER, 1, 5, 15, 30 min (s) */
static char *link_name(char *s, const uint8_t *L);
static char hd_msg[24];                         /* (ui.c: in the header's place until hd_until) */
static int32_t bat_lvl = -1;                    /* the battery's level (batt_poll; ui.c's header) */
static uint32_t hd_until, as_err;               /* (as_err: an autosave failed, it is tried again) */
static uint32_t as_wait, sv_wait;               /* an autosave waits for the voices to fall silent; that, or a SOUNDS
                                                 * SAVE / CLEAR, waits (ui.c: SAVES WHEN SILENT) */
static int32_t sv_pend = -1;                    /* SOUNDS: a SAVE or CLEAR waiting for the sequencer to stop (its ACTION) */
static uint32_t sv_pend_slot, sv_pend_cat, sv_pend_trk;
static int32_t ow_slot = -1;                    /* a used slot asked to be saved into: the same again within 3 s */
static uint32_t ow_until;
enum { UK_STEP = 1u << 24, UK_LEN = 2u << 24, UK_SRC = 3u << 24, UK_SND = 4u << 24, UK_ONE = 5u << 24,
       UK_SONG = 6u << 24 };                    /* undo keys (UK_SONG: a song record, its chain undone too) */
static uint32_t uk_n;                           /* (UK_ONE + a count: an action of its own) */
static int32_t ch_last = -1;                    /* SEL's picture: the note last played (a key, MIDI), its chord shown */

/* ---- the screen: plain text, a row per value; only what changed is drawn */
#define KEY0 53u                                /* key 0 = F3 */
#define OCT_BOTH (1u << BTN_OCT_DOWN | 1u << BTN_OCT_UP)
#define HOLD_MS 600u                            /* a button held this long: its hold function (HOME: SYSTEM) */

static uint32_t pct10(uint32_t ticks)           /* ticks a block -> tenths of a % of the budget, at most 409.5 % */
{
    if (ticks > 570000u) ticks = 570000u;       /* (ticks * 1000 stays in 32 bits) */
    ticks = (ticks * 1000u + BUDGET_TICKS / 2u) / BUDGET_TICKS;
    return ticks > 4095u ? 4095u : ticks;
}
static char *put_x10(char *s, uint32_t t)       /* tenths: "12.3" */
{
    s = put_u(s, t / 10u);
    *s++ = '.';
    *s++ = (char)('0' + t % 10u);
    *s = 0;
    return s;
}
static char *put_pc(char *s, int32_t v) { return put_s(put_u(s, ((uint32_t)clamp(v, 0, 127) * 100u + 63u) / 127u), "%"); }
static char *put_ms(char *s, uint32_t q16)      /* a time, ms in Q16: "5 ms", "120 ms", "1.5 s", "12 s" */
{
    uint32_t ms = (q16 + 32768u) >> 16;
    if (ms < 995u) return put_s(put_u(s, ms), " ms");
    if (ms < 10000u) return put_s(put_x10(s, (ms + 50u) / 100u), " s");
    return put_s(put_u(s, (ms + 500u) / 1000u), " s");
}

/* the pages (the control map): each with its button, its knobs' names, built or not, the track's own or not. A button's
 * tap opens its first page, again the next (round); SYSTEM is HOME's, held */
enum { PG_SOUND, PG_SOURCE, PG_ENV, PG_LFO, PG_SCALE, PG_CHORD, PG_SONG, PG_HOME, PG_MIX1, PG_MIX2, PG_SYSTEM,
       PG_SYS2, PG_SOUNDS, PG_PROJECTS, PG_ARP, PG_GRID, PG_PATTERN, PG_TAPE, PG_GRAIN, PG_DELAY, PG_SPACE, PG_PERFORM,
       PG_TRACK, PG_FMALGO, PG_FMOP, NPAGE };   /* (the newer last: MIDI LEARN's links keep the pages' numbers they
                                                 * have; in their button's round by pg_key. TRACK: gone in step 25, its
                                                 * number kept: sys_load moves its links onto HOME) */
typedef struct {
    const char *name;
    uint8_t btn, built, own, cyc;               /* its button (BTN_); built; the track's own (the header names it); in
                                                 * the button's round */
    const char *knob[4];
} page_t;
static const page_t PAGES[NPAGE] = {
    {"SOUND", BTN_EDIT, 1, 1, 1, {"", "", "", ""}},   /* (the source's names) */
    {"SOURCE", BTN_EDIT, 1, 1, 1, {"SOURCE", "LATCH", "", ""}},
    {"ENV", BTN_ENV, 1, 1, 1, {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"}},
    {"LFO", BTN_LFO, 1, 1, 1, {"RATE", "DEPTH", "DESTINATION", "SHAPE"}},
    {"SCALE", BTN_SEL, 1, 0, 1, {"ROOT", "SCALE", "", ""}},
    {"CHORD", BTN_SEL, 1, 0, 1, {"CHORD TYPE", "VOICING", "", ""}},
    {"SONG", BTN_GLO, 1, 0, 1, {"SECTION", "PATTERN", "REPEATS", "MODE"}},
    {"HOME", BTN_HOME, 1, 1, 1, {"VOLUME", "CUTOFF", "RESONANCE", "FX"}},   /* (the track's: its LEVEL, its filter, its
                                                 * sends together; its sound's name in the header) */
    {"MIXER 1", BTN_HOME, 1, 1, 1, {"LEVEL", "DLY SEND", "REV SEND", "GRAIN SEND"}},
    {"MIXER 2", BTN_HOME, 1, 1, 1, {"PAN", "", "", ""}},
    {"SYSTEM", BTN_HOME, 1, 0, 0, {"COLOUR", "MIDI CHANNEL", "TUNE", "SCREEN OFF"}},   /* (HOME held; HOME again: */
    {"SYSTEM 2", BTN_HOME, 1, 0, 0, {"MIDI LEARN", "SEQ MIDI OUT", "SAMPLES", ""}},       /* SYSTEM 2, and back) */
    {"SOUNDS", BTN_SAVE, 1, 0, 1, {"SLOT", "CATEGORY", "TYPE", "ACTION"}},
    {"PROJECTS", BTN_SAVE, 1, 0, 1, {"SLOT", "CATEGORY", "TYPE", "ACTION"}},
    {"ARP", BTN_ARP, 1, 1, 1, {"MODE", "RATE", "OCTAVES", "GATE"}},
    {"GRID", BTN_SEQ, 1, 1, 1, {"NOTE", "CHANCE", "RATCHET", "LENGTH"}},
    {"PATTERN SETTINGS", BTN_SEQ, 1, 1, 1, {"STEPS", "DIVISION", "SWING", "PLAY MODE"}},
    {"TAPE", BTN_FX, 1, 0, 1, {"WOW", "SATURATION", "TONE", "HISS"}},
    {"GRAIN", BTN_FX, 1, 0, 1, {"SIZE", "DENSITY", "SCATTER", "MIX"}},
    {"DELAY", BTN_FX, 1, 0, 1, {"TIME", "FEEDBACK", "WEAR", "MIX"}},
    {"SPACE", BTN_FX, 1, 0, 1, {"SIZE", "DAMP", "MIX", "PRE-DELAY"}},
    {"PERFORM", BTN_FX, 1, 0, 0, {"FILT", "REV", "ECHO", "PITCH"}},   /* (FX held ~0.3 s: the tricks; its slabs show
                                                 * them, its knobs do nothing) */
    {"TRACK", BTN_HOME, 0, 1, 0, {"", "", "", ""}},   /* (gone: its knobs on HOME) */
    {"FM ALGO", BTN_EDIT, 1, 1, 1, {"ALGO", "FEEDBACK", "OP", "FINE"}},   /* (an FM track's: its own patch, fm.c) */
    {"FM OP", BTN_EDIT, 1, 1, 1, {"RATIO", "LEVEL", "ATTACK", "DECAY"}},  /* (the operator OP selects) */
};
static uint32_t pg_key(uint32_t p)              /* its place in its button's round */
{
    return p == PG_FMALGO ? PG_SOUND * 4u + 1u : p == PG_FMOP ? PG_SOUND * 4u + 2u : p * 4u;
}
static int pg_on(uint32_t p)                    /* in its button's round now (FM ALGO, FM OP: an FM track's only) */
{
    return PAGES[p].cyc && ((p != PG_FMALGO && p != PG_FMOP) || clamp(trk[sel].eng, 0, NENG - 1) == ENG_FM);
}
static volatile int32_t fm_op;                  /* FM ALGO's OP: the operator FINE and FM OP's knobs edit (0 = OP1) */
static const char *const BTN_NAME[14] = {"OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP",
                                         "SEQ", "PLAY", "REC"};
#define BTN_PAGES (1u << BTN_EDIT | 1u << BTN_ENV | 1u << BTN_LFO | 1u << BTN_SEL | 1u << BTN_GLO | 1u << BTN_HOME | \
                   1u << BTN_SAVE | 1u << BTN_ARP | 1u << BTN_SEQ | 1u << BTN_FX)   /* the buttons with pages */
#define BTN_HOLDS (1u << BTN_EDIT | 1u << BTN_SEL | 1u << BTN_GLO | 1u << BTN_HOME | 1u << BTN_SAVE | 1u << BTN_ARP | 1u << BTN_SEQ | \
                   1u << BTN_FX)                /* .. with a hold function: their page changes when let go */
static uint32_t page = PG_HOME;

static uint32_t next_page(uint32_t b)           /* button b tapped: its first page, or (on one of its) the next */
{
    uint32_t p, first = NPAGE, nx = NPAGE, on = PAGES[page].btn == b && pg_on(page);
    if (b == BTN_HOME && (page == PG_SYSTEM || page == PG_SYS2)) return page == PG_SYSTEM ? PG_SYS2 : PG_SYSTEM;
    for (p = 0; p < NPAGE; p++) {
        if (PAGES[p].btn != b || !pg_on(p)) continue;
        if (first == NPAGE || pg_key(p) < pg_key(first)) first = p;
        if (on && pg_key(p) > pg_key(page) && (nx == NPAGE || pg_key(p) < pg_key(nx))) nx = p;
    }
    return nx != NPAGE ? nx : first;
}

static uint32_t sel_eng(void) { return (uint32_t)clamp(trk[sel].eng, 0, NENG - 1); }
static const engine_t *sel_engine(void) { return ENGINES[sel_eng()]; }
static volatile int32_t *knob_param(uint32_t pg, uint32_t k)   /* what KNOB k + 1 turns on page pg (0: nothing) */
{
    static volatile int32_t *const FX[4][4] = {{&tf_wow, &tf_sat, &tf_tone, &tf_hiss},
                                               {&ge_size, &ge_dens, &ge_scat, &ge_mix},
                                               {&dl_time, &dl_fb, &dl_wear, &dl_mix},
                                               {&rv_size, &rv_damp, &rv_mix, &rv_pre}};
    volatile int32_t *const MIX[4] = {&trk[sel].level, &trk[sel].dly, &trk[sel].rev, &trk[sel].grn};
    switch (pg) {
    case PG_SOUND: return &trk[sel].p[sel_eng()][k];
    case PG_SOURCE: return k == 0u ? &trk[sel].eng : k == 1u ? &trk[sel].latch : 0;
    case PG_ENV: return &trk[sel].env[k];
    case PG_SONG: return k == 0u ? &song_sect : k == 1u ? &song_pat[clamp(song_sect, 0, SONG_N - 1)] :
                         k == 2u ? &song_rep[clamp(song_sect, 0, SONG_N - 1)] : &song_mode;
    case PG_LFO: return k == 0u ? &trk[sel].lfo_rate : k == 1u ? &trk[sel].lfo_depth : k == 2u ? &trk[sel].lfo_dest :
                        &trk[sel].lfo_shape;
    case PG_SCALE: return k == 0u ? &sc_root : k == 1u ? &sc_scale : 0;
    case PG_CHORD: return k == 0u ? &trk[sel].ch_type : k == 1u ? &trk[sel].ch_voice : 0;
    case PG_ARP: return k == 0u ? &trk[sel].arp_mode : k == 1u ? &trk[sel].arp_rate : k == 2u ? &trk[sel].arp_oct :
                        &trk[sel].arp_gate;
    case PG_HOME: return k == 0u ? &trk[sel].level : k == 1u ? &trk[sel].cut : k == 2u ? &trk[sel].res : &trk[sel].fx;
    case PG_MIX1: return MIX[k];
    case PG_MIX2: return k ? 0 : &trk[sel].pan;
    case PG_FMALGO: return k == 0u ? &trk[sel].fm_alg : k == 1u ? &trk[sel].fm_fb : k == 2u ? &fm_op :
                           &trk[sel].fmo[clamp(fm_op, 0, 5)][FO_FINE];
    case PG_FMOP: return &trk[sel].fmo[clamp(fm_op, 0, 5)][k == 0u ? FO_RATIO : k == 1u ? FO_LEVEL : k == 2u ? FO_ATK : FO_DEC];
    case PG_TAPE: case PG_GRAIN: case PG_DELAY: case PG_SPACE: return FX[pg - PG_TAPE][k];
    case PG_GRID: {                             /* the selected step of the track's pattern */
        seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
        return k == 0u ? &st->note : k == 1u ? &st->chance : k == 2u ? &st->ratchet : &st->len;
    }
    case PG_PATTERN: return k == 0u ? &seq[sel].steps : k == 1u ? &seq[sel].div : k == 2u ? &seq[sel].swing : &seq[sel].mode;
    case PG_SOUNDS: return k == 0u ? &sv_slot : k == 1u ? &sv_cat : k == 2u ? &sv_type : &sv_act;
    case PG_PROJECTS: return k == 0u ? &pj_slot : k == 3u ? &pj_act : 0;   /* (CATEGORY, TYPE: "-") */
    case PG_SYSTEM: return k == 0u ? &sy_col : k == 1u ? &sy_mch : k == 2u ? &sy_tune : &sy_scr;
    case PG_SYS2: return k == 0u ? &sy_learn : k == 1u ? &sy_out : k == 2u ? &sy_fac : 0;
    default: return 0;                          /* (not built yet: the knobs do nothing) */
    }
}
static const char *knob_name(uint32_t pg, uint32_t k) { return pg == PG_SOUND ? sel_engine()->knob[k] : PAGES[pg].knob[k]; }
static int32_t knob_max(uint32_t pg, uint32_t k)
{
    static const int32_t GRID_MAX[4] = {127, 100, 4, SEQ_N}, PATT_MAX[4] = {SEQ_N, 7, 127, SEQ_NMODE - 1};
    return pg == PG_SOUND ? sel_engine()->max[k] : pg == PG_SOURCE ? (k ? 1 : (int32_t)NENG - 1) :
           pg == PG_GRID ? GRID_MAX[k] : pg == PG_PATTERN ? PATT_MAX[k] :
           pg == PG_SOUNDS ? (k == 0u ? SND_N - 1 : k == 1u ? NCAT - 1 : k == 2u ? (int32_t)NENG : 2) :
           pg == PG_PROJECTS ? 3 : pg == PG_SCALE ? (k ? NSCALE - 1 : 11) : pg == PG_CHORD ? (k ? NVOICING - 1 : NCHORD - 1) :
           pg == PG_ARP ? (k == 0u ? NARPMODE - 1 : k == 1u ? NARPRATE - 1 : k == 2u ? 4 : 100) :
           pg == PG_LFO ? (k == 0u ? 127 : k == 1u ? 100 : k == 2u ? NLFODEST - 1 : NLFOSHAPE - 1) :
           pg == PG_SONG ? (k == 0u ? SONG_N - 1 : k == 1u ? SONG_END : k == 2u ? 16 : NSONGMODE - 1) :
           pg == PG_SYSTEM ? (k == 0u ? 3 : k == 1u ? MCH_ALL : k == 2u ? 50 : 4) : pg == PG_SYS2 ? (k ? 1 : 2) :
           pg == PG_HOME && k == 3u ? 200 : pg == PG_FMALGO ? (k == 0u ? 31 : k == 1u ? 7 : k == 2u ? 5 : 99) :
           pg == PG_FMOP ? (k ? 99 : 31) : 127;
}
static int32_t knob_min(uint32_t pg, uint32_t k)   /* (RATCHET, LENGTH, STEPS, OCTAVES: from 1; GATE from 10 %) */
{
    if (pg == PG_ARP && k >= 2u) return k == 2u ? 1 : 10;
    if (pg == PG_SONG && k == 2u) return 1;     /* (REPEATS from 1) */
    if (pg == PG_SYSTEM && k == 2u) return -50; /* (TUNE: -50 .. +50 cents) */
    return (pg == PG_GRID && k >= 2u) || (pg == PG_PATTERN && k == 0u);
}
static char *lfo_rate_text(char *s, int32_t v, const char *sp)   /* RATE: "20 S", "2.5 S" a cycle; "4.0 HZ" (sp: " ",
                                                 * or "" on a slab) */
{
    uint32_t q = lfo_hz_q16(v), ms = 3932160000u / q, f10 = q / 393216u;   /* (60 s x 65536 / q; 10 / 60 x q / 65536) */
    if (ms >= 9950u) return put_s(put_s(put_u(s, (ms + 500u) / 1000u), sp), "S");
    if (ms >= 995u) return put_s(put_s(put_x10(s, (ms + 50u) / 100u), sp), "S");
    return put_s(put_s(put_x10(s, f10), sp), "HZ");
}
static const char *lfo_dest_name(int32_t d)     /* DESTINATION: its name (a knob: the track's source's now) */
{
    d = clamp(d, 0, NLFODEST - 1);
    return d >= LD_K1 && d <= LD_K4 ? knob_name(PG_SOUND, (uint32_t)(d - LD_K1)) : LFO_DEST_NAME[d];
}
static const char *const COL_NAME[4] = {"RED", "AMBER", "TEAL", "VIOLET"};   /* SYSTEM: COLOUR */
static const char *const SCR_NAME[5] = {"NEVER", "1 MIN", "5 MIN", "15 MIN", "30 MIN"};   /* SCREEN OFF */
static const char *const LEARN_NAME[3] = {"OFF", "LEARN", "CLEAR ALL"};   /* SYSTEM 2: MIDI LEARN */
static char *mch_text(char *s, int32_t v)       /* MIDI CHANNEL: "1-4" .. "13-16", "ALL > SEL" */
{
    if (v >= MCH_ALL) return put_s(s, "ALL > SEL");
    return put_u(put_s(put_u(s, (uint32_t)v + 1u), "-"), (uint32_t)v + 4u);
}
static char *tune_text(char *s, int32_t v)      /* TUNE: "+00", "-12" */
{
    s[0] = v < 0 ? '-' : '+', v = v < 0 ? -v : v;
    s[1] = (char)('0' + v / 10 % 10), s[2] = (char)('0' + v % 10), s[3] = 0;
    return s + 3;
}
static void knob_text(uint32_t pg, uint32_t k, char *s)   /* knob k's value on page pg, as the screen shows it */
{
    volatile int32_t *p = knob_param(pg, k);
    int32_t v = p ? *p : 0;
    uint32_t i;
    *s = 0;
    if (!PAGES[pg].built) {                     /* (its knobs named, nothing behind them yet) */
        if (PAGES[pg].knob[k][0]) put_s(s, "NOT BUILT YET");
        return;
    }
    switch (pg) {
    case PG_SOUND: sel_engine()->text(trk[sel].p[sel_eng()], k, s); return;
    case PG_SOURCE:
        if (k == 0u) put_s(s, sel_engine()->name);
        else if (k == 1u) put_s(s, v ? "ON" : "OFF");
        return;
    case PG_ENV:                                /* (as env_coef: ms, Q16) */
        v = clamp(v, 0, 127);
        if (k == 0u) put_ms(s, pow2_q16(v * 20));
        else if (k == 1u) put_ms(s, 10u * pow2_q16(v * 15));
        else if (k == 2u) put_pc(s, v);
        else put_ms(s, 10u * pow2_q16((v * 33) >> 1));
        return;
    case PG_SCALE: if (k < 2u) put_s(s, k ? SC_NAME[clamp(v, 0, NSCALE - 1)] : SC_NOTE[clamp(v, 0, 11)]); return;
    case PG_CHORD: if (k < 2u) put_s(s, k ? VC_NAME[clamp(v, 0, NVOICING - 1)] : CH_SHORT[clamp(v, 0, NCHORD - 1)]); return;
    case PG_SONG:
        if (k == 0u || k == 2u) put_u(s, (uint32_t)v + (k ? 0u : 1u));
        else if (k == 1u) put_s(s, v >= SONG_END ? "END" : v == 0 ? "A" : v == 1 ? "B" : v == 2 ? "C" : "D");
        else put_s(s, SONG_MODE_NAME[clamp(v, 0, NSONGMODE - 1)]);
        return;
    case PG_LFO:
        if (k == 0u) lfo_rate_text(s, v, " ");
        else if (k == 1u) put_s(put_u(s, (uint32_t)clamp(v, 0, 100)), " %");
        else if (k == 2u) put_s(s, lfo_dest_name(v));
        else put_s(s, LFO_SHAPE_NAME[clamp(v, 0, NLFOSHAPE - 1)]);
        return;
    case PG_ARP:
        if (k == 0u) put_s(s, ARP_MODE_NAME[clamp(v, 0, NARPMODE - 1)]);
        else if (k == 1u) put_s(s, ARP_RATE_NAME[clamp(v, 0, NARPRATE - 1)]);
        else put_s(put_u(s, (uint32_t)v), k == 3u ? " %" : "");
        return;
    case PG_HOME: case PG_MIX1: put_pc(s, v); return;
    case PG_MIX2:
        if (k) return;
        if (v == 64) { put_s(s, "C"); return; }
        put_u(put_s(s, v < 64 ? "L " : "R "), (uint32_t)(v < 64 ? 64 - v : v - 64));
        return;
    case PG_TAPE: put_pc(s, v); return;
    case PG_GRAIN:
        switch (k) {
        case 0: put_s(put_u(s, (pow2_q16(clamp(v, 0, 127) * 8) * 10u + 32768u) >> 16), " ms"); return;
        case 1: i = gr_iv(v); put_s(put_u(s, (44100u + i / 2u) / (i ? i : 1u)), " /s"); return;
        default: put_pc(s, v); return;
        }
    case PG_DELAY:
        switch (k) {
        case 0: put_s(put_u(s, DL_MS[clamp(v, 0, 127)]), " ms"); return;
        case 1: put_s(put_u(s, ((uint32_t)v * 110u + 63u) / 127u), "%"); return;   /* the loop's gain: past 100 */
        case 3: put_pc(s, v); return;
        default: put_u(s, (uint32_t)v); return;
        }
    default:                                    /* SPACE */
        switch (k) {
        case 0: if (v >= RV_INF) put_s(s, "INF"); else put_s(put_x10(s, RV_RT60_X10[clamp(v, 0, 126)]), " s"); return;
        case 2: put_pc(s, v); return;
        case 3: put_s(put_u(s, ((uint32_t)v * 250u + 63u) / 127u), " ms"); return;
        default: put_u(s, (uint32_t)v); return;
        }
    }
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void hd_say(const char *m)               /* the header says m a moment (cut to what hd_msg holds) */
{
    uint32_t i;
    for (i = 0; m[i] && i < sizeof hd_msg - 1u; i++) hd_msg[i] = m[i];
    hd_msg[i] = 0;
    hd_until = fm1_ms + 1500u;
}
static int sv_shown(uint32_t i)                 /* SOUNDS: slot i in TYPE's list (ALL: every slot; a source: its sounds) */
{
    const uint8_t *r = snd_rec(i);
    return !sv_type || (r && r[3] == (uint32_t)sv_type - 1u);
}
static void sv_step(int32_t d)                  /* SOUNDS: SLOT d places on in TYPE's list */
{
    int32_t i = clamp(sv_slot, 0, SND_N - 1), k;
    if (!sv_shown((uint32_t)i)) d = d ? d : 1;
    for (; d; d += d > 0 ? -1 : 1)
        for (k = 0; k < (int32_t)SND_N; k++) {
            i = d > 0 ? (i + 1 < (int32_t)SND_N ? i + 1 : (int32_t)SND_N - 1) : (i > 0 ? i - 1 : 0);
            if (sv_shown((uint32_t)i) || i == 0 || i == (int32_t)SND_N - 1) break;
        }
    sv_slot = i;
}
static void brw_at(uint32_t t, const uint8_t *r)   /* PRESETS: track t's place, at sound r (its CATEGORY, its list) */
{
    uint32_t k;
    brw_cat[t] = r[1], trk_nm[t][0] = 0;
    for (k = 0; cat_item(r[1], k); k++)
        if (cat_item(r[1], k) == r) { brw_pos[t] = (int32_t)k; return; }
}
static void slot_do_save(uint32_t s)            /* the state into slot s (used: asked again first) */
{
    char m[24];
    if (slot_used(s) && !(ow_slot == (int32_t)s && fm1_ms < ow_until)) {
        ow_slot = (int32_t)s, ow_until = fm1_ms + 3000u;
        put_s(m, "AGAIN: OVERWRITE ");
        m[17] = (char)('A' + s), m[18] = 0;
        put_s(hd_msg, m), hd_until = ow_until;
        return;
    }
    ow_slot = -1;
    slot_save(s);
    put_s(m, "SAVED "), m[6] = (char)('A' + s), m[7] = 0;
    hd_say(m);
}
static void slot_do_recall(uint32_t s)          /* slot s back: playing, at the next bar; stopped, now */
{
    char m[24];
    if (!slot_used(s)) { hd_say("EMPTY"); return; }
    if (playing) {                              /* (song record: at the pass's end, song.c) */
        seq_recall = (int32_t)s;
        char *e = put_s(m, song_rec == 2u ? "NEXT PASS: " : "NEXT BAR: ");
        e[0] = (char)('A' + s), e[1] = 0;
    } else {
        slot_recall(s);
        put_s(m, "LOADED "), m[7] = (char)('A' + s), m[8] = 0;
    }
    brw_forget();                               /* (the tracks' sounds the slot's now: no names) */
    hd_say(m);
}
static void sv_sound(uint32_t act, uint32_t i, uint32_t cat, uint32_t t)   /* SOUNDS: SAVE (1) or CLEAR (2), now */
{
    int rc = act == 1u ? snd_save(i, t, cat) : snd_clear(i);
    hd_say(rc ? "NOT DONE" : act == 1u ? "SAVED" : "CLEARED");
}
static void sv_do(void)                         /* OCT+ on a SAVE page: its ACTION */
{
    if (page == PG_SOUNDS) {
        uint32_t i = (uint32_t)clamp(sv_slot, 0, SND_N - 1);
        const uint8_t *r = snd_rec(i);
        if (sv_act == 0) {                      /* LOAD */
            if (!r) { hd_say("EMPTY"); return; }
            undo_take(UK_SND | sel, -1);
            snd_apply(r, sel);
            brw_at(sel, r);
            hd_say("LOADED");
        } else if (playing || !voices_silent()) {   /* SAVE, CLEAR: the flash only while the sequencer stands and
                                                 * no voice sounds (latched notes too) */
            sv_pend = sv_act, sv_pend_slot = i, sv_pend_cat = (uint32_t)clamp(sv_cat, 0, NCAT - 1), sv_pend_trk = sel;
            hd_say(playing ? "SAVES ON STOP" : "SAVES WHEN SILENT");
        } else
            sv_sound((uint32_t)sv_act, i, (uint32_t)clamp(sv_cat, 0, NCAT - 1), sel);
        return;
    }
    switch (clamp(pj_act, 0, 3)) {              /* PROJECTS */
    case 0: slot_do_recall((uint32_t)pj_slot & 3u); return;
    case 1: slot_do_save((uint32_t)pj_slot & 3u); return;
    case 2:
        if (!slot_used((uint32_t)pj_slot & 3u)) { hd_say("EMPTY"); return; }
        undo_take(UK_ONE | ++uk_n, pj_slot & 3);
        slot_erase((uint32_t)pj_slot & 3u);
        hd_say("ERASED");
        return;
    default:
        undo_take(UK_ONE | ++uk_n, -1);
        project_new();
        brw_forget();
        hd_say("NEW");
        return;
    }
}
/* PRESETS (not on SEQ's pages): the track's next / last sound in its CATEGORY (the factory's first, then the user's);
 * past the end, the next CATEGORY's */
static void presets_turn(int32_t d)
{
    uint32_t c = (uint32_t)brw_cat[sel] % NCAT, tries;
    int32_t pos = brw_pos[sel];
    const uint8_t *r;
    char m[24];
    for (; d; d += d > 0 ? -1 : 1) {
        tries = 0;
        if (d > 0) {
            for (pos++; pos >= (int32_t)cat_len(c); c = (c + 1u) % NCAT, pos = 0)
                if (++tries > NCAT) { hd_say("NO SOUNDS"); return; }
        } else {
            for (pos--; pos < 0; pos = (int32_t)cat_len(c) - 1) {
                if (++tries > NCAT) { hd_say("NO SOUNDS"); return; }
                c = (c + NCAT - 1u) % NCAT;
            }
        }
    }
    if (!(r = cat_item(c, (uint32_t)pos))) { hd_say("NO SOUNDS"); return; }
    undo_take(UK_SND | sel, -1);                /* (one undo for a whole browse) */
    snd_apply(r, sel);
    brw_cat[sel] = (int32_t)c, brw_pos[sel] = pos, trk_nm[sel][0] = 0;
    snd_rec_name(r, m);
    hd_say(m);
}
#include "ui.c"                                 /* the screens (after the concept designs) */

static void set_source(int32_t e)               /* the selected track's source: its notes go over (a fade, then a new
                                                 * attack); each source keeps its own settings */
{
    e = clamp(e, 0, NENG - 1);
    if (e == trk[sel].eng)
        return;
    trk[sel].eng = e;
    nq_put(NQ_RESTART, sel, 0);
}
static void set_latch(int32_t on)               /* the selected track's LATCH; ON -> OFF releases its latched notes */
{
    on = on > 0;
    if (on == trk[sel].latch)
        return;
    if (!on)
        nq_put(NQ_RELEASE, sel, 0);
    trk[sel].latch = on;
}

/* a knob turned d detents (one main loop pass): faster, bigger steps (a slow turn: 1 a detent; a fast sweep: up to 8,
 * ~one turn for 0..127), never more than a sixteenth of its range a detent (a short range: 1). A jump of 8 or more in one
 * pass is a script's (host mode, the browser), not a hand's: as it is */
static int32_t enc_take(uint32_t e)             /* an encoder's detents (the screen dark: they only wake it) */
{
    int32_t d = fm1_enc_take(e);
    if (d) {
        t_hand = fm1_ms;
        if (scr_dark) scr_woke = 1, d = 0;
    }
    return d;
}
static int32_t enc_accel(uint32_t e, int32_t d, int32_t range)
{
    static uint32_t last[7];
    uint32_t dt = fm1_ms - last[e % 7u], ad = (uint32_t)(d < 0 ? -d : d), per, m;
    int32_t mx = range / 16;
    last[e % 7u] = fm1_ms;
    if (ad >= 8u || mx <= 1) return d;
    per = dt / ad;                              /* ms a detent */
    m = per >= 80u ? 1u : per >= 40u ? 2u : per >= 20u ? 4u : 8u;
    return d * (int32_t)(m < (uint32_t)mx ? m : (uint32_t)mx);
}
static void knob_step(uint32_t pg, uint32_t k, int32_t d)   /* KNOB k + 1 on page pg d steps on */
{
    volatile int32_t *p = knob_param(pg, k);
    int32_t lo = knob_min(pg, k), hi = knob_max(pg, k), old;
    if (!p) return;
    old = *p;
    if (pg == PG_SOUNDS && k == 0u) { sv_step(d); return; }   /* (SLOT: within TYPE's list) */
    if (pg == PG_GRID) undo_take(UK_STEP | sel << 8 | (uint32_t)seq[sel].cursor, -1);   /* (undo: a step's edit, */
    if (pg == PG_PATTERN && k == 0u) undo_take(UK_LEN | sel, -1);                      /* the pattern's length, */
    if (pg == PG_SOURCE && k == 0u) undo_take(UK_SRC | sel, -1);                       /* a source change, */
    if (pg == PG_SOUND && k == 0u && sel_eng() == ENG_FM) undo_take(UK_SND | sel, -1);   /* FM's PATCH: its edits) */
    if (pg == PG_SOURCE && k == 0u) set_source(*p + d);
    else if (pg == PG_SOURCE && k == 1u) set_latch(*p + d);
    else if (pg == PG_GRID && k == 0u && sc_scale > 0) *p = sc_walk(*p, d);   /* (a SCALE: its notes only) */
    else if (pg == PG_GRID && k == 0u && !*p && d > 0) *p = 60;   /* NOTE from OFF: C4, then on from there */
    else *p = clamp(*p + d, lo, hi);
    if (pg == PG_SOUNDS && k == 2u) sv_step(0);   /* (a new TYPE: SLOT onto its list) */
    if (pg == PG_SOUND && k == 0u && sel_eng() == ENG_FM && *p != old) fm_builtin(sel, *p);   /* (PATCH: the built-in one
                                                 * the track's patch now, its edits gone) */
    if (pg == PG_FMALGO && k == 0u && *p != old) nq_put(NQ_RESTART, sel, 0);   /* (ALGO: its notes again, a fade) */
    if (pg == PG_GRID && k == 0u && !*p) {      /* (a step OFF: its locks and its chord gone) */
        seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
        st->lmask = 0, st->chord = 0;
    }
}
static void knob_turn(uint32_t pg, uint32_t k, int32_t d)   /* KNOB k + 1 on page pg turned d detents (accelerated) */
{
    knob_step(pg, k, enc_accel(ENC_KNOB1 + k, d, knob_max(pg, k) - knob_min(pg, k)));
}

/* ---- MIDI LEARN (SYSTEM 2, KNOB 1): LEARN, a knob turned on any page (that parameter of that track: learn_to), the
 * first CC to come in linked to it (a CC or a parameter linked already: that link replaced); a linked CC moves its
 * parameter as its knob would (0..127 over the knob's range). OCT- cancels; CLEAR ALL + OCT+: every link gone */
static void learn_end(const char *m)
{
    learn_st = 0, sy_learn = LEARN_OFF;
    hd_say(m);
}
static void learn_knob(uint32_t pg, uint32_t k) /* a knob turned while LEARN waits: the parameter (SYSTEM's, SAVE's,
                                                 * PERFORM's: none) */
{
    if (!learn_st || pg == PG_SYSTEM || pg == PG_SYS2 || PAGES[pg].btn == BTN_SAVE || pg == PG_PERFORM ||
        !knob_param(pg, k))
        return;
    if (pg == PG_HOME && k == 0u) pg = PG_MIX1; /* (HOME's VOLUME: the track's LEVEL, as MIXER 1's) */
    learn_to[0] = (uint8_t)pg, learn_to[1] = (uint8_t)k, learn_to[2] = (uint8_t)(PAGES[pg].own ? sel : 0u);
    learn_to[3] = (uint8_t)(pg == PG_SOUND ? sel_eng() : pg == PG_GRID ? (uint32_t)seq[sel].cursor :
                            pg == PG_SONG && (k == 1u || k == 2u) ? (uint32_t)song_sect :
                            pg == PG_FMOP || (pg == PG_FMALGO && k == 3u) ? (uint32_t)fm_op : 0u);   /* (FM: the operator) */
    learn_st = 2;
}
static char *link_name(char *s, const uint8_t *L)   /* a link's parameter: "T1 CUTOFF", "WOW", "T2 LEVEL" */
{
    uint32_t pg = L[1] % NPAGE, k = L[2] & 3u;
    if (PAGES[pg].own) s = put_s(put_u(put_s(s, "T"), (L[3] & 3u) + 1u), " ");
    return put_s(s, pg == PG_SOUND ? ENGINES[L[4] % NENG]->knob[k] : PAGES[pg].knob[k]);
}
static void learn_cc(uint32_t cc)               /* MOVE A CC: this one, linked */
{
    uint32_t i, j, n = 0;
    char *e;
    for (i = 0; i < set.nlk && i < NLINK; i++) {
        for (j = 0; j < 4u && set.lk[i][1u + j] == learn_to[j]; j++)
            ;
        if (set.lk[i][0] == cc || j == 4u) continue;   /* (the CC's old link, the parameter's: out) */
        for (j = 0; j < 5u; j++) set.lk[n][j] = set.lk[i][j];
        n++;
    }
    if (n >= NLINK) { learn_end("32 LINKS: FULL"); return; }
    set.lk[n][0] = (uint8_t)cc;
    for (j = 0; j < 4u; j++) set.lk[n][1u + j] = learn_to[j];
    set.nlk = (uint8_t)(n + 1u);
    set_dirty = 1, set_t = fm1_ms;
    learn_end("");
    e = put_s(put_u(put_s(hd_msg, "CC "), cc), " > ");   /* "CC 21 > T1 CUTOFF", 3 s */
    link_name(e, set.lk[n]);
    hd_until = fm1_ms + 3000u;
}
static void cc_apply(const uint8_t *L, uint32_t val)   /* a linked CC: its parameter, val 0..127 over its range */
{
    uint32_t pg = L[1] % NPAGE, k = L[2] & 3u, x = L[4], s = sel;
    int32_t ss = song_sect, cu, v, op;
    volatile int32_t *p;
    if (pg == PG_SOUND) {                       /* (that source's, the track's source or not) */
        int32_t mx = ENGINES[x % NENG]->max[k];
        v = ((int32_t)val * mx + 63) / 127;
        if (x % NENG == ENG_FM && k == 0u && v != trk[L[3] & 3u].p[ENG_FM][0]) fm_builtin(L[3] & 3u, v);   /* (PATCH) */
        trk[L[3] & 3u].p[x % NENG][k] = v;
        return;
    }
    sel = L[3] & 3u, cu = seq[sel].cursor, op = fm_op;   /* (the knob's track, step, section, operator, for this one
                                                 * turn) */
    if (pg == PG_FMOP || (pg == PG_FMALGO && k == 3u)) fm_op = clamp((int32_t)x, 0, 5);
    if (pg == PG_GRID) seq[sel].cursor = clamp((int32_t)x, 0, SEQ_N - 1);
    if (pg == PG_SONG && (k == 1u || k == 2u)) song_sect = clamp((int32_t)x, 0, SONG_N - 1);
    if ((p = knob_param(pg, k)) != 0) {
        v = knob_min(pg, k) + ((int32_t)val * (knob_max(pg, k) - knob_min(pg, k)) + 63) / 127;
        if (pg == PG_GRID && k == 0u) {         /* NOTE: the note (a SCALE: its nearest), 0 OFF */
            seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
            undo_take(UK_STEP | sel << 8 | (uint32_t)seq[sel].cursor, -1);
            *p = v ? (int32_t)sc_snap((uint32_t)v) : 0;
            if (!*p) st->lmask = 0, st->chord = 0;
        } else
            knob_step(pg, k, v - *p);
    }
    seq[sel].cursor = cu, sel = s, fm_op = op;
    if (pg == PG_SONG && (k == 1u || k == 2u)) song_sect = ss;
}
static void midi_cc(uint32_t cc, uint32_t val)  /* a CC in (any channel) */
{
    uint32_t i;
    if (learn_st == 2u) { learn_cc(cc); return; }
    for (i = 0; i < set.nlk && i < NLINK; i++)
        if (set.lk[i][0] == cc) { cc_apply(set.lk[i], val); return; }
}

/* ---- USB-MIDI out: the keys' notes (the selected track's), and with SEQ MIDI OUT the steps' and the arps', each on its
 * track's channel (MIDI CHANNEL: T1 on the first; ALL > SEL: 1-4). echo_on keeps what is on out there: a channel
 * changed, SEQ MIDI OUT off, an echo lost (ec full): all of it let go first (mo_flush), never a note left on */
static uint32_t mo_flush;
static uint32_t out_ch(uint32_t t) { return (uint32_t)(set.mch < MCH_ALL ? set.mch : 0) + (t & 3u); }
static void midi_out(void)
{
    uint32_t n, c;
    if (ec_lost) {                              /* (ec was full: what waits in it goes, all of it let go) */
        ec_lost = 0;
        ec_r = ec_w;
        mo_flush = 1;
    }
    for (n = 0; mo_flush && n < 128u; n++)
        for (c = 0; echo_on[n] && c < 16u; c++)
            if (echo_on[n] >> c & 1u) {
                if (!midi_room()) return;       /* (USB's ring full: the rest next pass) */
                midi_send(0x80u | c, n, 0);
                echo_on[n] &= (uint16_t)~(1u << c);
            }
    mo_flush = 0;
    while (ec_r != ec_w && midi_room()) {
        uint32_t st = ec[ec_r % EC_LEN][0], en = ec[ec_r % EC_LEN][1];
        n = en & 127u, c = out_ch(st & 3u);
        if ((st & 0xF0u) == 0x90u) {
            if (!(en & EC_SEQ) || mo_seq) {
                midi_send(0x90u | c, n, 100u);
                echo_on[n] |= (uint16_t)(1u << c);   /* (sounding, sent: MIDI in ignores it on c till its off) */
            }
        } else if (echo_on[n] >> c & 1u) {
            midi_send(0x80u | c, n, 0);
            echo_on[n] &= (uint16_t)~(1u << c);
        }
        ec_r = ec_r + 1u;
    }
}

/* ---- SYSTEM's knobs into the settings (a change: saved), and where they act: the accent, TUNE, SEQ MIDI OUT, the
 * channels (changed: the notes out there let go, MIDI's held notes released) */
static void sys_sync(void)
{
    int8_t tn = (int8_t)clamp(sy_tune, -50, 50);
    uint8_t col = (uint8_t)clamp(sy_col, 0, 3), mch = (uint8_t)clamp(sy_mch, 0, MCH_ALL), scr = (uint8_t)clamp(sy_scr, 0, 4);
    uint8_t out = (uint8_t)clamp(sy_out, 0, 1);
    uint32_t n, t;
    if (mch != set.mch || (set.out && !out)) mo_flush = 1;
    if (mch != set.mch)
        for (n = 0; n < 128u; n++)
            for (t = 0; t < NT; t++)
                if (midi_held[n] >> t & 1u) nq_note(NQ_OFF, t, 0, MIDI_ID(n)), midi_held[n] &= (uint8_t)~(1u << t);
    if (tn != set.tune || col != set.col || mch != set.mch || scr != set.scr || out != set.out) {
        set.tune = tn, set.col = col, set.mch = mch, set.scr = scr, set.out = out;
        set_dirty = 1, set_t = fm1_ms;
    }
    ui_acc = UI_ACCENTS[set.col & 3u];
    tune_f = (set.tune * 23666 + set.tune * set.tune * 7) / 10000;   /* (2^(c / 1200) - 1, x 4096) */
    mo_seq = set.out;
}
static void sys_load(void)                      /* the settings at power-on into SYSTEM's knobs (version 19's: the
                                                 * octave alone; steps 20..24's: no ver, their links to HOME's T1..T4
                                                 * LEVEL now MIXER 1's LEVEL of that track, to TRACK's knobs HOME's) */
{
    int8_t o = 0;
    uint32_t i;
    if (settings_load(&set, sizeof set) && settings_load(&set, sizeof set - 1u)) {
        set.oct = settings_load(&o, 1) ? 0 : o;
        set.tune = 0, set.col = set.mch = set.scr = set.out = set.nlk = 0;
    }
    if (set.oct < -2 || set.oct > 2) set.oct = 0;
    if (set.nlk > NLINK) set.nlk = 0;
    sy_col = set.col, sy_mch = set.mch, sy_tune = set.tune, sy_scr = set.scr, sy_out = set.out;
    sys_sync();
    set_dirty = 0;
    if (set.ver != SET_VER)
        for (i = 0; i < set.nlk; i++) {         /* (a link moved: saved again) */
            uint8_t *L = set.lk[i];
            if (L[1] == PG_HOME) L[3] = L[2] & 3u, L[1] = PG_MIX1, L[2] = 0, L[4] = 0, set_dirty = 1;
            else if (L[1] == PG_TRACK) L[1] = PG_HOME, set_dirty = 1;
        }
    set.ver = SET_VER, set_t = fm1_ms;
}
/* HOME's KNOB k + 1 turned: what it changed in the header ~1.5 s ("T2 VOL 090", "T2 CUTOFF 824", "T2 CUTOFF 2.4K",
 * "T2 RES 040", "T2 FX 140%"; then the track and its sound again): its slabs show the four tracks */
static void home_say(uint32_t k)
{
    static const char *const W[4] = {" VOL ", " CUTOFF ", " RES ", " FX "};
    char *e = put_s(put_u(put_s(hd_msg, "T"), sel + 1u), W[k & 3u]);
    if (k == 1u) {
        uint32_t hz = CUTOFF_HZ[clamp(trk[sel].cut, 0, 127)];
        if (trk[sel].cut >= 127) put_s(e, "OPEN");
        else if (hz < 1000u) put_u(e, hz);
        else if (hz < 10000u) put_s(put_x10(e, (hz + 50u) / 100u), "K");
        else put_s(put_u(e, (hz + 500u) / 1000u), "K");
    } else if (k == 3u)
        put_s(put_u(e, (uint32_t)clamp(trk[sel].fx, 0, 200)), "%");
    else {
        uint32_t v = k ? (uint32_t)clamp(trk[sel].res, 0, 127) : (uint32_t)clamp(trk[sel].level, 0, 127);
        v = (v * 100u + 63u) / 127u;
        e[0] = (char)('0' + v / 100u), e[1] = (char)('0' + v / 10u % 10u), e[2] = (char)('0' + v % 10u), e[3] = 0;
    }
    hd_until = fm1_ms + 1500u;
}
/* SEL held on SEQ's pages: KNOB k + 1 locks the source's knob k on the selected step, from its lock (none: the knob's
 * value); an OFF step takes none */
static void lock_turn(uint32_t k, int32_t d)
{
    seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
    uint32_t e = sel_eng();
    int32_t mx = sel_engine()->max[k], v;
    if (!st->note) { hd_say("STEP OFF: NO LOCK"); return; }
    undo_take(UK_STEP | sel << 8 | (uint32_t)seq[sel].cursor, -1);
    v = st->lmask >> k & 1u ? st->lv[k] : trk[sel].p[e][k];
    st->lv[k] = (uint8_t)clamp(v + enc_accel(ENC_KNOB1 + k, d, mx), 0, mx);
    st->lmask |= (uint8_t)(1u << k);
}

/* SEQ held + the white keys F3 .. F4: the selected track's pattern. CLEAR, RANDOM (the SCALE's notes around the
 * keys' octave, some steps OFF), SHIFT LEFT / RIGHT (round within STEPS, with the locks), COPY, PASTE (steps, locks,
 * STEPS; across tracks), DOUBLE (steps 1..STEPS onto STEPS + 1 .., STEPS twice, 64 at most), MUTATE (2 or 3 steps a
 * little: a note a scale step or two on, a step on or off). Each but COPY undoable */
static uint8_t clip[SEQ_PK + LOCK_PK];          /* COPY's pattern (packed: its steps, locks, STEPS) */
static uint32_t clip_ok, tl_rng = 0x6C8E9CF5u;
static uint32_t tl_rnd(uint32_t n)
{
    tl_rng ^= tl_rng << 13, tl_rng ^= tl_rng >> 17, tl_rng ^= tl_rng << 5;
    return n ? tl_rng % n : 0u;
}
static void step_off(seq_step_t *s)
{
    s->note = 0, s->chance = 100, s->ratchet = 1, s->len = 1, s->chord = 0, s->lmask = 0;
    s->lv[0] = s->lv[1] = s->lv[2] = s->lv[3] = 0;
}
static void step_cp(seq_step_t *d, const seq_step_t *s)
{
    d->note = s->note, d->chance = s->chance, d->ratchet = s->ratchet, d->len = s->len, d->chord = s->chord;
    d->lmask = s->lmask, d->lv[0] = s->lv[0], d->lv[1] = s->lv[1], d->lv[2] = s->lv[2], d->lv[3] = s->lv[3];
}
static int32_t tl_note(uint32_t c)              /* a note near c, in the SCALE */
{
    return (int32_t)sc_snap((uint32_t)clamp((int32_t)c - 7 + (int32_t)tl_rnd(15), 1, 127));
}
static void seq_tool(uint32_t w, uint32_t base)
{
    static const char *const SAY[8] = {"CLEARED", "RANDOM", "SHIFTED LEFT", "SHIFTED RIGHT", "COPIED", "PASTED",
                                       "DOUBLED", "MUTATED"};
    seq_pat_t *P = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)];
    int32_t n = seq_steps(&seq[sel]), i, j, m;
    uint32_t c = base + 7u;                     /* (the keys' octave: its C) */
    seq_step_t tmp;
    tl_rng ^= fm1_ms;
    if (w == 5u && !clip_ok) { hd_say("NOTHING COPIED"); return; }
    if (w == 6u && n >= (int32_t)SEQ_N) { hd_say("64 STEPS: FULL"); return; }
    if (w != 4u) undo_take(UK_ONE | ++uk_n, -1);
    switch (w) {
    case 0:                                     /* CLEAR */
        for (i = 0; i < (int32_t)SEQ_N; i++) step_off(&P->st[i]);
        break;
    case 1:                                     /* RANDOM */
        for (i = 0; i < (int32_t)SEQ_N; i++) {
            step_off(&P->st[i]);
            if (i < n && tl_rnd(100) < 70u) P->st[i].note = tl_note(c);
        }
        break;
    case 2: case 3:                             /* SHIFT LEFT, RIGHT: round within STEPS */
        if (w == 2u) {
            step_cp(&tmp, &P->st[0]);
            for (i = 0; i < n - 1; i++) step_cp(&P->st[i], &P->st[i + 1]);
            step_cp(&P->st[n - 1], &tmp);
        } else {
            step_cp(&tmp, &P->st[n - 1]);
            for (i = n - 1; i > 0; i--) step_cp(&P->st[i], &P->st[i - 1]);
            step_cp(&P->st[0], &tmp);
        }
        break;
    case 4:                                     /* COPY */
        pk_seq(clip, sel), pk_lock(clip + SEQ_PK, sel);
        clip_ok = 1;
        break;
    case 5: {                                   /* PASTE: steps, locks, STEPS (DIVISION, SWING, PLAY MODE stay) */
        int32_t dv = seq[sel].div, sw = seq[sel].swing, md = seq[sel].mode;
        uk_seq(clip, sel, SEQ_PK), uk_lock(clip + SEQ_PK, sel, LOCK_PK);
        seq[sel].div = dv, seq[sel].swing = sw, seq[sel].mode = md;
        break;
    }
    case 6:                                     /* DOUBLE */
        for (i = n; i < 2 * n && i < (int32_t)SEQ_N; i++) step_cp(&P->st[i], &P->st[i - n]);
        seq[sel].steps = 2 * n < (int32_t)SEQ_N ? 2 * n : (int32_t)SEQ_N;
        break;
    default:                                    /* MUTATE */
        for (m = 2 + (int32_t)tl_rnd(2), j = 0; j < m; j++) {
            seq_step_t *s = &P->st[tl_rnd((uint32_t)n)];
            int32_t dd = (tl_rnd(2) ? 1 : -1) * (1 + (int32_t)tl_rnd(2));
            if (s->note && tl_rnd(3)) s->note = sc_scale > 0 ? sc_walk(s->note, dd) : clamp(s->note + dd, 1, 127);
            else if (s->note) step_off(s);
            else s->note = tl_note(c);
            if (!s->note) step_off(s);
        }
        break;
    }
    hd_say(SAY[w]);
}

/* the battery (HOME's header, beside the tempo): the divider's raw reading once a second, smoothed (~8 s), its level by
 * the stock firmware's thresholds (531, 561, 591 on ADC ch3, as Felucca has them; no calibration to volts) with a
 * little hysteresis: 0..3 (1..4 bars; 0, the lowest: its bar blinks); 4 a USB host powers it (a bolt: there is no
 * charger line, a charger alone is not seen); -1 no reading (host mode, the browser: no icon): bat_lvl */
static void batt_poll(void)
{
    static const int32_t TH[3] = {531, 561, 591};
    static uint32_t t;
    static int32_t s, lv = -1;                  /* (the reading x 16, smoothed; the level) */
    int32_t b;
    if (t && fm1_ms - t < 1000u) return;
    t = fm1_ms ? fm1_ms : 1u;
    if ((b = batt_raw()) > 0) {
        s = s ? s + (b * 16 - s) / 8 : b * 16;
        b = s / 16;
        if (lv < 0) for (lv = 0; lv < 3 && b >= TH[lv]; lv++);
        while (lv < 3 && b >= TH[lv] + 3) lv++;
        while (lv > 0 && b < TH[lv - 1] - 3) lv--;
    }
    bat_lvl = usb_powered() ? 4 : lv;
}

#include "editor.c"                             /* the editor's mailbox: sounds, slots, settings over SysEx */
#ifdef GRIMM_STRESS
#include "../../tests/stress_check.c"             /* (tests/stress.sh: the stress test's checks, host mode) */
#endif
static void app_main(void)
{
    uint32_t prev = 0, k, b_down = 0, b_used = 0, t_down[14] = {0}, rec = 0,
             back_page = PG_HOME, pf_keys = 0, pf_back = PG_HOME, t_touch = 0, as_last = 0, as_next = 0, learn_was = 0,
             fac_was = 0;
    static const uint8_t WK[8] = {0, 2, 4, 6, 7, 9, 11, 12};   /* the white keys from F3: 1..4 recall A..D, 5..8 save them */
    int8_t oct_before = 0;                      /* the octave before the last OCT press (undone if it was a reset) */
    ima_init();                                 /* (samples.c: the decoder's tables, before samples_poll checks a slot) */
    seq_init();
    store_boot();                               /* the working project and the slots, as the FM-1 was left */
    pj_slot = slot_cur;
    sys_load();                                 /* (the settings: the octave, SYSTEM's; the accent before the boot
                                                 * screen) */

    ui_boot();                                  /* (the boot screen, UI_BOOT_MS: ui_update moves it) */
    if ((k = smp_erased()) != 0u) {             /* the factory samples into the slots that hold nothing (samples.c),
                                                 * said on the boot screen while they go in (~1 s a slot) */
        uint32_t s;
        bt_note = "INSTALLING SAMPLES";
        ui_boot();
        for (s = 0; s < SMP_SLOTS; s++)
            if (k >> s & 1u) smp_factory(s);
        bt_note = 0;
        ui_boot();
    }
    if (slot_lost) {                            /* a slots' sector bad at power-on (store.c): those slots empty, said */
        char *q = put_s(hd_msg, "SLOT DATA LOST:");
        for (k = 0; k < 4u; k++)
            if (slot_lost >> k & 1u) {
                if (slot_lost != 15u) *q++ = ' ';
                *q++ = (char)('A' + k), *q = 0;
            }
        hd_until = 15000u;
    }
    if (snd_moved || snd_lost) {                /* the sounds made 64 (store.c): those past 64 moved down, or lost */
        char *q = put_s(put_u(put_s(hd_msg, "SOUNDS: "), snd_moved), " MOVED");
        if (snd_lost) put_s(put_u(put_s(q, ", "), snd_lost), " LOST");
        hd_until = 15000u;
    }
#if !defined DINGHY_HOST && !defined GRIMM_WEB
    if (fm1_boot.p3_rst & 4u) {                 /* the watchdog restarted it (the main loop stood 8 s; a fault shows
                                                 * the red crash screen instead): said for ~15 s */
        put_s(hd_msg, "WATCHDOG RESET");
        hd_until = 15000u;
    }
#endif

    for (;;) {
        uint32_t keys, down, up, rel, b, n, lit = 0, base = (uint32_t)(KEY0 + 12 * set.oct);
        uint8_t st, d1, d2;
        int32_t d;
        platform_poll();
        if (fm1_in.notes != prev || fm1_in.buttons) t_touch = t_hand = fm1_ms;   /* (the autosave waits for the hands
                                                 * to rest; SCREEN OFF counts from their last touch) */

        if ((d = enc_take(ENC_ALGORITHM)) != 0) {   /* ALGORITHM: the track */
            sel = (uint32_t)clamp((int32_t)sel + d, 0, NT - 1);
            t_touch = fm1_ms;
        }
        if ((d = enc_take(ENC_SELECT)) != 0) {   /* SELECT: SEQ held, the track's STEPS; on SEQ's pages the cursor
                                                 * (the step); on HOME the tempo */
            t_touch = fm1_ms;
            if (b_down >> BTN_SEQ & 1u) {
                undo_take(UK_LEN | sel, -1);
                seq[sel].steps = clamp(seq[sel].steps + enc_accel(ENC_SELECT, d, SEQ_N - 1), 1, SEQ_N);
                b_used |= 1u << BTN_SEQ;        /* (let go: no page change) */
            } else if (PAGES[page].btn == BTN_SEQ)
                seq[sel].cursor = clamp(seq[sel].cursor + enc_accel(ENC_SELECT, d, SEQ_N - 1), 0, SEQ_N - 1);
            else if (page == PG_HOME)
                seq_bpm = clamp(seq_bpm + enc_accel(ENC_SELECT, d, 280), 20, 300);
        }
        if ((d = enc_take(ENC_PRESETS)) != 0) {   /* PRESETS: on SEQ's pages the tempo; elsewhere the saved sounds */
            t_touch = fm1_ms;
            if (PAGES[page].btn == BTN_SEQ)
                seq_bpm = clamp(seq_bpm + enc_accel(ENC_PRESETS, d, 280), 20, 300);
            else
                presets_turn(d);
        }

        b = fm1_input_edges(&rel);              /* buttons pressed / released since the last call (bit = button id) */
        if (b | rel) t_touch = t_hand = fm1_ms;
        if (scr_dark && b) scr_woke = 1, b = 0; /* (the screen dark: a press only wakes it; let go as ever) */
        if ((b & OCT_BOTH) && (fm1_in.buttons & OCT_BOTH) == OCT_BOTH) {   /* OCT- and OCT+ held together: PEAK to 0 */
            if ((b & OCT_BOTH) != OCT_BOTH) set.oct = oct_before;           /* (the first one's octave step undone) */
            cpu_reset++;
            hd_say("CPU PEAK RESET");         /* (CPU NOW, PEAK: SYSTEM 2) */
        } else if ((b & (1u << BTN_OCT_DOWN)) && (b_down >> BTN_SEL & 1u)) {   /* SEL held + OCT-: the selected step's
                                                 * locks cleared */
            seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
            undo_take(UK_ONE | ++uk_n, -1);
            st->lmask = 0;
            b_used |= 1u << BTN_SEL;
            hd_say("LOCKS CLEARED");
        } else if ((b & OCT_BOTH) && (b_down >> BTN_EDIT & 1u)) {   /* EDIT held + OCT-: undo; + OCT+: redo */
            if (b & (1u << BTN_OCT_DOWN)) hd_say(undo() ? "NOTHING TO UNDO" : "UNDO");
            if (b & (1u << BTN_OCT_UP)) hd_say(redo() ? "NOTHING TO REDO" : "REDO");
            b_used |= 1u << BTN_EDIT;           /* (let go: no page change) */
        } else if ((b & (1u << BTN_OCT_DOWN)) && (learn_st || sy_learn == LEARN_CLEAR)) {   /* MIDI LEARN: OCT-
                                                 * cancels (LEARN, CLEAR ALL) */
            learn_end(sy_learn == LEARN_CLEAR ? "NOT CLEARED" : "LEARN CANCELLED");
        } else if ((b & (1u << BTN_OCT_DOWN)) && page == PG_SYS2 && sy_fac) {   /* SAMPLES: OCT- keeps them */
            sy_fac = 0;
            hd_say("SAMPLES KEPT");
        } else if ((b & (1u << BTN_OCT_UP)) && page == PG_SYS2 && sy_fac) {   /* RESTORE: OCT+, when silent */
            if (playing || !quiet()) hd_say("STOP AND SILENCE FIRST");
            else {
                sy_fac = 0;
                hd_say("RESTORING SAMPLES"), hd_until = fm1_ms + 15000u;
                fac_at = fm1_ms + 120u;         /* (the header shows it first: the flash then stands the screen) */
            }
        } else if ((b & (1u << BTN_OCT_UP)) && page == PG_SYS2 && sy_learn == LEARN_CLEAR) {   /* CLEAR ALL: OCT+ */
            set.nlk = 0;
            set_dirty = 1, set_t = fm1_ms;
            learn_end("ALL LINKS CLEARED");
        } else if (PAGES[page].btn == BTN_SAVE) {   /* SAVE's menus: OCT+ confirms (the ACTION), OCT- backs out */
            if (b & (1u << BTN_OCT_UP)) sv_do();
            if (b & (1u << BTN_OCT_DOWN)) page = back_page;
        } else {
            if (b & (1u << BTN_OCT_DOWN)) { oct_before = set.oct; if (set.oct > -2) { set.oct--; set_dirty = 1; set_t = fm1_ms; } }
            if (b & (1u << BTN_OCT_UP)) { oct_before = set.oct; if (set.oct < 2) { set.oct++; set_dirty = 1; set_t = fm1_ms; } }
            if (b & OCT_BOTH) {                 /* (the octave in the header a moment: "OCTAVE +1") */
                char *e = put_s(hd_msg, "OCTAVE ");
                e[0] = set.oct < 0 ? '-' : '+', e[1] = (char)('0' + (set.oct < 0 ? -set.oct : set.oct)), e[2] = 0;
                hd_until = fm1_ms + 1500u;
            }
        }
        for (k = 0; k < 14u; k++)               /* the pages: a button without a hold function on its press; one with,
                                                 * when let go (unless its hold was used) */
            if ((b >> k & 1u) && (BTN_PAGES >> k & 1u)) {
                if (BTN_HOLDS >> k & 1u) {
                    b_down |= 1u << k;
                    b_used &= ~(1u << k);
                    t_down[k] = fm1_ms;
                } else
                    page = next_page(k);
            }
        if (song_end) song_end = 0, playing = 0;   /* (a SONG played out: stopped) */
        if (b & (1u << BTN_PLAY)) {             /* PLAY/STOP: all four tracks' sequences (a song: song.c) */
            playing ^= 1u;
            nq_put(playing ? NQ_PLAY : NQ_STOP, 0, (uint32_t)slot_cur & 3u);
        }
        if (b & (1u << BTN_REC)) {
            if (page == PG_SONG || song_rec) {  /* the SONG page: song record armed (playing: from now), or ended */
                if (song_rec) {
                    if (playing && song_rec == 2u) nq_note(NQ_SONG, 0, 0, 0);
                    else song_rec = 0;
                } else {
                    undo_take(UK_SONG | ++uk_n, -1);   /* (undo: the chain as it was) */
                    undo_chain = 1;
                    song_rec = 1;
                    if (playing) nq_note(NQ_SONG, 0, (uint32_t)slot_cur & 3u, 1);
                }
            } else {                            /* REC: stopped, step record; playing, live record */
                rec ^= 1u;
                if (rec) undo_take(UK_ONE | ++uk_n, -1);   /* (undo: the recording pass) */
            }
        }
        rec_on = rec;
        glo_held = b_down >> BTN_GLO & 1u;      /* GLO held: mutes, solos on the keys */
        if ((b_down >> BTN_HOME & 1u) && !(b_used >> BTN_HOME & 1u) && fm1_ms - t_down[BTN_HOME] >= HOLD_MS) {
            page = PG_SYSTEM;                   /* HOME held: SYSTEM */
            b_used |= 1u << BTN_HOME;
        }
        if ((rel >> BTN_SEL & 1u) && (b_down >> BTN_SEL & 1u) && !(b_used >> BTN_SEL & 1u) &&
            fm1_ms - t_down[BTN_SEL] >= HOLD_MS) {   /* SEL held, let go: the track's chord mode (no page change; a knob
                                                 * turned, SEL + OCT- meanwhile: not) */
            trk[sel].chord = !trk[sel].chord;
            b_used |= 1u << BTN_SEL;
            hd_say(trk[sel].chord ? "CHORD ON" : "CHORD OFF");
        }
        sel_held = b_down >> BTN_SEL & 1u;
        if ((b_down >> BTN_ARP & 1u) && !(b_used >> BTN_ARP & 1u) && fm1_ms - t_down[BTN_ARP] >= HOLD_MS) {
            trk[sel].arp = !trk[sel].arp;       /* ARP held: the track's arp (let go: no page change) */
            b_used |= 1u << BTN_ARP;
            hd_say(trk[sel].arp ? "ARP ON" : "ARP OFF");
        }
        arp_rec = rec && playing ? (int32_t)sel : -1;   /* (REC, playing: the arp's notes recorded) */
        save_held = b_down >> BTN_SAVE & 1u;    /* SAVE held: its layer on the keys (slots A..D), their state shown */
        if ((b_down >> BTN_FX & 1u) && page != PG_PERFORM && fm1_ms - t_down[BTN_FX] >= 300u) {
            pf_back = page;                     /* FX held ~0.3 s: PERFORM (let go: back, no page change) */
            page = PG_PERFORM;
            b_used |= 1u << BTN_FX;
        }

        keys = fm1_in.notes;                    /* bit k: key k (0 = F3 .. 26 = G5) */
        down = keys & ~prev;
        up = prev & ~keys;
        if (scr_dark && down) scr_woke = 1, down = 0;   /* (the screen dark: a key only wakes it) */
        prev = keys;
        for (k = 0; k < 27u; k++) {
            if ((up >> k & 1u) && (pf_keys >> k & 1u)) {   /* a trick's key let go: the trick off */
                uint32_t w;
                for (w = 1; w < 8u && WK[w] != k; w++)
                    ;
                pf_keys &= ~(1u << k);
                if (w < 8u) pf_held &= ~(1u << (w - 1u));
            }
            if (up >> k & 1u && key_held[k]) {  /* a held key let go: the voices it started released */
                nq_note(NQ_OFF | NQ_ECHO, (key_held[k] >> 8) & 3u, 0, KEY_ID(k));
                key_held[k] = 0;
            }
            if (!(down >> k & 1u)) continue;
            if (glo_held) {                     /* GLO held: white keys 1..4 mute T1..T4, 5..8 solo them (on, off) */
                uint32_t w;
                for (w = 0; w < 8u && WK[w] != k; w++)
                    ;
                if (w < 4u) trk_mute ^= 1u << w;
                else if (w < 8u) trk_solo ^= 1u << (w - 4u);
                b_used |= 1u << BTN_GLO;
                continue;
            }
            if (b_down >> BTN_SEQ & 1u) {       /* SEQ held + white keys 1..8: the sequencer's tools (no page change) */
                uint32_t w;
                for (w = 0; w < 8u && WK[w] != k; w++)
                    ;
                if (w < 8u) {
                    seq_tool(w, base);
                    b_used |= 1u << BTN_SEQ;
                    continue;
                }
            }
            if (save_held) {                    /* SAVE held: white keys 1..4 recall A..D, 5..8 save into them */
                uint32_t w;
                for (w = 0; w < 8u && WK[w] != k; w++)
                    ;
                if (w < 4u) slot_do_recall(w);
                else if (w < 8u) slot_do_save(w - 4u);
                b_used |= 1u << BTN_SAVE;
                continue;
            }
            if ((b_down >> BTN_FX & 1u) && k != 0u) {   /* FX held + G3 .. F4: a trick, while its key is held */
                uint32_t w;
                for (w = 1; w < 8u && WK[w] != k; w++)
                    ;
                if (w < 8u) {
                    pf_keys |= 1u << k;
                    pf_held |= 1u << (w - 1u);
                    b_used |= 1u << BTN_FX;
                    continue;
                }
            }
            if ((b_down >> BTN_FX & 1u) && k == 0u) {   /* FX held + F3: FREEZE */
                ge_freeze ^= 1u;
                b_used |= 1u << BTN_FX;
                continue;
            }
            n = sc_snap(base + k);              /* (a SCALE: its nearest note) */
            ch_last = (int32_t)n;
            if (rec && !(playing && trk[sel].arp)) {   /* REC: the note into the track's sequence (stopped: the
                                                 * cursor's step, the cursor on; playing: the nearest step), heard while
                                                 * held (ARP on, playing: the arp's notes instead, arp.c) */
                int32_t i = playing ? seq_nearest(sel) : seq[sel].cursor;
                if (i >= 0) seq_write(sel, i, n, trk[sel].chord ? chord_step(sel) : 0u);   /* (chord mode: a chord
                                                 * step, its chord as it is now) */
                if (!playing) seq[sel].cursor = (seq[sel].cursor + 1) % clamp(seq[sel].steps, 1, SEQ_N);
            }
            if (trk[sel].latch && !rec)         /* (LATCH ON: that key's voices on, or off) */
                nq_note(NQ_TOGGLE | NQ_ECHO, sel, n, KEY_ID(k));
            else {
                nq_note(NQ_ON | NQ_ECHO, sel, n, KEY_ID(k));
                key_held[k] = (uint16_t)(0x8000u | sel << 8);
            }
        }
        for (k = 0; k < 14u; k++)               /* the buttons let go: their page (unless the hold was used) */
            if ((rel >> k & 1u) && (b_down >> k & 1u)) {
                if (!(b_used >> k & 1u)) {
                    if (k == BTN_SAVE && PAGES[page].btn != BTN_SAVE) back_page = page;   /* (OCT- comes back here) */
                    page = next_page(k);
                }
                if (k == BTN_FX && page == PG_PERFORM) page = pf_back;   /* (PERFORM: back where it was) */
                b_down &= ~(1u << k);
            }

        for (k = 0; k < 4u; k++)                /* KNOB 1..4: the page's four (SEL held: SEQ's pages, the step's locks;
                                                 * SEL let go then: no chord mode) */
            if ((d = enc_take(ENC_KNOB1 + k)) != 0) {
                if ((b_down >> BTN_SEL & 1u) && PAGES[page].btn == BTN_SEQ) lock_turn(k, d);
                else knob_turn(page, k, d), learn_knob(page, k);   /* (LEARN: this one's parameter) */
                if (page == PG_HOME) home_say(k);   /* (HOME: what it changed, in the header a moment) */
                if (b_down >> BTN_SEL & 1u) b_used |= 1u << BTN_SEL;
                t_touch = fm1_ms;
            }

        if (sy_learn == LEARN_ON && !learn_st) learn_st = 1;   /* MIDI LEARN: LEARN turned to: TURN A KNOB; */
        if (sy_learn != LEARN_ON && learn_st) learn_st = 0;    /* turned away: no more */
        if (sy_learn == LEARN_CLEAR && (page != PG_SYS2 || !learn_was)) {   /* CLEAR ALL: OCT+ (away: back to OFF) */
            if (page != PG_SYS2) sy_learn = LEARN_OFF;
            else hd_say("OCT+: CLEAR ALL LINKS");
        }
        learn_was = sy_learn == LEARN_CLEAR;
        if (sy_fac && page != PG_SYS2) sy_fac = 0;   /* SAMPLES: away, back to KEEP; RESTORE turned to: said */
        if (sy_fac && !fac_was) hd_say("OCT+: RESTORE SAMPLES");
        fac_was = sy_fac != 0;
        if (fac_at && (int32_t)(fm1_ms - fac_at) >= 0) {   /* RESTORE: the four factory samples (~5 s) */
            uint32_t s, rc = 0;
            fac_at = 0;
            for (s = 0; s < SMP_SLOTS && !rc; s++) rc = smp_factory(s);
            hd_say(rc ? "RESTORE FAILED" : "SAMPLES RESTORED");
        }
        sys_sync();
        if ((page == PG_FMALGO || page == PG_FMOP) && sel_eng() != ENG_FM) page = PG_SOUND;   /* (not FM now: SOUND) */

        while (midi_get(&st, &d1, &d2)) {       /* USB-MIDI in: a note on MIDI CHANNEL's T1 .. T4 channels plays that
                                                 * track as its keys (ALL > SEL: any channel, the selected track); a CC:
                                                 * its link (MIDI LEARN) */
            uint32_t ty = st & 0xF0u, c = st & 15u, on = ty == 0x90u && d2, off = ty == 0x80u || (ty == 0x90u && !d2), t, m;
            t_touch = fm1_ms;
            d1 &= 0x7Fu;
            if (ty == 0xB0u) { midi_cc(d1, d2 & 0x7Fu); continue; }
            if (!(on || off) || (echo_on[d1] >> c & 1u)) continue;   /* (our own notes come back: a computer's MIDI
                                                 * thru; never played again) */
            if (set.mch >= MCH_ALL) t = sel;
            else if (c - set.mch < NT) t = c - set.mch;
            else continue;                      /* (not one of the tracks' channels) */
            n = sc_snap(d1);                    /* (a SCALE: its nearest note) */
            if (on && t == sel) ch_last = (int32_t)n;
            for (m = 0; m < NT; m++)            /* LATCH OFF: the voices it started released (a note-on again without
                                                 * its off: those first); ALL > SEL: whichever track it went to */
                if ((midi_held[d1] >> m & 1u) && (m == t || set.mch >= MCH_ALL)) {
                    nq_note(NQ_OFF, m, 0, MIDI_ID(d1));
                    midi_held[d1] &= (uint8_t)~(1u << m);
                }
            if (on && trk[t].latch)
                nq_note(NQ_TOGGLE, t, n, MIDI_ID(d1));
            else if (on) {
                nq_note(NQ_ON, t, n, MIDI_ID(d1));
                midi_held[d1] |= (uint8_t)(1u << t);
            }
        }
        midi_out();                             /* the notes out over USB-MIDI (the echoes from the audio ISR) */

        base = (uint32_t)(KEY0 + 12 * set.oct);
        for (k = 0; k < nlat && k < NV; k++) {  /* the selected track's notes, on the keys they came from: the
                                                 * physical key (never the note after SCALE or chord), a MIDI note's
                                                 * number as it came, a step's note */
            uint32_t id = lat_id[k], o = lat_src[k] == SRC_SEQ ? lat_root[k] : id >= MIDI_ID(0) ? id - MIDI_ID(0) : 255u;
            if (lat_trk[k] != sel || lat_src[k] == SRC_ARP) continue;   /* (the arp's: its keys, below) */
            if (lat_src[k] == SRC_KEY && id < 27u) lit |= 1u << id;
            else if (o >= base && o < base + 27u) lit |= 1u << (o - base);
        }
        for (k = 0; k < arp[sel].n && k < ARP_N; k++) {   /* ARP: the keys (MIDI notes) in its list */
            uint32_t id = arp[sel].id[k], o = id >= MIDI_ID(0) ? id - MIDI_ID(0) : 255u;
            if (id < 27u) lit |= 1u << id;
            else if (o >= base && o < base + 27u) lit |= 1u << (o - base);
        }
        if (b_down >> BTN_SEQ & 1u)             /* (SEQ held: the tools' eight keys lit) */
            for (k = 0; k < 8u; k++) lit |= 1u << WK[k];
        if (glo_held)                           /* (GLO held: keys 1..4 lit for the tracks muted, 5..8 soloed) */
            for (lit = 0, k = 0; k < 4u; k++) lit |= (trk_mute >> k & 1u) << WK[k] | (trk_solo >> k & 1u) << WK[4u + k];
        if (save_held)                          /* (SAVE held: keys 1..4 lit for the slots used) */
            for (lit = 0, k = 0; k < 4u; k++) lit |= (uint32_t)slot_used(k) << WK[k];
        if (b_down >> BTN_FX & 1u)              /* (FX held: the eight tricks' keys glow; one on: lit) */
            for (k = 0; k < 8u; k++) lit |= (k ? pf_held >> (k - 1u) & 1u : ge_freeze & 1u) << WK[k];
        for (k = 0; k < 27u; k++) {
            uint32_t wk;
            for (wk = 0; wk < 8u && WK[wk] != k; wk++)
                ;
            fm1_led_key(KEY_LED(k), lit >> k & 1u);
            fm1_led_glow(KEY_LED(k), wk < 8u && (b_down >> BTN_FX & 1u));
        }
        fm1_led_key(BTN_OCT_DOWN, set.oct < 0);
        fm1_led_key(BTN_OCT_UP, set.oct > 0);
        for (k = 0; k < 14u; k++)               /* the button whose page is open */
            if (BTN_PAGES >> k & 1u) fm1_led_key(k, PAGES[page].btn == k);
        fm1_led_key(BTN_PLAY, playing);
        fm1_led_key(BTN_REC, rec || song_rec);

        if (scr_woke) {                         /* SCREEN OFF: a touch woke it; no touch so long: dark (the panel keeps
                                                 * its picture; nothing drawn meanwhile) */
            scr_woke = scr_dark = 0;
            lcd_light(1);
        } else if (!scr_dark && set.scr && fm1_ms - t_hand >= SCR_S[set.scr % 5u] * 1000u) {
            scr_dark = 1;
            lcd_light(0);
        }
        batt_poll();
        if (!scr_dark) ui_update(set.oct);
#ifdef GRIMM_STRESS
        stress_poll();
#endif
        tapefx_poll();                          /* (TAPE: SATURATION's curve, when the knob has moved) */
        samples_poll(quiet() && !playing);      /* an upload's frame (writes only while nothing sounds, never while
                                                 * the sequencer plays), the slots' checks */

        sv_wait = !playing && (as_wait || sv_pend >= 0) && !voices_silent();
        if (set_dirty && quiet() && !playing && fm1_ms - set_t > 2000u) {   /* 2 s after the change, when silent */
            settings_save(&set, sizeof set);
            set_dirty = 0;
        }
        if (!playing && sv_pend >= 0 && voices_silent()) {   /* SOUNDS: a SAVE or CLEAR that waited (the stop, silence) */
            sv_sound((uint32_t)sv_pend, sv_pend_slot, sv_pend_cat, sv_pend_trk);
            sv_pend = -1;
        }
        /* the autosave: the sequencer standing, the hands at rest 2.5 s, at most every 20 s, if anything changed, no voice
         * sounding (a release, a latched note: it waits, the header says so); a failed one: again in 3 s, the header
         * says so */
        if (playing) as_wait = 0;
        if (!playing && !ed_open() && fm1_ms - t_touch >= 2500u && (!as_last || fm1_ms - as_last >= 20000u) &&
            fm1_ms >= as_next) {                /* (an editor's PUT open: its bytes are in st_img) */
            uint32_t g = st_gen;
            as_next = fm1_ms + 500u;
            if (!voices_silent())               /* (no flash while a voice sounds: it waits, if there is anything) */
                as_wait = (uint32_t)autosave_due();
            else {
                as_wait = 0;
                if (autosave()) {
                    as_err = 1;
                    as_next = fm1_ms + 3000u;
                } else {
                    as_err = 0;
                    if (st_gen != g) as_last = fm1_ms ? fm1_ms : 1u;
                }
            }
        }
    }
}

#ifdef GRIMM_WEB
#include "../../web/editor_web.c"                 /* (the browser build only: what the editor page asks of it) */
#endif
