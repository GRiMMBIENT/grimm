/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's arpeggiator: a track's ARP (ARP held: on / off; MODE, RATE, OCTAVES, GATE: the track's, saved with the project
 * and the slots, not with sounds).
 *   ARP on: the track's keys and MIDI notes (after SCALE; chord mode on: each its whole chord) go into its list instead
 *   of sounding; the arp plays them a step at a time in MODE's order (UP, DOWN, UP-DOWN, RANDOM, AS PLAYED; CHORD: all
 *   of them each step), OCTAVES times an octave higher, each note GATE of its step long
 *   LATCH OFF: a key's notes leave the list when it is let go; LATCH ON: they stay, the key again takes them out; an
 *   empty list stops it (its note released), as does ARP off
 *   the clock: the sequencer's when it plays (its steps on the sequencer's grid), else the tempo's, from the first note
 *   held (that note at once)
 *   REC on and the sequencer playing: the selected track's arp notes are live-recorded into its sequence
 * Its notes: grimm.c note_on / note_off as SRC_ARP (their origin: the note), the track's source and ENV, no chord of
 * their own. Audio ISR; the main loop reads the list (the screen, the keys' lights). */

enum { ARP_UP, ARP_DOWN, ARP_UPDN, ARP_RND, ARP_PLAYED, ARP_CHORD, NARPMODE };
#define NARPRATE 9
#define ARP_N 16u                               /* notes in a track's list at most */
#define ARP_PN (2u * ARP_N * 4u)                /* a pattern's steps at most (UP-DOWN over 4 octaves) */
static const uint16_t ARP_RATE_T[NARPRATE] = {384, 192, 96, 48, 24, 12, 64, 32, 16};   /* RATE: ticks a step */
static const char *const ARP_RATE_NAME[NARPRATE] = {"1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T"};
static const char *const ARP_MODE_NAME[NARPMODE] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "AS PLAYED", "CHORD"};
static const char *const ARP_MODE_SHORT[NARPMODE] = {"UP", "DOWN", "UPDN", "RAND", "PLAYED", "CHORD"};   /* (slabs) */

typedef struct {
    uint8_t note[ARP_N], id[ARP_N], root[ARP_N];   /* the list: the notes, where they came from (lat_id), the note
                                                 * played (before its chord) */
    volatile uint32_t n;                        /* .. how many */
    uint32_t on[4];                             /* the notes it has on now (bit: the note) */
    uint32_t ft, since;                         /* its own clock (ticks since its first note); ticks since its step */
    volatile uint32_t pos;                      /* the step to come */
    volatile int32_t last;                      /* the step played last (the screen; -1: none) */
    uint32_t was;                               /* ARP on, as last seen */
} arp_t;
static arp_t arp[NT];
static volatile int32_t arp_rec = -1;           /* main loop: the track live-recording its arp (REC, playing), or -1 */
static uint32_t arp_acc;                        /* audio: the clock's samples (Q16) while the sequencer stands */

/* track t's steps into p (at most ARP_PN), MODE's order over OCTAVES (oct); CHORD: the list's notes, low to high, once */
static uint32_t arp_pattern(uint32_t t, uint8_t *p, uint32_t oct)
{
    const arp_t *A = &arp[t];
    uint8_t b[ARP_N];
    uint32_t n = 0, i, j, k = 0, o, cnt = A->n < ARP_N ? A->n : ARP_N, mode = (uint32_t)clamp(trk[t].arp_mode, 0, NARPMODE - 1);
    for (i = 0; i < cnt; i++) {                 /* each note once (AS PLAYED: in the order they came) */
        for (j = 0; j < n && b[j] != A->note[i]; j++)
            ;
        if (j == n) b[n++] = A->note[i];
    }
    if (mode != ARP_PLAYED)                     /* low to high */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && b[j - 1] > b[j]; j--) {
                uint8_t x = b[j];
                b[j] = b[j - 1], b[j - 1] = x;
            }
    if (mode == ARP_CHORD) oct = 1;
    for (o = 0; o < oct; o++)
        for (i = 0; i < n; i++)
            if (b[i] + 12u * o <= 127u) p[k++] = (uint8_t)(b[i] + 12u * o);
    if (mode == ARP_DOWN)
        for (i = 0; i < k / 2u; i++) {
            uint8_t x = p[i];
            p[i] = p[k - 1u - i], p[k - 1u - i] = x;
        }
    else if (mode == ARP_UPDN && k > 2u) {      /* up, then down (the ends once) */
        for (i = k - 1u, j = k; i-- > 1u;)
            p[j++] = p[i];
        k = j;
    }
    return k;
}

