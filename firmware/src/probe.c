/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Host mode's "T probe" (firmware/src/host.c): the MIDI clock's and the sequencer's state at that moment, one line, for
 * tests/clock_test.py:
 *   probe MS play P seq S ext E bpm B10 own BPM ticks T pulses N done D pos P1,P2,P3,P4 mch M clk C links L notes ..
 * play: the main loop's PLAY; seq: the sequencer runs; ext: following a MIDI clock; bpm: its tempo x 10; own: the
 * FM-1's tempo (SELECT on HOME); ticks: the sequencer's since PLAY; pulses, done: the clock's pulses since it came and
 * the ticks played for them; pos: each track's step (-1 stopped); mch, clk, links: MIDI CHANNEL, CLOCK, MIDI LEARN's
 * links (the settings); notes: the notes on, by track:note (1..4); then (V2) "fx mf CUT,RES,TYPE,POS dr AMOUNT,TYPE,
 * TONE,MIX ft T1 T2 T3 T4": MASTER's and DRIVE's knobs, the tracks' filter TYPE | POS << 2; "ui page P sel T rec R
 * latch L1,L2,L3,L4 new N steps S wait W said A": the page (PG_), the selected track (0..3), REC, the tracks' LATCH,
 * SYSTEM 3's LATCH (0 FROM SOUND, 1 ALWAYS OFF), the selected track's steps with a note, a save waiting for silence,
 * the header saying so (tests/quick_test.py); "eng E slatch L": the selected track's source, the LATCH saved in the
 * sound PRESETS last loaded on it (-1: none) (tests/latch_test.py) */
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
        const uint8_t *P2;
        uint32_t n = 0;
        for (i = 0; i < SEQ_N; i++) n += P->st[i].note != 0;
        printf(" ui page %u sel %u rec %u latch %d,%d,%d,%d new %u steps %u wait %u said %u", page, sel, rec_on,
               (int)trk[0].latch, (int)trk[1].latch, (int)trk[2].latch, (int)trk[3].latch, set.latch & 1u, n, sv_wait,
               sv_wait && fm1_ms - sv_said < SV_SAY_MS);
        P2 = brw_pos[sel] >= 0 ? cat_item((uint32_t)brw_cat[sel] % NCAT, (uint32_t)brw_pos[sel]) : 0;
        printf(" eng %d slatch %d sname ", (int)trk[sel].eng, P2 ? (int)(P2[12] & 1u) : -1);
        {                                       /* (V2: the factory sound PRESETS loaded, its name, "_" for a space) */
            const char *nm = P2 ? snd_fname(P2) : 0;
            for (; nm && *nm; nm++) putchar(*nm == ' ' ? '_' : *nm);
            if (!nm) putchar('-');
        }
        printf(" vty %d vsh %d", (int)trk[sel].vty, (int)trk[sel].p[ENG_VOICE][1]);   /* (V2: VOICE's TYPE, SHIFT) */
        printf(" dv %d,%d,%d,%d", (int)trk[0].drv, (int)trk[1].drv, (int)trk[2].drv, (int)trk[3].drv);   /* (V2: the
                                                 * tracks' DRIVE sends) */
        printf(" t1mix %d,%d,%d,%d", (int)trk[0].level, (int)trk[0].dly, (int)trk[0].rev, (int)trk[0].grn);
        printf(" tp %d,%d,%d,%d ao %d", (int)trk[0].tap, (int)trk[1].tap, (int)trk[2].tap, (int)trk[3].tap,
               (int)trk[0].arp_oct);                    /* (V2: the TAPE sends; T1's ARP OCTAVES, beside them saved) */
        {                                       /* (V2: GENERATE: on (bit t), MODE; the tracks' patterns' hashes;
                                                 * the notes on by source: sequencer's, generated (track:note)) */
            uint32_t t2, h, j2;
            printf(" gen %u %u,%u,%u,%u oc %d,%d,%d,%d rg %u,%u,%u,%u ph", gen_on, gen_md[0], gen_md[1], gen_md[2],
                   gen_md[3], gen_oc[0], gen_oc[1], gen_oc[2], gen_oc[3], gen_rg[0], gen_rg[1], gen_rg[2], gen_rg[3]);
            for (t2 = 0; t2 < NT; t2++) {
                const uint8_t *b = (const uint8_t *)&seq[t2].pat[0];
                for (h = 2166136261u, j2 = 0; j2 < sizeof seq[t2].pat[0]; j2++) h = (h ^ b[j2]) * 16777619u;
                printf("%s%08x", t2 ? "," : " ", h);
            }
            printf(" gv %u %u %u,%u,%u,%u", gen_took, gen_short, gen[0].left, gen[1].left, gen[2].left,
                   gen[3].left);                /* (V2: GENERATE's voices: taken from another track, chord notes not
                                                 * played within the share; the notes' ticks left) */
            {                                   /* (V2: the tracks' settings as a project saves them (FNV-1a of each
                                                 * pk_track), their ENV; the 64 sounds' and slots A..D's, and the
                                                 * sounds saved) */
                uint8_t q[TRK_PK];
                uint32_t used = 0;
                printf(" keep");
                for (t2 = 0; t2 < NT; t2++) {
                    pk_track(q, t2);
                    for (h = 2166136261u, j2 = 0; j2 < TRK_PK; j2++) h = (h ^ q[j2]) * 16777619u;
                    printf("%s%08x", t2 ? "," : " ", h);
                }
                for (t2 = 0; t2 < NT; t2++)
                    printf("%s%d,%d,%d,%d", t2 ? "/" : " env ", (int)trk[t2].env[0], (int)trk[t2].env[1], (int)trk[t2].env[2],
                           (int)trk[t2].env[3]);
                for (h = 2166136261u, t2 = 0; t2 < SND_N; t2++) {
                    const uint8_t *r = snd_rec(t2);
                    used += r != 0;
                    for (j2 = 0; r && j2 < SND_SIZE; j2++) h = (h ^ r[j2]) * 16777619u;
                }
                for (t2 = 0; t2 < 4u; t2++)
                    for (j2 = 0; j2 < SLOT_ALL; j2++) h = (h ^ slots[t2][j2]) * 16777619u;
                printf(" saved %08x %u", h, used);
            }
            printf(" srcs");
            for (j2 = 0; j2 < nlat; j2++) printf(" %u:%u:%u", lat_src[j2], lat_trk[j2] + 1u, lat_note[j2]);
            printf(" /srcs");
        }
        printf(" vel %u", vel_on);              /* (V2: SYSTEM 3's VELOCITY) */
        printf(" pd1 %d,%d,%d,%d", (int)trk[0].pd_ring, (int)trk[0].pd_noise, (int)trk[0].pd_wav,
               (int)trk[0].pd_dcw);             /* (V2: T1's PHASE extras: RING, NOISE, LINE 2's WAVE, DCW) */
        printf(" pde1 %d,%d,%d,%d", (int)trk[0].pd_de, (int)trk[0].pd_dd, (int)trk[0].pd_pe, (int)trk[0].pd_pd);   /* (V2:
                                                 * T1's PHASE ENV) */
        {                                       /* (V2: T1's GLIDE; its voices' way to go, the most) */
            int32_t g = 0, k2;
            for (k2 = 0; k2 < (int32_t)NV; k2++)
                if (vc[k2].trk == 0 && (vc_gl[k2] < 0 ? -vc_gl[k2] : vc_gl[k2]) > g) g = vc_gl[k2] < 0 ? -vc_gl[k2] : vc_gl[k2];
            printf(" gl1 %d %d", (int)trk[0].glide, (int)g);
        }
        {                                       /* (V2: the gain staging: SPACE's lines, DELAY's line past their soft
                                                 * knee, SPACE's pre-delay clipped (samples, so far); T1's chord mode,
                                                 * its sound's name: tests/level_test.py) */
            char nm[24];
            *trk_sname(0, nm) = 0;
            printf(" lv %u %u %u ch1 %d snd1 %s", lv_rvk, lv_dlk, lv_pre, (int)trk[0].chord, nm[0] ? nm : "-");
        }
        printf(" mix");                         /* (rc9: each track's LEVEL, CUTOFF, RES, FX, DLY, REV, GRN, TAPE,
                                                 * DRIVE, PAN; the mutes, solos; the change log's count) */
        for (i = 0; i < NT; i++)
            printf("%s%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", i ? "/" : " ", (int)trk[i].level, (int)trk[i].cut, (int)trk[i].res,
                   (int)trk[i].fx, (int)trk[i].dly, (int)trk[i].rev, (int)trk[i].grn, (int)trk[i].tap, (int)trk[i].drv,
                   (int)trk[i].pan);
        printf(" setd %u slabs", set_dirty);   /* (rc9: the settings changed and not saved yet; the page's four slabs'
                                                 * labels, "_" for a space, and their warning: HOME's CUT, DRY) */
        for (i = 0; i < 4u; i++) {
            const char *l = ui_sl[i].lab;
            putchar(' ');
            for (; *l; l++) putchar(*l == ' ' ? '_' : *l);
            printf(":%u", ui_sl[i].warn);
        }
        printf(" mute %u solo %u clog %u shed %u hd ", trk_mute, trk_solo, clog_n, shed_n);   /* (rc9: the overload guard's
                                                 * voices let go; the header's message, "_" for a space) */
        for (i = 0; hd_msg[i] && fm1_ms < hd_until; i++) putchar(hd_msg[i] == ' ' ? '_' : hd_msg[i]);
        if (!hd_msg[0] || fm1_ms >= hd_until) putchar('-');
        printf(" p1 %d,%d,%d,%d fxk", (int)trk[0].p[clamp(trk[0].eng, 0, NENG - 1)][0],
               (int)trk[0].p[clamp(trk[0].eng, 0, NENG - 1)][1], (int)trk[0].p[clamp(trk[0].eng, 0, NENG - 1)][2],
               (int)trk[0].p[clamp(trk[0].eng, 0, NENG - 1)][3]);   /* (rc9: T1's source's four; the 16 effects' knobs
                                                 * as a project saves them: TAPE, GRAIN, DELAY, SPACE) */
        for (i = 0; i < 16u; i++) printf("%s%d", i ? "," : " ", (int)*PK_FX[i]);
        printf(" chd");                        /* (rc11: each track's chord mode / CHORD TYPE / VOICING: tests/chord_test.py) */
        for (i = 0; i < NT; i++) printf(" %d/%d/%d", (int)trk[i].chord, (int)trk[i].ch_type, (int)trk[i].ch_voice);
        printf(" voices");                      /* (each voice: its stage (0 off, 1 attack, 2 sustain, 3 release, 4
                                                 * fade), track, note, in the notes-on list or not) */
        for (i = 0; i < NV; i++) printf(" %u/%u/%u/%d", (unsigned)vc[i].stage, (unsigned)vc[i].trk + 1u,
                                        (unsigned)vc[i].note, voice_latched(i));
    }
    printf("\n");
}
