/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V2's GENERATE: a track plays random notes of the SCALE (its chord in chord mode, as a key's) on the tempo, the
 * sequencer's clock or a MIDI clock (arp.c's arp_tick, a tick of each track: gen_tick), with rests, lengths, a volume
 * and an attack of their own each note (voices.c: gen_vel, gen_atk), within its own range (gen_lo, gen_hi: its chord's
 * notes past it folded in by octaves, grimm.c note_on). FX held + the black keys (grimm.c): F#3 G#3 A#3 C#4 tapped
 * (let go, nothing else pressed meanwhile): T1 .. T4 on, off; D#4 MODE, F#4 G#4 its OCTAVE down, up, A#4 its RANGE:
 * the tracks' black keys held, theirs; none, all four's. Each track's own:
 *   MODE   DRIFT: a few notes, long (2 .. 8 beats), soft, slow attacks (ambient); PULSE: a melody walking the scale on
 *          eighths, mostly on the beat, steadier; STORM: dense sixteenths, short, the volume swinging wildly
 *   OCTAVE its notes from C3 + 12 x it (-3 .. +3: C0 .. C6), the keyboard's OCT not at all
 *   RANGE  1, 2 or 3 octaves up from there
 * (V2) A SAMPLE track's notes: lengths from the MODE's shortest up to the whole loop (its LENGTH; gen_smp). Two tracks
 * generating or more: each its share of the voices, a new note replacing its own track's oldest (grimm.c gen_share).
 * While a track generates its pattern plays on unheard (its steps, ratchets and locks not played: seq.c), so it comes
 * back in time when it stops; GENERATE never writes into it. Its notes take the track's voices as a key's do (the
 * voice rule: none taken from another track). Nothing saved: at power-on all off, DRIFT, OCTAVE 0, RANGE 2. The main
 * loop sets gen_on, gen_md, gen_oc, gen_rg; the audio ISR plays (gen_hit: a note played, the PERFORM screen's flash). */

enum { GM_DRIFT, GM_PULSE, GM_STORM, NGM };
static const char *const GM_NAME[NGM] = {"DRIFT", "PULSE", "STORM"};
#define GEN_ID 200u                             /* its notes' origin (lat_id: past the keys' and MIDI's) */
#define GEN_C3 48                               /* OCTAVE 0's lowest note */
static volatile uint8_t gen_md[NT];             /* main loop: a track's MODE (GM_), */
static volatile int8_t gen_oc[NT];              /* .. OCTAVE (-3 .. 3), */
static volatile uint8_t gen_rg[NT] = {2, 2, 2, 2};   /* .. RANGE (octaves, 1 .. 3) */
static volatile uint8_t gen_hit[NT];            /* audio: + 1 a note played (the screen's flash) */
static const struct {
    uint8_t grid, on, off;                      /* a decision every grid ticks; a note's chance (%), on a beat, off it */
    uint8_t len_lo, len_hi;                     /* its length in grids x 4 (from .. to) */
    uint8_t vel_lo, vel_hi, atk_lo, atk_hi;     /* its volume (x / 256), its attack (GEN_ATK's) */
    uint8_t walk;                               /* steps from the last note (semitones; 0: anywhere in the range) */
} GM[NGM] = {
    {96, 30, 30, 8, 32, 80, 160, 4, 7, 0},      /* DRIFT: quarters, a third of them, 2 .. 8 beats */
    {48, 85, 30, 3, 10, 150, 255, 0, 2, 5},     /* PULSE: eighths, on the beat mostly, 0.75 .. 2.5 eighths */
    {24, 70, 70, 2, 8, 30, 255, 0, 3, 0},       /* STORM: sixteenths, most of them, 0.5 .. 2 sixteenths */
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
static uint32_t gen_low(uint32_t t)             /* track t's range: its lowest note, */
{
    return (uint32_t)(GEN_C3 + 12 * clamp(gen_oc[t], -3, 3));
}
static uint32_t gen_high(uint32_t t)            /* .. its highest */
{
    uint32_t h = gen_low(t) + 12u * (uint32_t)clamp(gen_rg[t], 1, 3) - 1u;
    return h > 127u ? 127u : h;
}
static uint32_t gen_smp(uint32_t t)            /* (V2) a SAMPLE track: its loop (LENGTH, as the screen has it, at the
                                                 * root) in ticks at the tempo now (96 a beat); another source, an empty
                                                 * slot: 0 */
{
    sp_loop_t L;
    uint32_t ms, bpm = ck_ext && ck_bpm10 ? ck_bpm10 / 10u : (uint32_t)clamp(seq_bpm, 20, 300);
    if (trk[t].eng != ENG_SAMPLE || !sp_loop(trk[t].p[ENG_SAMPLE], &L)) return 0;
    ms = L.len * 20u / 441u;                    /* (22050 Hz) */
    ms = ms * bpm * 2u / 1250u;                 /* (ms x BPM x 96 / 60000) */
    return ms > 65535u ? 65535u : ms;
}
static uint32_t gen_snap(uint32_t n, uint32_t lo, uint32_t hi)   /* n to the SCALE's nearest note within lo .. hi */
{
    uint32_t m = sc_snap(n);
    if (m < lo)
        for (m = lo; m < hi && !sc_in((int32_t)m); m++)
            ;
    if (m > hi)
        for (m = hi; m > lo && !sc_in((int32_t)m); m--)
            ;
    return m;
}
static void gen_tick(uint32_t t)                /* audio: a tick of track t */
{
    uint32_t on = gen_on >> t & 1u, m = gen_md[t] < NGM ? gen_md[t] : 0u, i, lo, hi, nt;
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
    lo = gen_low(t), hi = gen_high(t);
    nt = lo + gen_rnd(hi - lo + 1u);
    if (GM[m].walk && gen[t].last >= lo && gen[t].last <= hi)   /* (PULSE: a few semitones from its last note) */
        nt = (uint32_t)clamp((int32_t)gen[t].last + (int32_t)gen_rnd(2u * GM[m].walk + 1u) - (int32_t)GM[m].walk,
                             (int32_t)lo, (int32_t)hi);
    nt = gen_snap(nt, lo, hi);                  /* (the SCALE's nearest note, in the range) */
    gen[t].last = (uint8_t)nt;
    gen_vel = (uint8_t)(GM[m].vel_lo + gen_rnd(GM[m].vel_hi - GM[m].vel_lo + 1u));
    gen_atk = (uint8_t)(1u + GM[m].atk_lo + gen_rnd(GM[m].atk_hi - GM[m].atk_lo + 1u));
    gen_lo = (uint8_t)lo, gen_hi = (uint8_t)hi;
    note_on(t, GEN_ID, nt, SRC_GEN, 0, -1);     /* (chord mode: its chord, as a key's, folded into the range) */
    gen_vel = gen_atk = gen_lo = gen_hi = 0;
    gen[t].note = (uint8_t)nt;
    if ((i = gen_smp(t)) > GM[m].grid * GM[m].len_hi / 4u)   /* (V2: SAMPLE: from the MODE's shortest up to the
                                                 * whole loop) */
        gen[t].left = (uint16_t)(GM[m].grid * GM[m].len_lo / 4u + gen_rnd(i - GM[m].grid * GM[m].len_lo / 4u + 1u));
    else
        gen[t].left = (uint16_t)(GM[m].grid * (GM[m].len_lo + gen_rnd(GM[m].len_hi - GM[m].len_lo + 1u)) / 4u);
    gen_hit[t]++;
}
