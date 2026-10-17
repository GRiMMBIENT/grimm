/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's song mode (GLO: the SONG page; seq.c includes this file): a chain of up to SONG_N steps, each a slot (A..D)
 * played REPEATS passes, or END (the chain stops there). A pass: one run of the slot's longest pattern (its STEPS times
 * its DIVISION's ticks), every track from its first step again at each pass, so all of them stay in time.
 *   MODE OFF: PLAY plays the slot in use (as before); SONG: PLAY runs the chain from its first step once, then stops;
 *   SONG LOOP: round and round. The next step comes in exactly at the end of a pass (seq_jump, as a slot recalled).
 *   song record (REC on the SONG page; PLAY, or playing already): the slot in use is the chain's first step; a slot
 *   switched to (SAVE held + keys 1..4) comes in at the end of the pass and is the next step, the passes the last one
 *   played its REPEATS; STOP or REC again: the last one written (a pass begun: counted), then END.
 * The chain and MODE: the main loop's (SONG's knobs) and the project's (store.c); played in the audio ISR, a tick at a
 * time (seq_ctl: song_tick). */

#define SONG_N 16u
#define SONG_END 4                              /* PATTERN: A..D (0..3), END */
enum { SM_OFF, SM_SONG, SM_LOOP, NSONGMODE };
static const char *const SONG_MODE_NAME[NSONGMODE] = {"OFF", "SONG", "SONG LOOP"};
static const char *const SONG_MODE_SHORT[NSONGMODE] = {"OFF", "SONG", "LOOP"};   /* (slabs) */
static volatile int32_t song_pat[SONG_N] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};   /* the chain: a slot, or END */
static volatile int32_t song_rep[SONG_N] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};   /* .. its REPEATS 1..16 */
static volatile int32_t song_mode, song_sect;   /* MODE; SECTION (the step SONG's knobs edit, 0..15) */
static volatile int32_t song_at = -1;           /* audio: the chain's step playing (-1: no song playing) */
static volatile uint32_t song_left;             /* audio: its passes left (the one playing counted) */
static volatile uint32_t song_rec;              /* song record: 0 off; 1 armed (main loop); 2 recording (audio) */
static volatile uint32_t song_end;              /* audio: a SONG played out and stopped (main loop: PLAY off) */
static volatile uint32_t song_ri;               /* audio, recording: the chain's step being written */
static uint32_t song_rp, song_t, song_len;      /* audio: recording's passes; ticks into the pass, a pass's ticks */
static void seq_stop(void);

static uint32_t song_pass(void)                 /* a pass's ticks: the longest pattern's */
{
    uint32_t t, n, m = 0;
    for (t = 0; t < NT; t++) {
        n = (uint32_t)seq_steps(&seq[t]) * SEQ_DIV_T[clamp(seq[t].div, 0, 7)];
        if (n > m) m = n;
    }
    return m ? m : 1u;
}
static uint32_t song_length(void)               /* the chain's steps before its END */
{
    uint32_t i;
    for (i = 0; i < SONG_N && song_pat[i] >= 0 && song_pat[i] < SONG_END; i++)
        ;
    return i;
}
static int32_t song_cur(void)                   /* main loop: the step SONG shows: recording's, playing's, else
                                                 * SECTION's */
{
    return song_rec == 2u ? (int32_t)song_ri : song_at >= 0 ? song_at : clamp(song_sect, 0, SONG_N - 1);
}
static void song_go(uint32_t i)                 /* audio: the chain's step i in, from the start of a pass */
{
    song_at = (int32_t)i;
    song_left = (uint32_t)clamp(song_rep[i], 1, 16);
    seq_jump((uint32_t)song_pat[i]);
}
static void song_rec_begin(uint32_t slot)       /* audio: song record starts: the slot in use, the chain's first step */
{
    song_rec = 2u, song_ri = 0, song_rp = 0;
    song_pat[0] = (int32_t)(slot & 3u);
}
static void song_stop(void)                     /* audio: recording: its last step's passes (a pass begun: one), END */
{
    song_at = -1;
    if (song_rec != 2u) return;
    song_rep[song_ri] = clamp((int32_t)(song_rp + (song_t > 0u)), 1, 16);
    if (song_ri + 1u < SONG_N) song_pat[song_ri + 1u] = SONG_END;
    song_rec = 0;
}
static void song_start(uint32_t slot)           /* audio (PLAY): song record armed: it starts; else a song (MODE), from
                                                 * its first step */
{
    song_at = -1, song_t = 0;
    if (song_rec == 1u) song_rec_begin(slot);
    else if (song_mode != SM_OFF && song_length()) song_go(0);
    song_len = song_pass();
}
static int song_tick(void)                      /* audio, a tick while playing: a pass ends: the song's next step (or
                                                 * the same again), a recorded switch; 1: the song is over, stopped */
{
    uint32_t n;
    if (song_at >= 0 && song_mode == SM_OFF) song_at = -1;   /* (MODE OFF while it plays: the slot plays on) */
    if (song_at < 0 && song_rec != 2u) return 0;
    if (song_t < song_len) {
        song_t++;
        return 0;
    }
    song_t = 1;                                 /* (this tick: the next pass's first) */
    if (song_rec == 2u) {                       /* recording: a pass more; a slot switched to: the next step */
        song_rp++;
        if (seq_recall >= 0) {
            uint32_t s = (uint32_t)seq_recall;
            seq_recall = -1;
            song_rep[song_ri] = clamp((int32_t)song_rp, 1, 16);
            song_rp = 0;
            if (song_ri + 1u < SONG_N) song_pat[++song_ri] = (int32_t)s;
            else song_rec = 0;                  /* (the chain full: recording ends) */
            seq_jump(s);
        } else
            seq_rewind();
    } else if (--song_left)                     /* the same step again */
        seq_rewind();
    else {
        n = (uint32_t)song_at + 1u;
        if (n >= SONG_N || song_pat[n] < 0 || song_pat[n] >= SONG_END) {   /* the END */
            if (song_mode != SM_LOOP) {
                seq_stop();
                song_end = 1;
                return 1;
            }
            n = 0;
        }
        song_go(n);
    }
    song_len = song_pass();
    return 0;
}