static void arp_release(uint32_t t)             /* audio: its notes on, off */
{
    uint32_t w, b;
    for (w = 0; w < 4u; w++)
        for (b = 0; arp[t].on[w] && b < 32u; b++)
            if (arp[t].on[w] >> b & 1u) {
                note_off(t, w * 32u + b, SRC_ARP, 0);
                arp[t].on[w] &= ~(1u << b);
            }
}
static void arp_clear(uint32_t t)               /* audio: the list emptied (its keys' echoes off), its note off */
{
    uint32_t i, j;
    for (i = 0; i < arp[t].n && i < ARP_N; i++) {
        for (j = 0; j < i && arp[t].id[j] != arp[t].id[i]; j++)
            ;
        if (j == i && arp[t].id[i] < 27u) ec_put(0x80, t, arp[t].root[i]);   /* (a key's: echoed on as it came in) */
    }
    arp_release(t);
    arp[t].n = 0, arp[t].pos = 0, arp[t].last = -1;
}
static void arp_play(uint32_t t, uint32_t note)
{
    if (note > 127u) return;
    note_on(t, note, note, SRC_ARP, 0, 0);      /* (no chord of its own) */
    arp[t].on[note >> 5] |= 1u << (note & 31u);
}
static void arp_step(uint32_t t)                /* audio: the step due: its note (CHORD: its notes) on */
{
    arp_t *A = &arp[t];
    uint8_t p[ARP_PN];
    uint32_t oct = (uint32_t)clamp(trk[t].arp_oct, 1, 4), len = arp_pattern(t, p, oct), i, first;
    arp_release(t);
    if (!len) return;
    if (clamp(trk[t].arp_mode, 0, NARPMODE - 1) == ARP_CHORD) {   /* all of them, an octave up a step */
        uint32_t o = A->pos % oct;
        for (i = 0; i < len; i++) arp_play(t, p[i] + 12u * o);
        first = p[0] + 12u * o, A->last = (int32_t)o, A->pos = o + 1u;
    } else {
        i = clamp(trk[t].arp_mode, 0, NARPMODE - 1) == ARP_RND ? seq_rnd() % len : A->pos % len;
        arp_play(t, p[i]);
        first = p[i], A->last = (int32_t)i, A->pos = i + 1u;
    }
    if (arp_rec == (int32_t)t && seq_on && first <= 127u) {   /* REC: into the sequence, as a key's live note */
        int32_t s = seq_nearest(t);
        if (s >= 0) seq_write(t, s, first, 0u);   /* (its notes: notes, not chord steps) */
    }
}
static void arp_tick(uint32_t t, int locked)    /* audio: a tick of track t's arp (locked: the sequencer's tick) */
{
    arp_t *A = &arp[t];
    uint32_t r, g, due;
    if (!A->n) return;
    r = ARP_RATE_T[clamp(trk[t].arp_rate, 0, NARPRATE - 1)];
    g = r * (uint32_t)clamp(trk[t].arp_gate, 10, 100) / 100u;
    if (++A->since >= (g ? g : 1u)) arp_release(t);   /* GATE: the note's end */
    due = locked ? (seq_ticks - 1u) % r == 0u : A->ft % r == 0u;
    A->ft++;
    if (due) {
        A->since = 0;
        arp_step(t);
    }
}

/* audio: a key or a MIDI note (origin id, note after SCALE) of a track with ARP on: its notes (its chord) into the
 * list (in it already: anew), out of it */
static int arp_has(uint32_t t, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < arp[t].n; i++)
        if (arp[t].id[i] == id) return 1;
    return 0;
}
static void arp_take(uint32_t t, uint32_t id, int echo)
{
    arp_t *A = &arp[t];
    uint32_t i, k = 0, root = 255u;
    for (i = 0; i < A->n; i++)
        if (A->id[i] != id) {
            A->note[k] = A->note[i], A->id[k] = A->id[i], A->root[k] = A->root[i];
            k++;
        } else
            root = A->root[i];
    if (root <= 127u && echo) ec_put(0x80, t, root);
    A->n = k;
    if (!k) arp_clear(t);                       /* (none left: it stops) */
}
static void arp_add(uint32_t t, uint32_t id, uint32_t note, int echo)
{
    arp_t *A = &arp[t];
    uint8_t ns[CH_MAX];
    uint32_t n = 1, i;
    if (note > 127u) return;
    if (arp_has(t, id)) arp_take(t, id, echo);
    ns[0] = (uint8_t)note;
    if (trk[t].chord) n = chord_notes(t, note, ns);
    if (!A->n) A->ft = 0, A->since = 0, A->pos = 0;   /* the first: it starts (its own clock: at once) */
    for (i = 0; i < n && A->n < ARP_N; i++) {
        A->note[A->n] = ns[i], A->id[A->n] = (uint8_t)id, A->root[A->n] = (uint8_t)note;
        A->n++;
    }
    if (echo) ec_put(0x90, t, note);
}
static void arp_watch(void)                     /* audio: ARP switched on or off: the list starts empty (on: the track's
                                                 * key notes released first) */
{
    uint32_t t, on;
    for (t = 0; t < NT; t++)
        if ((on = trk[t].arp != 0) != arp[t].was) {
            arp[t].was = on;
            arp_clear(t);
            if (on) release_keys(t);
        }
}
static void arp_ctl(void)                       /* audio, a control block: the clock's ticks while the sequencer stands */
{
    uint32_t tick = SEQ_TICK_Q16 / (uint32_t)clamp(seq_bpm, 20, 300), t;
    if (seq_on) { arp_acc = 0; return; }        /* (playing: seq_ctl's ticks) */
    arp_acc += (uint32_t)CTL << 16;
    while (arp_acc >= tick) {
        arp_acc -= tick;
        for (t = 0; t < NT; t++)
            arp_tick(t, 0);
    }
}
