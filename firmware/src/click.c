/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V3.0's metronome (CLICK; SEQ held + F#3 turns it on, off): while the sequencer plays (REC on or off), a short
 * click on every beat, a higher and louder one on beat 1 of the bar. It follows the tempo and an external MIDI clock for
 * the beats are the sequencer's own ticks (seq_tick: 96 a beat, 384 a bar since PLAY). No count-in: the first click is
 * the first tick.
 * It is not a track: no voice (none of the 6 is taken), nothing in any pattern, no sound of the project's. It is added to
 * the mix when every track and effect is in, just before the output stage: the MASTER knob and the limiter apply to it
 * as to the rest, and USB audio carries it. Audio ISR only (seq_tick sets the click, app_audio plays it, one ISR); the
 * main loop sets cl_on (not saved: off at every power-on) and lights F#3 with the clicks (click_flash). A click starts on the control step the tick came in
 * (voices_render's vr_frame), as a note does. */

#define CL_BEAT 96u                             /* ticks a beat (seq.c: SEQ_BAR is 4 of them) */
#define CL_LEN 1500u                            /* frames a click lasts (~34 ms; its tail is under -60 dB by then) */
#define CL_DECAY 32600                          /* the envelope's one-pole a frame (Q15): ~-39 dB over 20 ms */
#define CL_HZ 1000u                             /* a beat's pitch */
#define CL_HZ_ACC 1600u                         /* beat 1's */
#define CL_AMP 8000                             /* a beat's level (Q15, in the mix before the output stage: after the headroom
                                                 * and MASTER 3/4 about -18 dBFS) */
#define CL_AMP_ACC 12000                        /* beat 1's: 3.5 dB more */
#define CL_INC ((uint32_t)(((uint64_t)CL_HZ << 32) / FS))        /* a frame's phase step: a cycle is 2^32 (folded at compile time) */
#define CL_INC_ACC ((uint32_t)(((uint64_t)CL_HZ_ACC << 32) / FS))
#define CL_NONE 0xFFFFu
static volatile uint32_t cl_on;                 /* main loop: CLICK on (not a setting: off at power-on) */
static uint32_t cl_at = CL_NONE;                /* audio: a click due in this block, at this frame (CL_NONE: none) */
static uint32_t cl_acc;                         /* audio: .. it is beat 1's */
static uint32_t cl_n, cl_ph, cl_inc;            /* audio: frames of the click left (0: silent), its phase, its step */
static int32_t cl_env, cl_amp;                  /* audio: its envelope (Q15), its level */

#define CL_FLASH 60u                            /* the F#3 light's flash on a beat (ms), */
#define CL_FLASH_ACC 160u                       /* .. on beat 1 (the key's LED has no brightness between off and on: a longer one) */
static volatile uint32_t cl_beats, cl_beat1;    /* audio: the clicks started so far; the last one was beat 1's (main loop: the light) */
static uint32_t cl_seen, cl_until;              /* main loop: the clicks seen; the flash lasts until (fm1_ms) */

static void click_tick(uint32_t ticks)          /* audio, a tick while playing (seq_tick; ticks: seq_ticks before it) */
{
    if (!cl_on || ticks % CL_BEAT) return;
    cl_at = vr_frame;
    cl_acc = !(ticks % (4u * CL_BEAT));
    cl_beat1 = cl_acc, cl_beats++;
}
static int click_flash(void)                    /* main loop: 1 while F#3 flashes with a click (CLICK on, a beat just came in) */
{
    if (cl_beats != cl_seen) {
        cl_seen = cl_beats;
        cl_until = fm1_ms + (cl_beat1 ? CL_FLASH_ACC : CL_FLASH);
    }
    return cl_on && (int32_t)(cl_until - fm1_ms) > 0;
}
static void click_render(int32_t *out, uint32_t n)   /* audio: n frames (Q15 << PB, left and right) of the click into out */
{
    uint32_t i;
    if (!cl_on) {                               /* (switched off: at once) */
        cl_n = 0, cl_at = CL_NONE;
        return;
    }
    if (!cl_n && cl_at >= n) {                  /* (silent, nothing due: no work) */
        cl_at = CL_NONE;
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t s;
        if (i == cl_at) {                       /* a click starts here (one still sounding: over) */
            cl_n = CL_LEN, cl_ph = 0, cl_env = 32767;
            cl_inc = cl_acc ? CL_INC_ACC : CL_INC;
            cl_amp = cl_acc ? CL_AMP_ACC : CL_AMP;
        }
        if (!cl_n) continue;
        s = (((sine_i(cl_ph) * cl_env) >> 15) * cl_amp) >> 15;
        out[2u * i] += s << PB, out[2u * i + 1u] += s << PB;
        cl_ph += cl_inc;
        cl_env = (cl_env * CL_DECAY) >> 15;
        cl_n--;
    }
    cl_at = CL_NONE;
}
