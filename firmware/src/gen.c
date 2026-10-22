/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V2's GENERATE: a track plays random notes of the SCALE (its chord in chord mode, as a key's) on the tempo, the
 * sequencer's clock or a MIDI clock (arp.c's arp_tick, a tick of each track: gen_tick), with rests, lengths, a volume
 * and an attack of their own each note (voices.c: gen_vel, gen_atk). FX held + the black keys F#3 G#3 A#3 C#4: on and
 * off for T1 .. T4 (lit while on, FX held); + D#4: MODE, the same for all four:
 *   DRIFT  a few notes, long (2 .. 8 beats), soft, slow attacks, low (ambient)
 *   PULSE  a melody walking the scale on eighths, mostly on the beat, steadier
 *   STORM  dense sixteenths, short, the volume swinging wildly, wide
 * While a track generates its pattern plays on unheard (its steps, ratchets and locks not played: seq.c), so it comes
 * back in time when it stops; GENERATE never writes into it. Its notes take the track's voices as a key's do (the
 * voice rule: none taken from another track). Nothing saved: all off at power-on. The main loop sets gen_on and
 * gen_mode; the audio ISR plays. */

enum { GM_DRIFT, GM_PULSE, GM_STORM, NGM };
static const char *const GM_NAME[NGM] = {"DRIFT", "PULSE", "STORM"};
#define GEN_ID 200u                             /* its notes' origin (lat_id: past the keys' and MIDI's) */
static volatile uint32_t gen_mode;              /* main loop: MODE (GM_) */
static const struct {
    uint8_t grid, on, off;                      /* a decision every grid ticks; a note's chance (%), on a beat, off it */
    uint8_t len_lo, len_hi;                     /* its length in grids x 4 (from .. to) */
    uint8_t vel_lo, vel_hi, atk_lo, atk_hi;     /* its volume (x / 256), its attack (GEN_ATK's) */
    uint8_t low, span, walk;                    /* its notes: from the keys' lowest + low, over span semitones; walk:
                                                 * steps from the last (0: anywhere) */
} GM[NGM] = {
    {96, 30, 30, 8, 32, 80, 160, 4, 7, 0, 17, 0},       /* DRIFT: quarters, a third of them, 2 .. 8 beats */
    {48, 85, 30, 3, 10, 150, 255, 0, 2, 3, 20, 5},      /* PULSE: eighths, on the beat mostly, 0.75 .. 2.5 eighths */
    {24, 70, 70, 2, 8, 30, 255, 0, 3, 0, 27, 0},        /* STORM: sixteenths, most of them, 0.5 .. 2 sixteenths */
};
static struct {
    uint16_t wait, left;                        /* ticks to the next decision; to the note's end (0: none on) */
    uint8_t n, note, last, was;                 /* decisions made (on the beat: even); the note on (0: none); the
                                                 * last one (PULSE walks from it); on */
} gen[NT];
static uint32_t gen_rng = 0x6C8E9CF5u;
static uint32_t gen_rnd(uint32_t n)
{
    gen_rng ^= gen_rng << 13, gen_rng ^= gen_rng >> 17, gen_rng ^= gen_rng << 5;
    return (uint32_t)(((uint64_t)gen_rng * n) >> 32);
}
static void gen_tick(uint32_t t)                /* audio: a tick of track t */
{
    uint32_t on = gen_on >> t & 1u, m = gen_mode < NGM ? gen_mode : 0u, i, lo, nt;
    if (on != gen[t].was) {                     /* switched: on, the pattern's notes out (it plays on unheard); off,
                                                 * the generated note out */
        gen[t].was = (uint8_t)on;
        if (on) {
            for (i = 0; i < SEQ_NOFF; i++)
                if (seq[t].off_left[i]) note_off(t, seq[t].off_note[i], SRC_SEQ, 0), seq[t].off_left[i] = 0;
            seq[t].r_left = 0, lk_m[t] = 0;
            gen[t].wait = 0, gen[t].n = 0;
        } else if (gen[t].note)
            note_off(t, GEN_ID, SRC_GEN, 0), gen[t].note = 0, gen[t].left = 0;
    }
    if (!on) return;
    if (gen[t].left && !--gen[t].left) note_off(t, GEN_ID, SRC_GEN, 0), gen[t].note = 0;
    if (gen[t].wait && --gen[t].wait) return;
    gen[t].wait = GM[m].grid;                   /* (a decision: on the grid) */
    if (gen[t].note || gen_rnd(100) >= (gen[t].n++ & 1u ? GM[m].off : GM[m].on)) return;   /* (one sounding: on; else
                                                 * a rest, by chance) */
    lo = (uint32_t)((int32_t)KEY0 + 12 * clamp(set.oct, -2, 2)) + GM[m].low;
    nt = lo + gen_rnd(GM[m].span + 1u);
    if (GM[m].walk && gen[t].last)              /* (PULSE: a few semitones from its last note, within its range) */
        nt = (uint32_t)clamp((int32_t)gen[t].last + (int32_t)gen_rnd(2u * GM[m].walk + 1u) - (int32_t)GM[m].walk,
                             (int32_t)lo, (int32_t)(lo + GM[m].span));
    nt = sc_snap(nt);                           /* (the SCALE's nearest note) */
    gen[t].last = (uint8_t)nt;
    gen_vel = (uint8_t)(GM[m].vel_lo + gen_rnd(GM[m].vel_hi - GM[m].vel_lo + 1u));
    gen_atk = (uint8_t)(1u + GM[m].atk_lo + gen_rnd(GM[m].atk_hi - GM[m].atk_lo + 1u));
    note_on(t, GEN_ID, nt, SRC_GEN, 0, -1);     /* (chord mode: its chord, as a key's) */
    gen_vel = gen_atk = 0;
    gen[t].note = (uint8_t)nt;
    gen[t].left = (uint16_t)(GM[m].grid * (GM[m].len_lo + gen_rnd(GM[m].len_hi - GM[m].len_lo + 1u)) / 4u);
}
