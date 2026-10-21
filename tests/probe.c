/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Host mode's "T probe" (tests/host.c): the MIDI clock's and the sequencer's state at that moment, one line, for
 * tests/clock_test.py:
 *   probe MS play P seq S ext E bpm B10 own BPM ticks T pulses N done D pos P1,P2,P3,P4 mch M clk C links L notes ..
 * play: the main loop's PLAY; seq: the sequencer runs; ext: following a MIDI clock; bpm: its tempo x 10; own: the
 * FM-1's tempo (SELECT on HOME); ticks: the sequencer's since PLAY; pulses, done: the clock's pulses since it came and
 * the ticks played for them; pos: each track's step (-1 stopped); mch, clk, links: MIDI CHANNEL, CLOCK, MIDI LEARN's
 * links (the settings); notes: the notes on, by track:note (1..4); then (V2) "fx mf CUT,RES,TYPE,POS dr AMOUNT,TYPE,
 * TONE,MIX ft T1 T2 T3 T4": MASTER's and DRIVE's knobs, the tracks' filter TYPE | POS << 2; "ui page P sel T rec R
 * latch L1,L2,L3,L4 new N steps S": the page (PG_), the selected track (0..3), REC, the tracks' LATCH, SYSTEM 3's LATCH,
 * the selected track's steps with a note (tests/quick_test.py) */
static void host_probe(void)
{
    uint32_t i;
    printf("probe %u play %u seq %u ext %u bpm %u own %d ticks %u pulses %u done %u pos %d,%d,%d,%d mch %u clk %u links %u "
           "notes", fm1_ms, playing, seq_on, ck_ext, ck_bpm10, (int)seq_bpm, seq_ticks, ck.p, ck.done, (int)seq[0].pos,
           (int)seq[1].pos, (int)seq[2].pos, (int)seq[3].pos, set.mch, set.clk, set.nlk);
    for (i = 0; i < nlat; i++) printf(" %u:%u", lat_trk[i] + 1u, lat_note[i]);
    printf(" fx mf %d,%d,%d,%d dr %d,%d,%d,%d ft", (int)mf_cut, (int)mf_res, (int)mf_type, (int)mf_pos, (int)dr_amt,
           (int)dr_type, (int)dr_tone, (int)dr_mix);   /* (V2: MASTER, DRIVE; the tracks' filter TYPE | POS << 2) */
    for (i = 0; i < NT; i++) printf(" %d", (int)(trk[i].ftype | trk[i].fpos << 2));
    {                                           /* (V2: the page, the track, REC, the tracks' LATCH, SYSTEM 3's, the
                                                 * selected track's steps with a note) */
        const seq_pat_t *P = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)];
        uint32_t n = 0;
        for (i = 0; i < SEQ_N; i++) n += P->st[i].note != 0;
        printf(" ui page %u sel %u rec %u latch %d,%d,%d,%d new %u steps %u", page, sel, rec_on, (int)trk[0].latch,
               (int)trk[1].latch, (int)trk[2].latch, (int)trk[3].latch, set.latch, n);
    }
    printf("\n");
}
