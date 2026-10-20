/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V2's MIDI clock: SYSTEM 2's CLOCK (grimm.c includes this file after the sequencer and the arp).
 *   INT      the FM-1's own tempo (SELECT / PRESETS on HOME); the clock and START / STOP from outside ignored
 *   INT+OUT  the same, and its clock out over USB (the jack has no output): F8 every 4 of the sequencer's ticks (24 a
 *            beat), also while stopped (the other gear knows the tempo before PLAY), on one tick count that never skips:
 *            PLAY starts at the next pulse, FA and that pulse (the downbeat) together; FC as it stops. Each goes out
 *            when its sound plays (platform.c rt_flush): ~0.1 ms of jitter
 *   USB, TRS the clock from there, after SLOOP's mclk (isod89/sloop-fm1 seq.c, after Felucca 1.0's midi_clock.c, from
 *            contributions by ChanceTheMaker and keremimo; GPL-3.0; GR!MM's own code): a pulse is 4 of the sequencer's
 *            96 ticks a beat, the first on the pulse, the other three between it and the next by the pulses' interval
 *            (smoothed), but never past the next pulse: the FM-1 follows the master's tempo and cannot drift. The tempo
 *            itself: per beat (24 pulses), smoothed; HOME shows "EXT" and it. START: from step 1, at its first pulse;
 *            CONTINUE: on from where STOP left it; STOP. PLAY on the FM-1 starts at the next pulse. No pulse for 0.5 s:
 *            the FM-1's own tempo again (playing on). The arp, song mode and a slot "at the next bar" go by the ticks;
 *            SELECT and PRESETS leave the tempo alone meanwhile.
 * The messages come with their time (platform.c rt_in) and act in the control block of that time one audio block
 * later (CK_LAT): by then every one has come, so where a render falls adds no jitter (USB's and the jack's MIDI are
 * read within 0.5 ms, platform.c). All of it in the audio ISR (ctl_hook); INT costs one test a control block. */
static const char *const CK_NAME[CK_N] = {"INT", "INT+OUT", "USB", "TRS"};
#define CK_LAT 139320u                          /* an audio block (256 frames) in rt_now's ticks (24 a us) */
#define CK_CTL 17415u                           /* a control block (32 frames): 32 / 44100 s (17414.97: a block's
                                                 * eight from its own start, so it never adds up) */
#define CK_SAMP 544u                            /* a frame (544.2) */
#define CK_LOST 12000000u                       /* 0.5 s without a pulse: the clock is gone */
#define CK_GAP 4800000u                         /* 0.2 s: two pulses further apart are a gap, not a tempo */
static volatile uint32_t ck_ext;                /* audio: the ticks come from a MIDI clock now */
static volatile uint32_t ck_bpm10;              /* audio: its tempo, x 10 (0: not measured yet) */
static volatile uint32_t ck_trans;              /* audio: + 1 at each START / CONTINUE / STOP from there (the main
                                                 * loop's PLAY follows seq_on) */
static volatile uint32_t ck_slot;               /* main loop: the slot PLAY plays (a START's) */
static uint32_t ck_acc, ck_co;                  /* audio, INT+OUT: the tempo's samples (Q16), its ticks (a pulse each 4) */
static struct {
    uint32_t last, iv, beat_t, beat;            /* the last pulse; their interval (smoothed); a beat's first pulse; a
                                                 * beat (smoothed) */
    uint32_t p, done, n24;                      /* pulses since the clock came, ticks played since; pulses into a beat */
    uint8_t go, cont, alive, was;               /* START, CONTINUE waiting for a pulse; pulses coming; the last CLOCK */
} ck;
static struct {                                 /* STOP's place (CONTINUE) */
    int32_t pos[NT], dir[NT];
    uint32_t t[NT], ticks, left, st, len;
    int32_t at;
    uint8_t ok;
} ck_rest;

/* audio: a message out (CLOCK INT+OUT) when the sound of now plays; rem: what the tick's accumulator kept (Q16 frames
 * past the tick within this control block: CTL << 16, the block's start) */
static void ck_out(uint32_t st, uint32_t rem)
{
    uint32_t f = rem >> 16;
    f = f < CTL ? CTL - f : 0u;
    rt_out(st, ck_e + CK_LAT + ck_j * CK_CTL + f * CK_SAMP);
}

