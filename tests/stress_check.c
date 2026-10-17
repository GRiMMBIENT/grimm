/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The stress test's checks (tests/stress.sh): grimm.c includes this file only when GRIMM_STRESS is set (host mode, with
 * address and undefined-behaviour checking), and calls stress_poll() every main loop pass (one millisecond). The script
 * (tests/stress_gen.py) says when everything has been let go (STRESS_CHECKS, a file: "T released", "T silent"), and
 * when to come back from the autosaved project as at power-on ("T boot": store_boot()), and when to stop whatever
 * plays and records ("T stop": REC and PLAY tapped as need be: a SONG may have stopped by itself), when to put the
 * project and the slots in flash as version 5 wrote them and come back from there ("T upgrade": nothing they hold may
 * be lost), when to cut a save of the slots half way ("T cut": sector 4 half written; then only its slots B, C, D
 * empty and said lost, A and the working project as they were), when SYSTEM's settings must be saved and come back
 * from as at power-on ("T e_settings"), the screen dark ("T dark": SCREEN OFF), woken by a key that played nothing
 * ("T woken"), MIDI LEARN's link moved the track's ATTACK to 127 ("T learned"), the links of steps 20..24 to HOME's
 * T3 LEVEL and TRACK's CUTOFF (T4) still moving them ("T h_links"), PRESETS browsed through the factory
 * sounds into the user's ("T factory": the lists as they should be, the track's sound the one browsed to). It fails (a line "STRESS FAIL", exit 3) when:
 *   - a key's voices still sound 60 ms after it was let go (its track's LATCH OFF, for 60 ms too), or a step's after
 *     the sequencer stopped; a key still in its track's arp 60 ms after it was let go (LATCH OFF); an arp's note
 *     60 ms after its ARP went off or its list emptied; a note of a track muted (or not soloed) for 60 ms; an FX
 *     trick still on 60 ms after its key was let go;
 *   - the sequencer's clock off while it plays (a trick held or not): its ticks over each half second not as the tempo
 *     says (+-3); PLAY off while a trick is held, not by PLAY (nor a SONG's end);
 *   - at "released": a note is still on, or a voice still in its attack or sustain, or one still on out there over
 *     USB-MIDI (the keys' notes, SEQ MIDI OUT's: what midi_send sent, on each channel);
 *   - at "silent": a voice still sounds at all (the releases are over), or an FX trick is still on (perform_idle);
 *   - the sound runs away: two seconds in a row with more than half the samples clipped;
 *   - a press the app takes that no hand made: a ghost of the FM-1's matrix (host.c HOST_MATRIX simulates it) past
 *     the input layer's test (a key let go while a rectangle of others still makes it read closed stays down till
 *     the rectangle breaks: on the FM-1 too, that can't be told from a key held);
 *   - audio blocks over their budget three times within a second (this computer's clock: a hang or a loop, not the
 *     FM-1's load; one alone is this computer pausing the test);
 *   - at "boot": nothing was autosaved to come back from, or TRACK's CUTOFF, RESONANCE, FX not back as they were;
 *   - at "upgrade": a track of a save from before TRACK not with its filter open, RESONANCE 0, FX 100 %, or one
 *     from before the FM patches not with its PATCH as built;
 *   - at "v_sounds" (the sounds as saved before step 24: 62 in slots 1..62, three past 64, put in sector 0, come back
 *     from): not the 62 where they were, the first two past 64 moved into 63 and 64, the third said lost;
 *   - at "m_fm": an FM track's own patch not back as it was from a sound saved and loaded, a slot saved and
 *     recalled; at "boot", from the autosave.
 * "x_editor": the editor's mailbox (editor.c) through its frames: INFO; slot A and the settings read back as they are;
 * a factory sound written into user sound 64, slot A into slot D, the settings as they are, each read back; then a
 * wrong CRC, a wrong format, a factory sound, while not silent, settings out of range, DATA without BEGIN, a PUT half
 * sent: each refused, nothing changed. "y_garbage": from then on every 47 ms a frame of garbage to it (random bytes,
 * cut short, a PUT begun and never ended, a COMMIT of the wrong bytes; now and then through USB's way in, host_sx):
 * the sounds, the slots and the settings must stay as they were.
 * "g_source" is no check: the page put on EDIT's SOURCE (the script can't tell how many EDIT taps it is away).
 * The last "silent" check ends with "STRESS OK". */
#include <stdio.h>

#define ST_MAXCHK 256u
static uint32_t st_chk_t[ST_MAXCHK], st_chk_kind[ST_MAXCHK], st_nchk, st_ichk;   /* (kind: 'r', 's', 'b') */
static uint32_t st_up_ms[27], st_latch[NT], st_latch_ms[NT], st_stop_ms, st_was_playing, st_prev_keys;
static uint32_t st_wav_i, st_clip, st_win, st_hot, st_arp[NT], st_arp_ms[NT];   /* (ARP on and its list: when not) */
static uint32_t st_quiet, st_quiet_ms[NT], st_stop, st_stop_t;   /* (the tracks silent, since when; "stop": at work) */
static uint32_t st_cw_t, st_cw_ticks, st_cw_bpm, st_cw_ok;   /* (the clock's half-second window: from, ticks, tempo) */

static void stress_fail(const char *what, uint32_t a);
static uint32_t st_rx[16][4];                   /* USB-MIDI out: the notes on out there (a channel's, bit: the note) */
static void stress_midi_out(uint32_t st, uint32_t d1)   /* (host.c's midi_send) */
{
    uint32_t c = st & 15u, n = d1 & 127u;
    if ((st & 0xF0u) == 0x90u) st_rx[c][n >> 5] |= 1u << (n & 31u);
    else if ((st & 0xF0u) == 0x80u) st_rx[c][n >> 5] &= ~(1u << (n & 31u));
}
static void stress_system(uint32_t kind)        /* "settings", "dark", "woken", "learned" */
{
    static uint8_t was[sizeof set];
    switch (kind) {
    case 'e':                                   /* settings: saved as they are; as at power-on, the same */
        if (set_dirty || memcmp(&set, host_store + 4, sizeof set)) stress_fail("settings: not saved as they are", set_dirty);
        if (set.scr != 1u || set.mch != 1u || set.tune != 7 || !set.out || set.nlk != 1u)
            stress_fail("settings: not as SYSTEM's knobs set them (MIDI CHANNEL)", set.mch);
        memcpy(was, &set, sizeof set);
        memset(&set, 0, sizeof set);
        sys_load();
        if (memcmp(was, &set, sizeof set) || tune_f != 16) stress_fail("settings: not back as saved (TUNE's factor)", (uint32_t)tune_f);
        break;
    case 'd':
        if (!scr_dark || !host_dark) stress_fail("dark: the screen still lit after SCREEN OFF's minute", scr_dark);
        break;
    case 'w':
        if (scr_dark || host_dark) stress_fail("woken: the screen still dark after a key", scr_dark);
        if (nlat) stress_fail("woken: the key that woke the screen played (notes)", nlat);
        break;
    default: {                                  /* learned: CC 21 linked to the selected track's ATTACK, moved to 127 */
        const uint8_t *L = set.lk[set.nlk ? set.nlk - 1u : 0u];
        if (!set.nlk || L[0] != 21u || L[1] != PG_ENV || L[2] != 0u || L[3] != sel || trk[sel].env[0] != 127)
            stress_fail("learned: CC 21 did not move the track's ATTACK (ATTACK)", (uint32_t)trk[sel].env[0]);
        break;
    }
    }
}

/* the version 5 forms (steps of 3 B, no chord steps, no locks) of the working project, of a slot */
#define ST_W5 WORK_LEN(52u, 20u, SEQ_PK5, 0u, 0u)
#define ST_S5 SLOT_LEN(52u, SEQ_PK5, 0u)
static uint8_t st_w[ST_W5], st_s[4u * ST_S5], st_x[ST_W5 > 4u * ST_S5 ? ST_W5 : 4u * ST_S5];
static const uint8_t *st_seq5(uint8_t *o, const uint8_t *n)   /* a sequence (260 B) as 196 B: o; the next of n */
{
    uint32_t k;
    for (k = 0; k < 4u; k++) o[k] = n[k];
    for (k = 0; k < SEQ_N; k++) o[4 + k * 3u] = n[4 + k * 4u], o[5 + k * 3u] = n[5 + k * 4u], o[6 + k * 3u] = n[6 + k * 4u];
    return n + SEQ_PK;
}
static void st_trk5(uint8_t *q)                 /* (a track as before TRACK: its bytes 29..31 zero) */
{
    uint32_t t;
    for (t = 0; t < NT; t++) q[t * TRK_PK + 29u] = q[t * TRK_PK + 30u] = q[t * TRK_PK + 31u] = 0;
}
static void st_work5(uint8_t *o)                /* the working project now, as version 5 (before TRACK too) */
{
    static uint8_t n[WORK_PK];
    const uint8_t *q = n;
    uint32_t k, t;
    pk_work(n);
    st_trk5(n);
    for (k = 0; k < NT * TRK_PK + 20u; k++) *o++ = *q++;
    for (t = 0; t < NT; t++, o += SEQ_PK5) q = st_seq5(o, q);
    q += NT * LOCK_PK;
    for (k = 0; k < SONG_PK; k++) *o++ = *q++;
}
static void st_slots5(uint8_t *o)               /* the slots now, as version 5 */
{
    uint32_t s, k, t;
    for (s = 0; s < 4u; s++) {
        const uint8_t *q = slots[s];
        for (k = 0; k < 4u + NT * TRK_PK; k++) *o++ = *q++;
        st_trk5(o - NT * TRK_PK);
        for (t = 0; t < NT; t++, o += SEQ_PK5) q = st_seq5(o, q);
    }
}
static void st_put(uint32_t sect, uint32_t type, const uint8_t *pay, uint32_t len)   /* a version 5 object, sector sect */
{
    static uint8_t im[4096];
    uint32_t k;
    for (k = 0; k < 4096u; k++) im[k] = k < OBJ_HDR + len ? (k >= OBJ_HDR ? pay[k - OBJ_HDR] : 0u) : 0xFFu;
    st_wr32(im, OBJ_MAGIC), st_wr32(im + 4, 7u), im[8] = (uint8_t)type, im[9] = 5u;
    im[10] = (uint8_t)len, im[11] = (uint8_t)(len >> 8);
    st_wr32(im + OBJ_HDR + len, st_crc(im, OBJ_HDR + len));
    save_flash_erase(sect);
    save_flash_write(sect, 0, im, 4096u);
}
static void stress_upgrade(void)                /* "upgrade": version 5's project and slots in flash; come back */
{
    uint32_t k;
    st_work5(st_w), st_slots5(st_s);
    for (k = 1; k < 6u; k++) save_flash_erase(k);
    st_put(1, OBJ_WORK, st_w, ST_W5);
    st_put(3, OBJ_SLOTS, st_s, 4u * ST_S5);
    store_boot();
    for (k = 0; k < NT; k++) {
        uint8_t a[FM_PK], b[FM_PK];
        uint32_t i;
        if (trk[k].cut != 127 || trk[k].res || trk[k].fx != 100)
            stress_fail("upgrade: a track's filter not open, FX not 100 % (track)", k);
        pk_fm(a, k), fm_builtin(k, trk[k].p[ENG_FM][0]), pk_fm(b, k);
        for (i = 0; i < FM_PK; i++)
            if (a[i] != b[i]) stress_fail("upgrade: a track's FM patch not its PATCH as built (track)", k);
    }
    st_work5(st_x);
    for (k = 0; k < ST_W5; k++)
        if (st_x[k] != st_w[k]) stress_fail("upgrade: the working project not as version 5 had it (byte)", k);
    st_slots5(st_x);
    for (k = 0; k < 4u * ST_S5; k++)
        if (st_x[k] != st_s[k]) stress_fail("upgrade: the slots not as version 5 had them (byte)", k);
    if (slot_dirty != 15u || slot_up != 3u) stress_fail("upgrade: the slots not to be written as the stream", slot_dirty);
}
static void stress_cut(void)                    /* "cut": a save of the slots cut in sector 4 (slots B, C, D in it) */
{
    static uint8_t a[SLOT_PK], w[2][4096];
    uint32_t k, i;
    for (i = 0; i < SLS_N; i++)
        if (!sls_ok(i)) stress_fail("cut: the slots' sectors not written before (sector)", i);
    if (!slot_used(0)) stress_fail("cut: slot A empty (the test needs it saved)", 0);
    for (k = 0; k < SLOT_PK; k++) a[k] = slots[0][k];
    for (i = 0; i < 2u; i++)
        for (k = 0; k < 4096u; k++) w[i][k] = save_flash(1u + i)[k];
    for (k = 0; k < 4096u; k++) st_img[k] = save_flash(4)[k];   /* (as it was: its first 1000 bytes again, the rest
                                                 * erased: the power gone half way) */
    save_flash_erase(4);
    save_flash_write(4, 0, st_img, 1000u);
    store_boot();
    if (slot_lost != 14u) stress_fail("cut: not B, C, D said lost (slot_lost)", slot_lost);
    for (k = 1; k < 4u; k++)
        if (slot_used(k)) stress_fail("cut: a slot of the cut sector not empty (slot)", k);
    for (k = 0; k < SLOT_PK; k++)
        if (slots[0][k] != a[k]) stress_fail("cut: slot A not as it was (byte)", k);
    for (i = 0; i < 2u; i++)
        for (k = 0; k < 4096u; k++)
            if (save_flash(1u + i)[k] != w[i][k]) stress_fail("cut: the working project's sectors touched (sector)", 1u + i);
}

static void stress_factory(void)                /* "factory" */
{
    uint32_t c, k, n = 0, i, u = 0, e;
    const uint8_t *r;
    uint8_t lf[4];
    for (i = 0; i < SND_NFACT; i++) {           /* (each a sound a track can take, named to fit) */
        const uint8_t *q = SND_FACTORY[i];
        if (q[1] >= NCAT || q[3] >= NENG || q[3] == ENG_SAMPLE || !SND_FNAME[i][0] || strlen(SND_FNAME[i]) > 16u)
            stress_fail("factory: a factory sound not as it should be (number)", i);
    }
    for (c = 0; c < NCAT; c++) {                /* (a CATEGORY's list: its factory sounds first, in order) */
        for (k = 0, i = 0; i < SND_NFACT; i++)
            if (SND_FACTORY[i][1] == c && cat_item(c, k++) != SND_FACTORY[i])
                stress_fail("factory: a factory sound not in its place in PRESETS' list (number)", i);
        n += cat_len(c);
    }
    for (i = 0; i < SND_N; i++) u += snd_rec(i) != 0;
    if (SND_NFACT != 40u || !u || n != SND_NFACT + u)
        stress_fail("factory: PRESETS' lists not the 40 factory sounds and the user's (sounds listed)", n);
    if (!(r = cat_item((uint32_t)brw_cat[sel] % NCAT, (uint32_t)brw_pos[sel])))
        stress_fail("factory: PRESETS browsed to nothing (place)", (uint32_t)brw_pos[sel]);
    e = r[3];
    lfo_pk(lf, sel);
    for (k = 0; k < 4u; k++)
        if ((uint32_t)trk[sel].p[e][k] != r[4 + k] || (uint32_t)trk[sel].env[k] != r[8 + k] || lf[k] != r[14 + k])
            stress_fail("factory: the track's sound not the one browsed to (knob)", k);
    if ((uint32_t)trk[sel].eng != e || (uint32_t)trk[sel].latch != r[12] || ch_pack(sel) != r[13])
        stress_fail("factory: the track's source, LATCH or CHORD not the browsed sound's (source)", (uint32_t)trk[sel].eng);
}

static void stress_vsounds(void)                /* "v_sounds" */
{
    static uint8_t im[4096];
    uint32_t i, k;
    for (k = 0; k < 4096u; k++) im[k] = 0xFFu;
    for (i = 0; i < 128u; i++) {                /* (62 in 0..61; 64, 100, 127) */
        uint8_t *r = im + i * 32u;
        if (i >= 62u && i != 64u && i != 100u && i != 127u) continue;
        for (k = 0; k < 28u; k++) r[k] = 0;
        r[0] = SND_MAGIC1, r[1] = (uint8_t)(i % NCAT), r[2] = (uint8_t)(i % 99u + 1u), r[3] = (uint8_t)(i % NENG);
        for (k = 4; k < 18u; k++) r[k] = (uint8_t)((i * 7u + k) & 63u);
        r[12] = (uint8_t)(i & 1u), r[13] = (uint8_t)(i % NCHORD | (i % NVOICING) << 4);   /* (what a sound can hold) */
        r[16] = (uint8_t)(i % NLFODEST), r[17] = (uint8_t)(i % NLFOSHAPE);
        st_wr32(r + 28, st_crc(r, 28));
    }
    save_flash_erase(0);
    save_flash_write(0, 0, im, 4096u);
    snd_moved = snd_lost = 0;
    store_boot();
    if (snd_moved != 2u || snd_lost != 1u) stress_fail("v_sounds: not two moved, one lost (moved)", snd_moved);
    for (i = 0; i < SND_N; i++) {
        uint32_t from = i < 62u ? i : i == 62u ? 64u : 100u;
        const uint8_t *r = snd_rec(i);
        if (!r) stress_fail("v_sounds: a sound gone (slot)", i);
        for (k = 1; k < 18u; k++)
            if (r[k] != im[from * 32u + k]) stress_fail("v_sounds: a sound not as it was (slot)", i);
        if (r[18] & 0x80u) stress_fail("v_sounds: an FM patch where none was saved (slot)", i);
    }
}
static void stress_mfm(void)                    /* "m_fm": a track's FM patch through a sound, a slot */
{
    uint8_t a[FM_PK], b[FM_PK];
    uint32_t t = sel, i, k;
    trk[t].fm_alg = (int32_t)(fm1_ms % 32u), trk[t].fm_fb = (int32_t)(fm1_ms % 8u);   /* (an edit of its own) */
    for (i = 0; i < 6u; i++)
        for (k = 0; k < 5u; k++) trk[t].fmo[i][k] = (int32_t)((fm1_ms / 7u + i * 13u + k * 5u) % (k ? 100u : 32u));
    pk_fm(a, t);
    trk[t].eng = ENG_FM;
    if (snd_save(SND_N - 1u, t, CAT_OTHER)) stress_fail("m_fm: the sound not saved", 0);
    slot_save(3);
    fm_builtin(t, 1), snd_load(SND_N - 1u, t), pk_fm(b, t);
    for (k = 0; k < FM_PK; k++)
        if (a[k] != b[k]) stress_fail("m_fm: the FM patch not back from the sound (byte)", k);
    fm_builtin(t, 2), slot_recall(3), pk_fm(b, t);
    for (k = 0; k < FM_PK; k++)
        if (a[k] != b[k]) stress_fail("m_fm: the FM patch not back from the slot (byte)", k);
}

static void stress_hlinks(void)                 /* "h_links": settings as steps 20..24 saved them (no ver), two links */
{
    static uint8_t was[sizeof set];
    memcpy(was, &set, sizeof set);
    set.nlk = 2;
    set.lk[0][0] = 21, set.lk[0][1] = PG_HOME, set.lk[0][2] = 2, set.lk[0][3] = 0, set.lk[0][4] = 0;   /* (T3 LEVEL) */
    set.lk[1][0] = 22, set.lk[1][1] = PG_TRACK, set.lk[1][2] = 1, set.lk[1][3] = 3, set.lk[1][4] = 0;  /* (T4 CUTOFF) */
    settings_save(&set, sizeof set - 1u);
    host_store[4 + sizeof set - 1u] = 0;        /* (no ver: as they were) */
    sys_load();
    if (set.lk[0][1] != PG_MIX1 || set.lk[0][2] != 0 || set.lk[0][3] != 2 || set.lk[1][1] != PG_HOME || set.lk[1][3] != 3)
        stress_fail("h_links: the old links not moved onto MIXER 1's LEVEL, HOME (page)", set.lk[0][1]);
    midi_cc(21, 127), midi_cc(22, 0);
    if (trk[2].level != 127 || trk[3].cut != 0) stress_fail("h_links: an old link moves nothing now (T3 LEVEL)", (uint32_t)trk[2].level);
    memcpy(&set, was, sizeof set);              /* (the settings as they were) */
    set_dirty = 1, set_t = fm1_ms;
}

/* ---- the editor's mailbox */
static uint8_t st_sxr[700], st_f[640];
static uint32_t st_sxn, st_fn, st_garbage, st_grng = 0x1234567u;
static void stress_sysex_out(const uint8_t *p, uint32_t n)   /* (host.c's sysex_send: its last reply) */
{
    st_sxn = n < sizeof st_sxr ? n : (uint32_t)sizeof st_sxr;
    memcpy(st_sxr, p, st_sxn);
}
static void st_fb(uint32_t b) { if (st_fn < sizeof st_f) st_f[st_fn++] = (uint8_t)(b & 0x7Fu); }
static void st_fhead(uint32_t cmd) { st_fn = 0, st_fb(0x7D), st_fb(0x47), st_fb(0x52), st_fb(cmd); }
static void st_f35(uint32_t v) { uint32_t k; for (k = 0; k < 5u; k++) st_fb(v >> (7u * k)); }
static void st_fpack(const uint8_t *d, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7u) {
        uint32_t g = n - i < 7u ? n - i : 7u, top = 0;
        for (j = 0; j < g; j++) top |= (uint32_t)(d[i + j] >> 7) << j;
        st_fb(top);
        for (j = 0; j < g; j++) st_fb(d[i + j]);
    }
}
static void st_ed(int silent) { st_sxn = 0; if (!ed_frame(st_f, st_fn, silent)) st_sxn = 0; }
static uint32_t st_get(uint32_t o, uint8_t *out, uint32_t len)   /* object o by GET, 256 B a time: 0 ok */
{
    uint32_t off, c;
    for (off = 0; off < len; off += c) {
        c = len - off < 256u ? len - off : 256u;
        st_fhead(ED_GET), st_fb(o), st_f35(off), st_fb(c), st_fb(c >> 7);
        st_ed(1);
        if (st_sxn < 15u || st_sxr[6] || unpack7(st_sxr + 14, st_sxn - 15u, out + off, c) != c) return 1;
    }
    return 0;
}
static uint32_t st_edput(uint32_t o, const uint8_t *b, uint32_t len, uint32_t crc, uint32_t fmt, int silent, int commit)
{                                               /* BEGIN, DATA, COMMIT: the first rc not 0, or COMMIT's */
    uint32_t off, c;
    st_fhead(ED_PUT), st_fb(0), st_fb(o), st_fb(fmt), st_f35(len), st_f35(crc);
    st_ed(silent);
    if (st_sxn < 9u || st_sxr[7]) return st_sxn < 9u ? 99u : st_sxr[7];
    for (off = 0; off < len; off += c) {
        c = len - off < 256u ? len - off : 256u;
        st_fhead(ED_PUT), st_fb(1), st_fb(o), st_f35(off), st_fpack(b + off, c);
        st_ed(silent);
        if (st_sxn < 9u || st_sxr[7]) return st_sxn < 9u ? 99u : st_sxr[7];
    }
    if (!commit) return 0;
    st_fhead(ED_PUT), st_fb(2), st_fb(o);
    st_ed(silent);
    return st_sxn < 9u ? 99u : st_sxr[7];
}
static uint8_t st_snap_s[4096], st_snap_sl[4][SLOT_ALL], st_snap_set[sizeof set];
static void st_snap(void)
{
    memcpy(st_snap_s, save_flash(0), 4096u), memcpy(st_snap_sl, slots, sizeof st_snap_sl), memcpy(st_snap_set, &set, sizeof set);
}
static int st_changed(void)
{
    return memcmp(st_snap_s, save_flash(0), 4096u) || memcmp(st_snap_sl, slots, sizeof st_snap_sl) ||
           memcmp(st_snap_set, &set, sizeof set);
}
static uint32_t st_grand(uint32_t n);
static uint32_t st_track(uint32_t op, uint32_t t, const uint8_t *s, int silent)   /* TRACK: its rc (99: no reply) */
{
    st_fhead(ED_TRACK), st_fb(op), st_fb(t);
    if (s) st_fpack(s, SND_SIZE);
    st_ed(silent);
    return st_sxn < 10u || st_sxr[5] != op || st_sxr[6] != t || st_sxr[7] != sel ? 99u : st_sxr[8];
}
static void stress_live(void)                   /* x_editor: TRACK read, set while playing, refused, selected */
{
    uint8_t r[SND_SIZE], q[SND_SIZE], m[SND_SIZE];
    uint32_t t = st_grand(NT), s0 = sel, rc;
    if ((rc = st_track(0, t, 0, 0)) != 0 || st_sxn < 84u || unpack7(st_sxr + 9, 74, r, SND_SIZE) != SND_SIZE)
        stress_fail("x_editor: TRACK READ (rc)", rc);
    snd_pack(q, t, 0, 1);
    if (memcmp(r, q, SND_SIZE) || !snd_ok(r)) stress_fail("x_editor: TRACK READ not the track's sound (track)", t);
    memcpy(m, r, SND_SIZE), m[8] = (uint8_t)((m[8] + 5u) & 127u), m[15] = (uint8_t)((m[15] + 7u) % 101u);
    st_wr32(m + 60, st_crc(m, 60));
    if ((rc = st_track(1, t, m, 0)) != 0 || trk[t].env[0] != m[8] || trk[t].lfo_depth != m[15])
        stress_fail("x_editor: TRACK SET not live while sounding (rc)", rc);
    m[60] ^= 1u;
    if (st_track(1, t, m, 0) != ER_BAD || trk[t].env[0] != m[8]) stress_fail("x_editor: a broken TRACK SET taken", t);
    if ((rc = st_track(1, t, r, 1)) != 0) stress_fail("x_editor: TRACK SET back (rc)", rc);
    snd_pack(q, t, 0, 1);
    if (memcmp(r, q, SND_SIZE)) stress_fail("x_editor: the track's sound not as it was (track)", t);
    if (st_track(2, (s0 + 1u) % NT, 0, 0) || sel != (s0 + 1u) % NT) stress_fail("x_editor: TRACK SELECT", sel);
    if (st_track(2, s0, 0, 1) || sel != s0 || st_track(0, NT, 0, 1) != ER_ARGS) stress_fail("x_editor: TRACK's arguments", sel);
}
static void stress_smp_info(void)               /* x_editor: the samples' INFO with every name 8 letters (26b2: cut) */
{
    char was[SMP_SLOTS][9];
    uint32_t k;
    for (k = 0; k < SMP_SLOTS; k++) memcpy(was[k], smp[k].name, 9), memcpy(smp[k].name, "ABCDEFGH", 9);
    st_fhead(15), st_sxn = 0, smp_frame(st_f, st_fn, 1);
    for (k = 0; k < SMP_SLOTS; k++) memcpy(smp[k].name, was[k], 9);
    if (st_sxn != SX_INFO + 6u || st_sxr[st_sxn - 1u] != 0xF7u || memcmp(st_sxr + st_sxn - 20u, "ABCDEFGH", 8))
        stress_fail("x_editor: the samples' INFO cut short (bytes)", st_sxn);
}
static void stress_names(void)                  /* x_editor: a sound's name (bytes 50..59): shown, kept, not made up */
{
    static const char NM[] = "AB\001CD";       /* (a byte the font has not: a space) */
    uint8_t r[SND_SIZE], was[SND_SIZE];
    char s[24];
    uint32_t t = st_grand(NT), k, had = snd_rec(61) != 0;
    if (had) memcpy(was, snd_rec(61), SND_SIZE);
    snd_pack(r, t, 2, 7);
    snd_rec_name(r, s);
    if (strcmp(s, "BELL 07")) stress_fail("x_editor: an unnamed sound not CAT NN", 0);
    memcpy(r + 50, NM, sizeof NM), st_wr32(r + 60, st_crc(r, 60));
    snd_rec_name(r, s);
    if (strcmp(s, "AB CD")) stress_fail("x_editor: a named sound's name not shown (or not as the font has it)", 0);
    if (snd_put(61, r) || snd_save(61, t, 3) || !snd_rec(61) || memcmp(snd_rec(61) + 50, NM, sizeof NM) || snd_rec(61)[1] != 3u)
        stress_fail("x_editor: a sound saved over a named one lost its name", 0);
    if (snd_clear(61) || snd_save(61, t, 3) || !snd_rec(61)) stress_fail("x_editor: a sound not saved into an empty slot", 0);
    for (k = 50; k < 60u; k++)
        if (snd_rec(61)[k]) stress_fail("x_editor: a sound saved into an empty slot has a name", k);
    if (had ? snd_put(61, was) : snd_clear(61)) stress_fail("x_editor: sound 62 not put back", 0);
    snd_pack(r, t, 0, 1);                       /* TRACK SET with its name: HOME's, and TRACK READ's */
    st_fhead(ED_TRACK), st_fb(1), st_fb(t), st_fpack(r, SND_SIZE);
    for (k = 0; k < 11u; k++) st_fb((uint8_t)"FROZEN HALL"[k]);
    st_ed(0);
    if (st_sxn < 10u || st_sxr[8] || (trk_sname(t, s), strcmp(s, "FROZEN HALL"))) stress_fail("x_editor: TRACK SET's name not HOME's", t);
    if (st_track(0, t, 0, 0) || st_sxn != 84u + 11u || memcmp(st_sxr + 83, "FROZEN HALL", 11))
        stress_fail("x_editor: TRACK READ without the track's name (bytes)", st_sxn);
    if (st_track(1, t, r, 0) || *trk_sname(t, s)) stress_fail("x_editor: TRACK SET without a name kept the last one", t);
}
static void stress_fsamples(void)               /* x_editor: power-on's factory samples: an erased slot gets its own, a slot
                                                 * that holds anything (a byte) is untouched; RESTORE: all four */
{
    static uint8_t keep[SAMPLE_AREA_LEN];
    uint32_t k, m;
    memcpy(keep, fake_flash, sizeof keep);
    memset(fake_flash, 0xFF, sizeof fake_flash);   /* (a new FM-1: nothing in the sample area) */
    if (smp_erased() != 15u) stress_fail("x_editor: an erased sample area not seen as erased (mask)", smp_erased());
    fake_flash[2u * SMP_SLOT_SIZE + 70000u] = 0x5A;   /* (slot 3 holds a byte, deep in it) */
    if ((m = smp_erased()) != 11u) stress_fail("x_editor: a slot holding a byte seen as erased (mask)", m);
    for (k = 0; k < SMP_SLOTS; k++)
        if (m >> k & 1u && smp_factory(k)) stress_fail("x_editor: a factory sample not written (slot)", k);
    for (k = 0; k < SMP_SLOTS; k++) {
        const uint8_t *f = fake_flash + k * SMP_SLOT_SIZE;
        if (k == 2u ? f[70000] != 0x5A || f[0] != 0xFF : memcmp(f, SMP_FACT_HDR[k], SMP_HDR_LEN) ||
                                                        memcmp(f + SMP_DATA_OFF, SMP_FACT_DATA[k], SMP_FACT_LEN[k]))
            stress_fail("x_editor: a sample slot not as the boot should leave it (slot)", k);
    }
    if (smp_erased()) stress_fail("x_editor: the next boot would write again (mask)", smp_erased());
    st_fhead(16), st_sxn = 0, smp_frame(st_f, st_fn, 0);
    if (st_sxn < 7u || st_sxr[5] != 6u || fake_flash[2u * SMP_SLOT_SIZE + 70000u] != 0x5A) stress_fail("x_editor: RESTORE while sounding", 0);
    st_fhead(16), st_sxn = 0, smp_frame(st_f, st_fn, 1);
    if (st_sxn < 7u || st_sxr[5] || memcmp(fake_flash + 2u * SMP_SLOT_SIZE, SMP_FACT_HDR[2], SMP_HDR_LEN))
        stress_fail("x_editor: RESTORE did not write all four", st_sxn);
    for (k = 0; k < SMP_SLOTS; k++)
        while (smp[k].state == SS_SCAN) smp_scan(k);
    if (!smp[0].ready || strcmp(smp[0].name, "URCHIN") || !smp[3].ready || strcmp(smp[3].name, "M-SOFT"))
        stress_fail("x_editor: the factory samples do not check out", 0);
    memcpy(fake_flash, keep, sizeof keep);      /* (back as it was, checked and indexed again) */
    for (k = 0; k < SMP_SLOTS; k++) {
        smp_look(k);
        while (smp[k].state == SS_SCAN) smp_scan(k);
    }
}
static void stress_editor(void)                 /* "x_editor" */
{
    static uint8_t a[SLOT_ALL], b[SLOT_ALL];
    uint32_t i, rc = 0;
    st_fhead(ED_INFO), st_ed(1);
    if (st_sxn < 20u || st_sxr[5] != ED_PROTO || st_sxr[6] != ED_FMT) stress_fail("x_editor: no INFO (length)", st_sxn);
    if (st_get(EO_SLOT, a, SLOT_ALL) || memcmp(a, slots[0], SLOT_ALL)) stress_fail("x_editor: slot A not read back", 0);
    if (st_get(EO_SET, a, sizeof set) || memcmp(a, &set, sizeof set)) stress_fail("x_editor: the settings not read back", 0);
    if (st_get(EO_FACT, a, SND_SIZE) || (rc = st_edput(EO_SND + 63u, a, SND_SIZE, st_crc(a, SND_SIZE), ED_FMT, 1, 1)) != 0)
        stress_fail("x_editor: a factory sound not written into sound 64 (rc)", rc);
    if (st_get(EO_SND + 63u, b, SND_SIZE) || memcmp(a, b, SND_SIZE) || !snd_rec(63) || snd_rec(63)[3] != SND_FACTORY[0][3])
        stress_fail("x_editor: sound 64 not the factory sound", 0);
    memcpy(a, slots[0], SLOT_ALL);
    if ((rc = st_edput(EO_SLOT + 3u, a, SLOT_ALL, st_crc(a, SLOT_ALL), ED_FMT, 1, 1)) != 0 || memcmp(slots[3], a, SLOT_ALL))
        stress_fail("x_editor: slot A not written into slot D (rc)", rc);
    for (i = 0; i < SLS_N; i++)
        if (!sls_ok(i)) stress_fail("x_editor: a slots' sector not good after the PUT (sector)", i);
    memcpy(a, &set, sizeof set);
    if ((rc = st_edput(EO_SET, a, sizeof set, st_crc(a, sizeof set), ED_FMT, 1, 1)) != 0 || memcmp(&set, a, sizeof set))
        stress_fail("x_editor: the settings not written back as they are (rc)", rc);
    st_snap();                                  /* refused, nothing changed */
    memcpy(a, slots[1], SLOT_ALL);
    if (st_edput(EO_SLOT, a, SLOT_ALL, st_crc(a, SLOT_ALL) ^ 1u, ED_FMT, 1, 1) != ER_BAD) stress_fail("x_editor: a wrong CRC taken", 0);
    if (st_edput(EO_SLOT, a, SLOT_ALL, st_crc(a, SLOT_ALL), ED_FMT + 1u, 1, 1) != ER_ARGS) stress_fail("x_editor: a wrong format taken", 0);
    if (st_edput(EO_FACT, a, SND_SIZE, st_crc(a, SND_SIZE), ED_FMT, 1, 1) != ER_ARGS) stress_fail("x_editor: a factory sound written", 0);
    if (st_edput(EO_SLOT, a, SLOT_ALL, st_crc(a, SLOT_ALL), ED_FMT, 0, 1) != ER_STOP) stress_fail("x_editor: written while not silent", 0);
    memcpy(b, &set, sizeof set), b[sizeof set - 1u] = 9u;   /* (ver 9) */
    if (st_edput(EO_SET, b, sizeof set, st_crc(b, sizeof set), ED_FMT, 1, 1) != ER_BAD) stress_fail("x_editor: bad settings taken", 0);
    memset(b, 0x55, SND_SIZE);                  /* (not a sound) */
    if (st_edput(EO_SND, b, SND_SIZE, st_crc(b, SND_SIZE), ED_FMT, 1, 1) != ER_BAD) stress_fail("x_editor: a broken sound taken", 0);
    st_fhead(ED_PUT), st_fb(1), st_fb(EO_SLOT), st_f35(0), st_fpack(a, 64), st_ed(1);
    if (st_sxn < 9u || st_sxr[7] != ER_NOBEGIN) stress_fail("x_editor: DATA without BEGIN taken", 0);
    if (st_edput(EO_SLOT + 1u, a, 300, st_crc(a, 300), ED_FMT, 1, 0) != ER_ARGS) stress_fail("x_editor: a short length taken", 0);
    if (st_edput(EO_SLOT + 1u, a, SLOT_ALL, st_crc(a, SLOT_ALL), ED_FMT, 1, 0)) stress_fail("x_editor: a half-sent PUT's start", 0);
    st_fhead(ED_PUT), st_fb(3), st_fb(EO_SLOT + 1u), st_ed(1);   /* (ABORT: never COMMITted) */
    if (st_changed()) stress_fail("x_editor: a refused PUT changed something", 0);
    stress_live();
    stress_smp_info();
    stress_names();
    stress_fsamples();
}
static uint32_t st_grand(uint32_t n)
{
    st_grng ^= st_grng << 13, st_grng ^= st_grng >> 17, st_grng ^= st_grng << 5;
    return n ? st_grng % n : 0u;
}
static void stress_garbage(void)                /* "y_garbage" on: a frame of garbage every 47 ms */
{
    static const uint32_t OBJ[6] = {EO_SET, EO_SLOT, EO_SLOT + 3u, EO_SND, EO_SND + 63u, EO_FACT};
    uint32_t k = st_grand(9), o = OBJ[st_grand(6)], i, n;
    st_fhead(ED_INFO + st_grand(3));
    switch (k) {
    case 0:                                     /* random bytes */
        for (i = 0, n = st_grand(600); i < n; i++) st_fb(st_grand(128));
        break;
    case 1: st_fn = st_grand(9); break;                                             /* cut short */
    case 2: st_fhead(ED_GET), st_fb(st_grand(128)), st_f35(st_grand(3000)), st_fb(st_grand(128)), st_fb(st_grand(4)); break;
    case 3: st_fhead(ED_PUT), st_fb(0), st_fb(o), st_fb(st_grand(4) ? ED_FMT : st_grand(128)), st_f35(ed_size(o)),
            st_f35(st_grand(1u << 30)); break;                                     /* a BEGIN (a wrong CRC) */
    case 4:                                     /* a DATA, mostly in order */
        st_fhead(ED_PUT), st_fb(1), st_fb(o), st_f35(st_grand(2) ? ed_got : st_grand(3000));
        for (i = 0, n = st_grand(300); i < n; i++) st_fb(st_grand(128));
        break;
    case 5: st_fhead(ED_PUT), st_fb(2), st_fb(o); break;                            /* a COMMIT */
    case 6: st_fhead(ED_PUT), st_fb(st_grand(6)), st_fb(st_grand(128)); break;
    case 7: st_fhead(st_grand(128)), st_f[1] = (uint8_t)st_grand(128); break;      /* not the editor's */
    default:                                    /* a TRACK READ, or a SET of garbage (never SELECT: the script's track) */
        st_fhead(ED_TRACK), st_fb(st_grand(2)), st_fb(st_grand(6));
        for (i = 0, n = st_grand(90); i < n; i++) st_fb(st_grand(128));
        break;
    }
    if (st_grand(5) == 0u && !host_sx_ready && (st_fn < 4u || st_f[3] < 11u || st_f[3] > 15u)) {   /* (USB's way in;
                                                 * never a sample command: those may erase) */
        memcpy(host_sx, st_f, st_fn), host_sx_n = st_fn, host_sx_ready = 1;
        return;
    }
    st_snap();
    ed_frame(st_f, st_fn, quiet() && !playing);
    if (st_changed()) stress_fail("y_garbage: garbage to the editor changed something (kind)", k);
}

static void stress_tap(uint32_t b)              /* a button tapped (host.c: let go the next millisecond) */
{
    fm1_in.buttons |= 1u << b, host_edges |= 1u << b, host_tap |= 1u << b;
}

static void stress_fail(const char *what, uint32_t a)
{
    printf("STRESS FAIL at %u ms: %s (%u)\n", fm1_ms, what, a);
    fflush(stdout);
    exit(3);
}
static void stress_load(void)
{
    const char *p = getenv("STRESS_CHECKS");
    char kind[16];
    unsigned t;
    FILE *f = p ? fopen(p, "r") : 0;
    if (!f) stress_fail("no STRESS_CHECKS file", 0);
    while (st_nchk < ST_MAXCHK && fscanf(f, "%u %15s", &t, kind) == 2) {
        st_chk_t[st_nchk] = t;
        st_chk_kind[st_nchk++] = (uint32_t)kind[0];
    }
    fclose(f);
}
static void stress_poll(void)
{
    uint32_t k, i, keys = fm1_in.notes;
    if (fm1_ms == 1u) stress_load();
#ifdef HOST_MATRIX
    if (hm_bad)                                 /* (a ghost let through: a press no hand made) */
        stress_fail("a ghost of the matrix reached the app as a press (id)", hm_bad - 1u);
#endif
    for (k = 0; k < 27u; k++) {                 /* the keys let go, when */
        if ((st_prev_keys >> k & 1u) && !(keys >> k & 1u)) st_up_ms[k] = fm1_ms;
        if (keys >> k & 1u) st_up_ms[k] = 0;
    }
    st_prev_keys = keys;
    for (k = 0; k < NT; k++) {                  /* LATCH ON -> OFF, when (its notes released then) */
        if (st_latch[k] && !trk[k].latch) st_latch_ms[k] = fm1_ms;
        st_latch[k] = (uint32_t)trk[k].latch;
    }
    for (k = 0; k < NT; k++) {                  /* ARP off or its list empty, since when */
        uint32_t a = trk[k].arp && arp[k].n;
        if (st_arp[k] && !a) st_arp_ms[k] = fm1_ms;
        st_arp[k] = a;
        if (trk[k].latch || fm1_ms - st_latch_ms[k] <= 60u) continue;
        for (i = 0; i < arp[k].n && i < ARP_N; i++)
            if (arp[k].id[i] < 27u && st_up_ms[arp[k].id[i]] && fm1_ms - st_up_ms[arp[k].id[i]] > 60u)
                stress_fail("a key stays in the arp after it was let go (key)", arp[k].id[i]);
    }
    {                                           /* muted, or another soloed: since when */
        uint32_t q = (trk_mute | ((trk_solo & 15u) ? ~trk_solo : 0u)) & 15u;
        for (k = 0; k < NT; k++)
            if ((q >> k & 1u) && !(st_quiet >> k & 1u)) st_quiet_ms[k] = fm1_ms;
        st_quiet = q;
    }
    if (st_stop && fm1_ms - st_stop_t >= 8u) {  /* "stop": song record off, REC off, PLAY off, a tap at a time */
        st_stop_t = fm1_ms;
        if (song_rec || rec_on) stress_tap(BTN_REC);
        else if (playing) stress_tap(BTN_PLAY);
        else st_stop = 0;
    }
    for (k = 0; k < 7u; k++) {                  /* an FX trick on while its key (G3 .. F4) is up */
        static const uint8_t TK[7] = {2, 4, 6, 7, 9, 11, 12};
        if ((pf_held >> k & 1u) && st_up_ms[TK[k]] && fm1_ms - st_up_ms[TK[k]] > 60u)
            stress_fail("an FX trick stays on after its key was let go (trick)", k);
    }
    if (st_was_playing && !playing && pf_held && !(fm1_in.buttons >> BTN_PLAY & 1u) && song_mode == SM_OFF)
        stress_fail("PLAY went off while an FX trick was held (tricks)", pf_held);
    if (!playing || !seq_on || st_cw_bpm != (uint32_t)seq_bpm || !st_cw_ok) {   /* the clock: a window from now */
        st_cw_t = fm1_ms, st_cw_ticks = seq_ticks, st_cw_bpm = (uint32_t)seq_bpm, st_cw_ok = playing && seq_on;
    } else if (fm1_ms - st_cw_t >= 500u) {      /* half a second: 96 x BPM / 120 ticks */
        int32_t got = (int32_t)(seq_ticks - st_cw_ticks), want = (int32_t)(96u * st_cw_bpm / 120u);
        if (got < want - 3 || got > want + 3) stress_fail("the sequencer's clock is off over half a second (ticks)", (uint32_t)got);
        st_cw_t = fm1_ms, st_cw_ticks = seq_ticks;
    }
    if (st_was_playing && !playing) st_stop_ms = fm1_ms;
    st_was_playing = playing;
    for (i = 0; i < nlat; i++) {                /* a note still on after its key or its step ended */
        uint32_t t = lat_trk[i], id = lat_id[i];
        if (lat_src[i] == SRC_KEY && id < 27u && !trk[t].latch && st_up_ms[id] && fm1_ms - st_up_ms[id] > 60u &&
            fm1_ms - st_latch_ms[t] > 60u)
            stress_fail("a key's note sounds on after it was let go (key)", id);
        if (lat_src[i] == SRC_SEQ && !playing && st_stop_ms && fm1_ms - st_stop_ms > 60u)
            stress_fail("a step's note sounds on after STOP (note)", lat_root[i]);
        if ((st_quiet >> t & 1u) && fm1_ms - st_quiet_ms[t] > 60u)
            stress_fail("a note of a muted (or not soloed) track sounds (track)", t);
        if (lat_src[i] == SRC_ARP && !st_arp[t] && fm1_ms - st_arp_ms[t] > 60u)
            stress_fail("an arp's note sounds on after its ARP went off or its list emptied (note)", lat_root[i]);
    }
    for (; st_wav_i < 2u * wav_n; st_wav_i++) { /* the level: two seconds in a row mostly clipped is a runaway */
        int32_t v = wav[st_wav_i];
        st_clip += v >= 32767 || v <= -32767;
        if (++st_win == 2u * FS) {
            st_hot = st_clip > FS ? st_hot + 1u : 0u;
            if (st_hot >= 2u) stress_fail("the sound runs away (clipped samples in the last second)", st_clip);
            st_clip = st_win = 0;
        }
    }
    if (fm1_ms > 2000u && host_cpu_max > BUDGET_TICKS) {   /* (over: counted, three within a second fail) */
        static uint32_t n, t0;
        if (fm1_ms - t0 > 1000u) n = 0, t0 = fm1_ms;
        if (++n >= 3u) stress_fail("audio blocks over their time budget (us)", host_cpu_max / FM1_TICKS_PER_US);
        host_cpu_max = 0;
    }
    if (st_ichk < st_nchk && fm1_ms >= st_chk_t[st_ichk]) {
        uint32_t v;
        if (st_chk_kind[st_ichk] == 'p') {     /* "stop" (its kind: 'p') */
            st_stop = 1, st_stop_t = fm1_ms - 8u;
        } else if (st_chk_kind[st_ichk] == 'u') {
            stress_upgrade();
        } else if (st_chk_kind[st_ichk] == 'c') {
            stress_cut();
        } else if (st_chk_kind[st_ichk] == 'f') {
            stress_factory();
        } else if (st_chk_kind[st_ichk] == 'g') {   /* "g_source": the page EDIT's SOURCE (the script can't tell
                                                 * how many taps: an FM track's round is longer) */
            page = PG_SOURCE;
        } else if (st_chk_kind[st_ichk] == 'x') {
            stress_editor();
        } else if (st_chk_kind[st_ichk] == 'y') {
            st_garbage = 1;
        } else if (st_chk_kind[st_ichk] == 'h') {
            stress_hlinks();
        } else if (st_chk_kind[st_ichk] == 'v') {
            stress_vsounds();
        } else if (st_chk_kind[st_ichk] == 'm') {
            stress_mfm();
        } else if (st_chk_kind[st_ichk] == 'e' || st_chk_kind[st_ichk] == 'd' || st_chk_kind[st_ichk] == 'w' ||
                   st_chk_kind[st_ichk] == 'l') {
            stress_system(st_chk_kind[st_ichk]);
        } else if (st_chk_kind[st_ichk] == 'b') {   /* power-on's way back: the autosaved project in */
            int32_t tr[NT][3];
            uint8_t fa[NT][FM_PK], fb[FM_PK];
            uint32_t j;
            if (!obj_gen[OBJ_WORK]) stress_fail("boot: nothing autosaved", 0);
            for (v = 0; v < NT; v++) tr[v][0] = trk[v].cut, tr[v][1] = trk[v].res, tr[v][2] = trk[v].fx, pk_fm(fa[v], v);
            store_boot();
            for (v = 0; v < NT; v++) {
                if (trk[v].cut != tr[v][0] || trk[v].res != tr[v][1] || trk[v].fx != tr[v][2])
                    stress_fail("boot: TRACK's CUTOFF, RESONANCE, FX not back (track)", v);
                pk_fm(fb, v);
                for (j = 0; j < FM_PK; j++)
                    if (fa[v][j] != fb[j]) stress_fail("boot: a track's FM patch not back (track)", v);
            }
        } else {
            if (nlat) stress_fail("released: notes still on", nlat);
            for (v = 0; v < 64u; v++)
                if (st_rx[v >> 2][v & 3u]) stress_fail("released: a note still on out there over USB-MIDI (channel)", v >> 2);
            for (v = 0; v < NV; v++) {
                if (vc[v].stage == ST_ATK || vc[v].stage == ST_SUS) stress_fail("released: a voice still held (voice)", v);
                if (st_chk_kind[st_ichk] == 's' && !voice_idle(v)) stress_fail("silent: a voice still sounds (voice)", v);
            }
            if (st_chk_kind[st_ichk] == 's' && !perform_idle()) stress_fail("silent: an FX trick still on", pf_held);
        }
        if (++st_ichk == st_nchk) {
            printf("STRESS OK\n");
            fflush(stdout);
        }
    }
    if (st_garbage && !(fm1_ms % 47u)) stress_garbage();
    if (fm1_ms <= 2000u) host_cpu_max = 0;      /* (boot: cold caches; the budget counts from 2 s) */
}