static void ck_tick(void)                       /* audio: a tick of the clock's */
{
    uint32_t t;
    ck.done++;
    if (seq_on) seq_tick();                     /* (a song's end stops it: song_end, the main loop's PLAY) */
    else
        for (t = 0; t < NT; t++) arp_tick(t, 0);
}
static void ck_pause(void)                      /* audio: STOP from there: its place kept for CONTINUE, then stopped */
{
    uint32_t t;
    for (t = 0; t < NT; t++) ck_rest.pos[t] = seq[t].pos, ck_rest.t[t] = seq[t].t, ck_rest.dir[t] = seq[t].dir;
    ck_rest.ticks = seq_ticks, ck_rest.at = song_at, ck_rest.left = song_left, ck_rest.st = song_t, ck_rest.len = song_len;
    ck_rest.ok = 1;
    seq_stop();
}
static void ck_resume(void)                     /* audio: CONTINUE: on from STOP's place */
{
    uint32_t t;
    for (t = 0; t < NT; t++) seq[t].pos = ck_rest.pos[t], seq[t].t = ck_rest.t[t], seq[t].dir = ck_rest.dir[t];
    seq_ticks = ck_rest.ticks, song_at = ck_rest.at, song_left = ck_rest.left, song_t = ck_rest.st, song_len = ck_rest.len;
    seq_on = 1;
}
static void ck_int_out(void)                    /* audio, a control block, INT+OUT: the FM-1's tempo, its pulses out */
{
    uint32_t tick = SEQ_TICK_Q16 / (uint32_t)clamp(seq_bpm, 20, 300), t;
    ck_acc += (uint32_t)CTL << 16;
    while (ck_acc >= tick) {
        ck_acc -= tick;
        if (!(ck_co % 4u)) {                    /* a pulse: PLAY waiting starts here (START out, then the downbeat) */
            if (ck.go && !seq_on) {
                seq_start((uint32_t)ck_slot & 3u);
                ck_out(0xFAu, ck_acc);
                ck.go = 0, ck_co = 0, ck_trans++;
            }
            ck_out(0xF8u, ck_acc);
        }
        ck_co++;
        if (seq_on)
            seq_tick();                         /* (a song's end stops it; the pulses go on) */
        else
            for (t = 0; t < NT; t++) arp_tick(t, 0);
    }
}
static void ck_lose(void)                       /* audio: no clock: the FM-1's own tempo from here (playing on) */
{
    ck.alive = 0, ck.iv = ck.beat = 0, ck.go = ck.cont = 0;
    ck_ext = 0, ck_bpm10 = 0;
    seq_acc = arp_acc = 0;
}
static void ck_pulse(uint32_t t)                /* audio: a pulse (F8) at t */
{
    uint32_t d = t - ck.last;
    if (ck.alive && d < CK_GAP) ck.iv = ck.iv ? (ck.iv * 3u + d + 2u) / 4u : d;   /* (the interval, smoothed) */
    if (!ck.alive) {                            /* the clock comes: counted from here */
        ck.alive = 1, ck_ext = 1;
        ck.p = ck.done = ck.n24 = 0, ck.beat_t = t;
    } else {
        while (ck.done < 4u * ck.p) ck_tick();  /* (the last pulse's ticks all played first) */
        if (++ck.n24 == 24u) {                  /* a beat: the tempo (20 .. 300 BPM) */
            uint32_t b = t - ck.beat_t;
            ck.n24 = 0, ck.beat_t = t;
            if (b >= 4800000u && b <= 72000000u) {
                ck.beat = ck.beat ? (ck.beat * 3u + b + 2u) / 4u : b;
                ck_bpm10 = 900000000u / (ck.beat >> 4);   /* (60 s x 24 MHz x 10 / beat) */
            }
        }
    }
    if (ck.go) {                                /* START (or PLAY): from step 1, this pulse the downbeat */
        seq_start((uint32_t)ck_slot & 3u);
        ck.go = 0, ck_trans++;
    } else if (ck.cont) {
        ck_resume();
        ck.cont = 0, ck_trans++;
    }
    ck.p++, ck.last = t;
}
static void ck_msg(uint32_t st, uint32_t t)     /* audio: a realtime message from CLOCK's source, at t */
{
    if (st == 0xF8u) {
        ck_pulse(t);
        return;
    }
    if (st == 0xFCu) {                          /* STOP */
        if (seq_on) ck_pause();
        ck.go = ck.cont = 0, ck_trans++;
        return;
    }
    if (st == 0xFBu && !seq_on && ck_rest.ok) { /* CONTINUE: from STOP's place, at the next pulse */
        ck.cont = 1, ck.go = 0;
        return;
    }
    if (seq_on) seq_stop();                     /* START (or a CONTINUE with no place): from step 1 at the next pulse */
    ck_rest.ok = 0, ck.go = 1, ck.cont = 0;
}
/* audio, a control block (ctl_hook): the sequencer's and the arps' ticks due, by the FM-1's tempo or the clock's */
static void clk_ctl(void)
{
    uint32_t m = ck_mode, v, r, due, k;
    if (m != ck.was) {                          /* CLOCK changed: a clock followed let go; INT+OUT: its tick count from
                                                 * the tempo's own (left playing: STOP out) */
        if (ck.was == CK_OUT && seq_on) ck_out(0xFCu, (uint32_t)CTL << 16);
        if (ck.was == CK_OUT) seq_acc = arp_acc = ck_acc;
        if (m == CK_OUT) ck_acc = seq_on ? seq_acc : arp_acc, ck_co = seq_ticks;
        if (ck.alive) ck_lose();
        ck.go = ck.cont = 0, ck.was = (uint8_t)m;
    }
    if (m < CK_USB) {                           /* INT, INT+OUT: the FM-1's tempo; realtime in ignored */
        rt_r = rt_w;
        if (m == CK_OUT) ck_int_out();
        else {
            seq_ctl();
            arp_ctl();
        }
        ck_j++;
        return;
    }
    v = ck_e + ck_j * CK_CTL - CK_LAT;          /* (this block's time, as the messages': CK_LAT back) */
    while ((r = rt_r) != rt_w && (int32_t)(rt_t[r % RTQ] - v) <= 0) {   /* the messages due */
        uint32_t q = rt_q[r % RTQ], t = rt_t[r % RTQ];
        rt_r = r + 1u;
        if ((q >> 8) == m - CK_USB) ck_msg(q & 0xFFu, t);   /* (the other source's: ignored) */
    }
    if (ck.alive && (int32_t)(v - ck.last) >= (int32_t)CK_LOST) ck_lose();
    if (!ck.alive) {                            /* (no clock yet, or gone: the FM-1's own) */
        seq_ctl();
        arp_ctl();
    } else {                                    /* the last pulse's first tick, then the others by its interval */
        due = 4u * ck.p - 3u;
        if (ck.iv) {
            k = (v - ck.last) * 4u / ck.iv;
            due += k < 3u ? k : 3u;
        }
        for (k = 0; ck.done < due && k < 16u; k++) ck_tick();
    }
    ck_j++;
}
