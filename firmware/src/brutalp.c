/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V3.0: BRUTAL's pictures: every page's, drawn as docs/themes/brutal-mockup.html draws them (there at 2x: here halved), from the live values. Included by ui.c
 * after EYE's pictures (before the themes' table). A picture is drawn in a region at the screen's coordinates, relative to the picture's corner (BP_X, BP_Y, 234 x 160), a
 * ground of black (the dark pages) or concrete with its faint lines (the light pages); small text and captions in Terminus Bold, big text and numbers in Archivo Black
 * (16 and 24 px, or those scaled by whole pixels), what does not fit big in Terminus. Nothing here uses the EYE pixfont. Only what moves is redrawn (the meters, the LFO's
 * dot: their own regions), no buffer of its own. */

#define BP_X 3                                  /* the picture's corner on the screen, */
#define BP_Y 32                                 /* its size: 234 x 160 */
#define BP_TILE(i) ((i) & 1 ? BR_C2 : BR_C1)    /* the tiles' two concretes, alternately (below: the theme's) */

/* The pictures are shared by BRUTAL and MINIMAL (V3.0): the theme's parameters are the ink and the ground (BRUTAL: black ink on concrete tiles on a light ground; MINIMAL: light ink on dark
 * tiles on black), the selected tile (BRUTAL: black with a red frame; MINIMAL: a lighter dark with a 2 px amber underline), the lines' width (5 px; 2), the text (Archivo Black 16 / 24 and Terminus Bold;
 * Terminus only: the bold for the ink, the regular for the dim, 24 px as Terminus 6x12 doubled, 16 as 8x16), and MINIMAL's GLOW (a halo round the light ink). BRUTAL's drawing is unchanged by it
 * (tests/brutal_hash_test.py) */
#define BP_MIN (ui_th->bfin == 2)
#define BP_INK (BP_MIN ? UI_L : UI_K)           /* the ink on a tile */
#define BP_ON (BP_MIN ? UI_L : UI_W)            /* the text on a selected or dark tile */
#define BP_C1 (BP_MIN ? UI_D : BR_C1)           /* the tiles' concretes, the dark of MINIMAL's tile */
#define BP_C2 (BP_MIN ? UI_D : BR_C2)
#define BP_C3 (BP_MIN ? UI_D : BR_C3)
#define BP_TRACK (BP_MIN ? MN_TRACK : BR_TRACK) /* a bar's empty part, a faint line */
#define BP_SELBG (BP_MIN ? MN_TRACK : BR_SELBG) /* .. on the selected tile */
#define BP_SELLAB (BP_MIN ? ui_acc : BR_SELLAB) /* the selected tile's label */
#undef BP_TILE
#define BP_TILE(i) ((i) & 1 ? BP_C2 : BP_C1)

static void bp_r(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { ui_rect(BP_X + x, BP_Y + y, w, h, c); }
static void bp_ground(int light)                /* the picture's ground: the light pages' concrete with its faint lines (every 11 px, as the mockup's); MINIMAL's is black */
{
    int32_t y;
    if (!light || BP_MIN) return;
    bp_r(0, 0, 234, 160, BP_C1);
    for (y = 10; y < 160; y += 11) bp_r(0, y, 234, 1, BP_C2);
}
static void bp_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t f, int sel)   /* a tile: its colour, or the selected one (BRUTAL: black with a red frame; MINIMAL: lighter, an amber underline) */
{
    if (BP_MIN) {
        bp_r(x, y, w, h, sel ? MN_SEL : f);
        if (sel) bp_r(x, y + h - 2, w, 2, ui_acc);
        return;
    }
    if (sel) {
        bp_r(x, y, w, h, UI_K);
        bp_r(x, y, w, 2, ui_acc), bp_r(x, y + h - 2, w, 2, ui_acc), bp_r(x, y, 2, h, ui_acc), bp_r(x + w - 2, y, 2, h, ui_acc);
    } else
        bp_r(x, y, w, h, f);
}
static void bp_bar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t num, int32_t den, int sel)   /* a bar num / den full: black on the concrete's track (white on black's; MINIMAL: light, amber selected) */
{
    bp_r(x, y, w, h, sel ? BP_SELBG : BP_TRACK);
    if (num > 0) bp_r(x, y, den > 0 && num < den ? w * num / den : w, h, sel ? (BP_MIN ? ui_acc : UI_W) : BP_INK);
}
/* Terminus at (x, y) = the capitals' ink top, left / right / centred on x (MINIMAL: the regular cut for the dim inks) */
static const br_tfont_t *bp_tf(const br_tfont_t *f, uint16_t c) { return !BP_MIN || (c != UI_M && c != MN_DIM) ? f : f == &BRT12 ? &BRT12R : &BRT14R; }
static void bp_tt(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c)
{
    if (BP_MIN) mn_t(BP_X + x, BP_Y + y, s, bp_tf(f, c), c);
    else br_tt(BP_X + x, BP_Y + y - f->capy, s, f, c);
}
static void bp_ttr(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c) { bp_tt(x - br_tlen(f, s), y, s, f, c); }
static void bp_ttc(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c) { bp_tt(x - br_tlen(f, s) / 2, y, s, f, c); }
/* Archivo Black f (16 or 24 px) scaled sc, at (x, y) = the capitals' ink top; MINIMAL's: Terminus Bold, the 24 as 6x12 doubled, the 16 as 8x16 */
static int32_t bp_alen(const br_font_t *f, const char *s, int32_t sc) { return BP_MIN ? (f == &BRF24 ? 12 : 8) * ui_len(s) * sc : br_len(f, s, sc); }
static int32_t bp_acap(const br_font_t *f) { return BP_MIN ? (f == &BRF24 ? 16 : 10) : br_cap(f); }
static int32_t bp_acapy(const br_font_t *f) { return BP_MIN ? (f == &BRF24 ? 4 : 2) : br_capy(f); }
static void bp_a(int32_t x, int32_t y, const char *s, const br_font_t *f, int32_t sc, uint16_t c)
{
    if (BP_MIN) {
        const br_tfont_t *t = f == &BRF24 ? &BRT12 : &BRT16;
        int32_t m = (f == &BRF24 ? 2 : 1) * sc;
        mn_tt(BP_X + x, BP_Y + y - t->capy * m, s, t, m, c);
    } else
        br_text(BP_X + x, BP_Y + y - br_capy(f) * sc, s, f, sc, c);
}
/* a value: as big as it fits in `avail` px (Archivo 24, 16, then Terminus 16, 14, 12), its capitals' ink ending at yb; a 0 left of x, 1 right of x, 2 centred on x */
static void bp_val(int32_t x, int32_t yb, const char *s, int32_t avail, uint16_t c, int al)
{
    const br_font_t *f = bp_alen(&BRF24, s, 1) <= avail ? &BRF24 : bp_alen(&BRF16, s, 1) <= avail ? &BRF16 : 0;
    const br_tfont_t *t = br_tlen(&BRT16, s) <= avail ? &BRT16 : br_tlen(&BRT14, s) <= avail ? &BRT14 : &BRT12;
    int32_t w = f ? bp_alen(f, s, 1) : br_tlen(t, s);
    x = al == 1 ? x - w : al == 2 ? x - w / 2 : x;
    if (f) bp_a(x, yb - bp_acap(f), s, f, 1, c);
    else bp_tt(x, yb - t->cap, s, t, c);
}
static void bp_caps(int32_t x, int32_t y, const char *s, uint16_t c) { bp_tt(x, y, s, &BRT12, c); }   /* a small label */

/* ---- SOURCE: the seven sources as tiles (the track's selected), LATCH, TYPE (VOICE), GLIDE */
static void bp_source(void)
{
    int32_t i, e = (int32_t)sel_eng();
    char g[12];
    for (i = 0; i < (int32_t)NENG; i++) {
        int32_t x = (i & 3) * 59, y = (i >> 2) * 54;
        bp_box(x, y, 57, 51, BP_TILE(i), i == e);
        bp_val(x + 28, y + 33, SRC_CODE[i], 52, i == e ? ui_acc : BP_INK, 2);
    }
    bp_box(177, 54, 57, 51, trk[sel].latch ? ui_acc : BP_C2, 0);
    bp_caps(183, 60, "LATCH", trk[sel].latch ? BP_ON : BP_INK);
    bp_val(183, 98, trk[sel].latch ? "ON" : "OFF", 46, trk[sel].latch ? BP_ON : BP_INK, 0);
    bp_r(0, 109, 234, 51, BP_C3);
    if (e == ENG_VOICE) {                       /* (VOICE's TYPE: one singer, or a choir a note) */
        bp_caps(10, 119, "TYPE", BP_INK);
        bp_val(10, 148, VT_NAME[clamp(trk[sel].vty, 0, NVT - 1)], 120, trk[sel].vty ? ui_acc : BP_INK, 0);
    }
    bp_caps(224 - 6 * 5, 119, "GLIDE", BP_INK);
    if (trk[sel].glide) ui_time(g, 10u * pow2_q16(clamp(trk[sel].glide, 0, 127) * 12)); else put_s(g, "OFF");
    bp_ttr(224, 134, g, &BRT14, BP_INK);
}

/* ---- PERFORM: the eight tricks (FX held + the white keys; on: the selected style), GENERATE's four tracks under them (on: red; the mode, the range, a square flashing as it plays) */
static void bp_perform(void)
{
    static const char *const KEY[8] = {"F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4"};
    static const char *const NM[8][2] = {{"FREEZE", ""}, {"REV", "THROW"}, {"ECHO", "THROW"}, {"TAPE", "STOP"},
                                         {"REVER-", "SE"}, {"FILTER", "DOWN"}, {"FILTER", "UP"}, {"STUT-", "TER"}};
    int32_t i;
    for (i = 0; i < 8; i++) {
        int32_t x = (i & 3) * 59, y = (i >> 2) * 53;
        int on = i ? (int)(pf_held >> (i - 1) & 1u) : (int)(ge_freeze & 1u);
        bp_box(x, y, 57, 50, BP_TILE(i), on);
        bp_tt(x + 6, y + 6, KEY[i], &BRT14, on ? ui_acc : BP_INK);
        bp_tt(x + 6, y + 28, NM[i][0], &BRT12, on ? BP_ON : BP_INK);
        bp_tt(x + 6, y + 38, NM[i][1], &BRT12, on ? BP_ON : BP_INK);
    }
    bp_r(0, 109, 234, 51, BP_C3);
    bp_caps(8, 114, "GENERATE", BP_INK);
    for (i = 0; i < (int32_t)NT; i++) {
        int32_t x = 8 + i * 56, y = 126;
        int on = (int)(gen_on >> i & 1u);
        char r[12], m[4];
        bp_r(x, y, 50, 20, on ? ui_acc : BP_C2);
        put_u(put_s(r, "T"), (uint32_t)i + 1u);
        bp_tt(x + 4, y + 6, r, &BRT12, on ? BP_ON : BP_INK);
        m[0] = GM_NAME[gen_md[i] % NGM][0], m[1] = GM_NAME[gen_md[i] % NGM][1], m[2] = GM_NAME[gen_md[i] % NGM][2], m[3] = 0;
        bp_tt(x + 22, y + 6, m, &BRT12, on ? BP_ON : BP_INK);
        if (gen_fl >> i & 1u) bp_r(x + 42, y + 3, 5, 5, on ? BP_ON : BP_INK);
        ui_note(put_s(ui_note(r, gen_low((uint32_t)i)), "-"), gen_high((uint32_t)i));
        bp_tt(x, y + 26, r, &BRT12, BP_INK);
    }
}

/* ---- MIXER 1: EYE's layout exactly (MXT_: six tiles, 3 x 2, in BRUTAL's frame: moved down by the theme's dyf): a label, the 8 segments (7 x 30 px, 8 apart) of the level or the send, LEVEL's live
 * (the sound now) with a notch under them where LEVEL is set, the value (2 lines of pixels high: Archivo Black 24 or 16), FX0 dim when the sends do nothing, the one SELECT picks with a frame.
 * Only restyled: concrete tiles, black segments (red on the selected tile, and LEVEL's top two), the selected tile black with a red frame, Terminus Bold labels */
#define BM_X(i) (MXT_X0 + (int32_t)((i) % 3u) * MXT_DX)
#define BM_Y(i) (MXT_Y0 + 3 + (int32_t)((i) / 3u) * MXT_DY)
static void bp_mix1(void)
{
    static const char *const LAB[MX_N] = {"LEVEL", "DELAY", "REVERB", "TAPE", "GRAIN", "DRIVE"};
    uint32_t i;
    int32_t j;
    for (i = 0; i < MX_N; i++) {
        int32_t x = BM_X(i), y = BM_Y(i), v = clamp(*mx_tile(sel, i), 0, 127), on = (int32_t)i == mx_sq, f = i == MX_LEVEL ? mx_h[sel] : (v * 8 + 63) / 127;
        int dry = i != MX_LEVEL && trk[sel].fx <= 0;   /* (FX 0 on HOME: the sends do nothing: dim, "FX0") */
        uint16_t fc = dry ? UI_M : on ? ui_acc : BP_INK;
        const br_font_t *bf;
        char s[4];
        ui_rect(x, y, MXT_W, MXT_H, UI_K);
        if (on) ui_rect(x, y, MXT_W, 2, ui_acc), ui_rect(x, y + MXT_H - 2, MXT_W, 2, ui_acc), ui_rect(x, y, 2, MXT_H, ui_acc), ui_rect(x + MXT_W - 2, y, 2, MXT_H, ui_acc);
        else ui_rect(x, y, MXT_W, MXT_H, BP_TILE(i));
        br_tt(x + 6, y + 8 - BRT12.capy, LAB[i], &BRT12, on ? BP_SELLAB : BP_INK);
        for (j = 0; j < 8; j++)
            ui_rect(x + 6 + j * 8, y + 28, 7, 30, j >= f ? (on ? BP_SELBG : BP_TRACK) : i != MX_LEVEL ? fc : on || j >= 6 ? ui_acc : BP_INK);
        if (i == MX_LEVEL) ui_rect(x + 6 + v * 60 / 127, y + 60, 3, 5, fc);
        ui_3(s, ui_pc(v));
        if (dry) put_s(s, "FX0");
        bf = br_len(&BRF24, s, 1) <= MXT_W - 12 ? &BRF24 : &BRF16;
        br_text(x + 6, y + 76 - br_capy(bf), s, bf, 1, dry ? ui_acc : fc);
    }
}
static void bp_meter_region(uint32_t t)         /* a meter moved: its region (MIXER 1: the selected track's LEVEL bar; LEVELS: that track's meter) */
{
    if (page == PG_MIX1) ui_region(BM_X(0) + 6, BM_Y(0) + 28, 63, 30, UI_K, ui_th->pic);
    else ui_region(BP_X + (int32_t)t * 59 + 31, BP_Y + 50, 21, 100, UI_K, ui_th->pic);
}

/* ---- MIXER 2: a column a track (PAN: a bar and its knob, the filter's TYPE and POS; the selected track black with a red frame) */
static void bp_mix2(void)
{
    int32_t t;
    for (t = 0; t < (int32_t)NT; t++) {
        int32_t x = t * 59, p = clamp(trk[t].pan, 0, 127), on = t == (int32_t)sel;
        char nm[3] = {'0', (char)('1' + t), 0}, v[6];
        bp_box(x, 0, 57, 160, BP_TILE(t), on);
        bp_a(x + 6, 6, nm, &BRF16, 1, on ? ui_acc : BP_INK);
        bp_r(x + 6, 36, 45, 6, on ? BP_SELBG : BP_TRACK);
        bp_r(x + 6 + (45 - 6) * p / 127, 33, 6, 12, on ? BP_ON : BP_INK);
        if (p == 64) put_s(v, "C"); else put_u(put_s(v, p < 64 ? "L" : "R"), (uint32_t)(p < 64 ? 64 - p : p - 64));
        bp_tt(x + 6, 50, v, &BRT12, on ? BP_ON : BP_INK);
        bp_val(x + 6, 118, FT_NAME[clamp(trk[t].ftype, 0, NFTYPE - 1)], 45, on ? BP_ON : BP_INK, 0);
        bp_tt(x + 6, 128, trk[t].fpos ? "POST" : "PRE", &BRT12, on ? BP_ON : BP_INK);
    }
}

/* ---- LEVELS (V3.0): a column a track: its name, its LEVEL in %, two vertical bars of 8 segments, the LEVEL set (the concrete's light) and beside it the sound now (the live
 * meter, the theme's colours) */
static void bp_levels(void)
{
    int32_t t, j;
    for (t = 0; t < (int32_t)NT; t++) {
        int32_t x = t * 59, f = (clamp(trk[t].level, 0, 127) * 8 + 63) / 127, q = trk_quiet >> t & 1u;
        char nm[3] = {'T', (char)('1' + t), 0}, v[6];
        bp_box(x, 0, 57, 160, UI_D, 0);
        bp_tt(x + 6, 6, nm, &BRT14, UI_L);
        bp_tt(x + 30, 6, SRC_CODE[clamp(trk[t].eng, 0, NENG - 1)], &BRT14, UI_M);
        ui_3(v, ui_pc(trk[t].level));
        bp_val(x + 6, 36, q ? "MUTE" : v, 45, q ? UI_M : BP_ON, 0);
        for (j = 0; j < 8; j++) {
            bp_r(x + 6, 50 + (7 - j) * 12, 20, 10, j < f ? (q ? UI_M : UI_L) : UI_K);
            bp_r(x + 31, 50 + (7 - j) * 12, 20, 10, j < (int32_t)mx_h[t] ? ui_meter_col(j) : UI_K);
        }
    }
}

/* ---- SOUNDS: the sound slots (TYPE's list), six rows, the selected one black with a red frame */
static void bp_sounds_row(int32_t i, int on, const char *l, const char *r)
{
    int32_t y = i * 26;
    bp_box(0, y, 234, 24, BP_TILE((uint32_t)i), on);
    bp_tt(8, y + 7, l, &BRT14, on ? BP_ON : BP_INK);
    bp_ttr(226, y + 7, r, &BRT14, on ? ui_acc : BP_INK);
}
static void bp_sounds(void)
{
    int32_t list[SND_N], n = 0, i, at = -1, from;
    for (i = 0; i < (int32_t)SND_N; i++)
        if (sv_shown((uint32_t)i)) {
            if (i == sv_slot) at = n;
            list[n++] = i;
        }
    if (!n) { bp_ttc(117, 70, "NO SOUNDS", &BRT16, UI_M); return; }
    from = clamp((at < 0 ? 0 : at) - 1, 0, n > 6 ? n - 6 : 0);
    for (i = 0; i < 6 && from + i < n; i++) {
        const uint8_t *r = snd_rec((uint32_t)list[from + i]);
        char l[24], *e = ui_3(l, (uint32_t)list[from + i] + 1u);
        *e++ = ' ';
        if (r) snd_name((uint32_t)list[from + i], e); else put_s(e, "---");
        bp_sounds_row(i, from + i == at, l, r ? ENGINES[r[3]]->name : "EMPTY");
    }
}

/* ---- PROJECTS: the slots A .. D as four tiles (the selected black with a red frame; the one in use's state in red), used or not */
static void bp_projects(void)
{
    int32_t i;
    for (i = 0; i < 4; i++) {
        int32_t x = (i & 1) * 119, y = (i >> 1) * 82, on = i == (pj_slot & 3);
        const char *st = seq_recall == i ? "NEXT BAR" : i == slot_cur ? "IN USE" : slot_used((uint32_t)i) ? "SAVED" : "EMPTY";
        char nm[2] = {(char)('A' + i), 0};
        bp_box(x, y, 115, 78, slot_used((uint32_t)i) ? BP_C1 : BP_C2, on);
        bp_a(x + 8, y + 10, nm, &BRF16, 3, on ? ui_acc : slot_used((uint32_t)i) ? BP_INK : UI_M);
        bp_ttr(x + 107, y + 62, st, &BRT14, i == slot_cur ? ui_acc : on ? BP_ON : BP_INK);
    }
}

/* ---- PATTERN: the patterns A .. D as four tiles (A: the one there is, its steps or PLAY), under them the steps big, the division, the swing and the play mode */
static void bp_pattern(void)
{
    const seq_trk_t *S = &seq[sel];
    int32_t i;
    char v[12];
    for (i = 0; i < 4; i++) {
        int32_t x = i * 59;
        char nm[2] = {(char)('A' + i), 0};
        bp_box(x, 0, 57, 50, BP_TILE(i), i == 0);
        bp_a(x + 28 - bp_alen(&BRF24, nm, 1) / 2, 8, nm, &BRF24, 1, i == 0 ? ui_acc : UI_M);
        if (!i) {
            if (seq_on) put_s(v, "PLAY"); else put_s(put_u(v, (uint32_t)seq_steps(S)), " ST");
            bp_ttc(x + 28, 36, v, &BRT12, BP_ON);
        } else
            bp_ttc(x + 28, 36, "EMPTY", &BRT12, UI_M);
    }
    bp_r(0, 53, 234, 107, BP_C3);
    put_u(v, (uint32_t)seq_steps(S));
    bp_a(8, 70, v, &BRF16, 4, BP_INK);
    bp_ttr(226, 62, "STEPS", &BRT12, BP_INK);
    bp_val(226, 100, SEQ_DIV_NAME[clamp(S->div, 0, 7)], 90, BP_INK, 1);
    put_s(v, "SWING ");
    ui_3(v + 6, ui_pc(S->swing));
    bp_ttr(226, 122, v, &BRT12, BP_INK);
    bp_ttr(226, 138, SEQ_MODE_NAME[clamp(S->mode, 0, SEQ_NMODE - 1)], &BRT12, BP_INK);
}

/* ---- SONG: the chain's 16 steps, 8 a row (its slot's letter, its REPEATS, its number; the step playing or shown black with a red frame; END; the rest empty), the MODE under them */
static void bp_song(void)
{
    int32_t cur = song_cur(), len = (int32_t)song_length(), i;
    char b[8];
    for (i = 0; i < SONG_N; i++) {
        int32_t x = (i & 7) * 29, y = (i >> 3) * 52, on = i == cur, p = song_pat[i];
        if (i > len) { bp_box(x, y, 27, 50, UI_D, 0); continue; }
        bp_box(x, y, 27, 50, i == len ? BP_C3 : BP_TILE(i), on);
        put_u(b, (uint32_t)i + 1u);
        bp_tt(x + 4, y + 5, b, &BRT12, on ? BP_ON : UI_M);
        if (i == len) { bp_ttc(x + 13, y + 28, "END", &BRT12, BP_INK); continue; }
        b[0] = p >= 0 && p < SONG_END ? (char)('A' + p) : '-', b[1] = 0;
        bp_a(x + 13 - bp_alen(&BRF16, b, 1) / 2, y + 16, b, &BRF16, 1, on ? ui_acc : BP_INK);
        if (i == song_at && song_rec != 2u) put_u(put_s(put_u(b, song_left), "/"), (uint32_t)clamp(song_rep[i], 1, 16));
        else put_u(put_s(b, "X"), (uint32_t)clamp(song_rep[i], 1, 16));
        bp_ttc(x + 13, y + 38, b, &BRT12, on ? BP_ON : BP_INK);
    }
    bp_r(0, 108, 234, 52, BP_C3);
    bp_val(8, 142, SONG_MODE_NAME[clamp(song_mode, 0, NSONGMODE - 1)], 130, BP_INK, 0);
    put_u(put_s(b, "LEN "), (uint32_t)len);
    bp_ttr(226, 122, b, &BRT14, BP_INK);
    if (song_rec) bp_ttr(226, 138, "REC", &BRT14, ui_acc);
}

/* ---- SYSTEM 3: LATCH, VELOCITY, THEME (V3.0): three rows, a name and its setting big */
static void bp_sys3(void)
{
    static const char *const NM[3] = {"LATCH", "VELOCITY", "THEME"};
    int32_t i;
    for (i = 0; i < 3; i++) {
        const char *v = i == 0 ? (set.latch & 1u ? "ALWAYS OFF" : "FROM SOUND") : i == 1 ? (set.latch & 2u ? "ON" : "OFF") : THEME_NAME[(set.latch >> 4 & 3u) % THEME_N];
        bp_box(0, i * 54, 234, 52, BP_TILE(i), 0);
        bp_caps(10, i * 54 + 8, NM[i], BP_INK);
        bp_val(10, i * 54 + 42, v, 214, BP_INK, 0);
    }
}

/* ---- the curve pages (stage 2b): light ground, a title (Archivo 16) with a note at the right (Terminus), the curve a thick line (5 px; the second, behind it, 3 px in the
 * mid grey), from the live values; points in picture coordinates, a polyline of at most 53 */
static void bp_pl(int16_t *q, uint32_t n, int32_t w, uint16_t c)   /* a line through n points (in place: moved to the screen) */
{
    uint32_t i;
    for (i = 0; i < n; i++) q[2 * i] = (int16_t)(q[2 * i] + BP_X), q[2 * i + 1] = (int16_t)(q[2 * i + 1] + BP_Y);
    if (BP_MIN) {                               /* (MINIMAL: 2 px, or 1 for the thin ones; GLOW: a halo 1 px round it first, if the ink is a light one) */
        w = w >= 5 ? 2 : 1;
        if (mn_lit(c)) br_polyline(q, n, w + 2, mn_mix(c, 1, 3));
    }
    br_polyline(q, n, w, c);
}
static void bp_head(const char *l, const char *r, uint16_t rc)   /* the title, and a note at the right */
{
    int32_t av = r ? 214 - br_tlen(&BRT12, r) - 12 : 214;
    if (bp_alen(&BRF16, l, 1) <= av) bp_a(10, 8, l, &BRF16, 1, BP_INK);
    else bp_tt(10, 9, l, &BRT14, BP_INK);
    if (r) bp_ttr(224, 9, r, &BRT12, rc);
}
static void bp_wave(int sq, int32_t p, int32_t o, int32_t w, uint16_t c)   /* VA: a saw (or square) of period p from offset o across the picture (the edges clip it) */
{
    int16_t q[64];
    uint32_t n = 0;
    int32_t x = -(o % p);
    for (; x < 234 && n < 30u; x += p) {
        if (sq) {
            q[2 * n] = (int16_t)x, q[2 * n + 1] = 45, n++;
            q[2 * n] = (int16_t)(x + p / 2), q[2 * n + 1] = 45, n++;
            q[2 * n] = (int16_t)(x + p / 2), q[2 * n + 1] = 145, n++;
            q[2 * n] = (int16_t)(x + p), q[2 * n + 1] = 145, n++;
        } else {
            q[2 * n] = (int16_t)x, q[2 * n + 1] = 145, n++;
            q[2 * n] = (int16_t)(x + p), q[2 * n + 1] = 45, n++;
        }
    }
    bp_pl(q, n, w, c);
}
static void bp_va(void)                         /* the waveform of WAVE; a second behind it by the detune (SUPERSAW: two, a spread) */
{
    const volatile int32_t *p = trk[sel].p[ENG_VA];
    int32_t w = p[0], dt = clamp(p[1], 0, 127) / 16 * 3;
    bp_ground(1);
    bp_head(w == WAVE_SUPER ? "SUPERSAW" : w == WAVE_SQR ? "SQUARE" : "SAW", 0, BP_INK);
    bp_r(0, 146, 234, 1, BP_TRACK);
    if (w == WAVE_SUPER) bp_wave(0, 57, 33, 3, UI_M), bp_wave(0, 51, 15, 3, UI_M), bp_wave(0, 54, 0, 5, BP_INK);
    else bp_wave(w == WAVE_SQR, 54 + dt, 9, 3, UI_M), bp_wave(w == WAVE_SQR, 54, 0, 5, BP_INK);
}

static void bp_phase(void)                      /* the bent waveform of WAVE at DCW, then WAVE2 (or WAVE again) beside it, the bend's point a red square under each */
{
    const volatile int32_t *p = trk[sel].p[ENG_PHASE];
    uint32_t w1 = (uint32_t)clamp(p[0], 0, 7), w2 = (uint32_t)clamp(p[1], 0, 8), dcw;
    int32_t c, i;
    int16_t q[2 * 26];
    pd_t b;
    char s[24], t[16], u[16];
    dcw = ((uint32_t)clamp(p[2], 0, 127) * 65535u / 127u * 56000u) >> 16;
    phase_text(p, 0, t);
    phase_text(p, 1, u);
    put_s(put_s(s, "WAVE2 "), u);
    bp_ground(1);
    bp_head(t, s, UI_M);
    bp_r(0, 85, 234, 1, BP_TRACK);
    for (c = 0; c < 2; c++) {
        int32_t x0 = 12 + c * 110;
        pd_setup(&b, c && w2 ? w2 - 1u : w1, dcw);
        for (i = 0; i < 26; i++) q[2 * i] = (int16_t)(x0 + i * 4), q[2 * i + 1] = (int16_t)(85 - pd_wave(&b, (uint32_t)i * 65535u / 25u) * 50 / 32767);
        bp_pl(q, 26, 5, c ? UI_M : BP_INK);
        bp_r(x0 + (int32_t)(b.x * 100u / 65535u) - 3, 142, 7, 7, ui_acc);
    }
}

static void bp_pdline(void)                     /* (V2) LINE 2: line 1 (black) beside line 2 (grey: its WAVE, DCW), how they meet */
{
    const volatile int32_t *p = trk[sel].p[ENG_PHASE];
    int32_t c, i, w2 = clamp(trk[sel].pd_wav, 0, 8), d2 = clamp(trk[sel].pd_dcw, 0, 127);
    int mix = p[3] || trk[sel].pd_noise || w2 || d2;
    int16_t q[2 * 24];
    pd_t b;
    bp_ground(1);
    bp_head("LINE 2", trk[sel].pd_ring ? "RING 1 X 2" : mix ? "MIX 1 + 2" : "LINE 1 ALONE", trk[sel].pd_ring ? ui_acc : UI_M);
    bp_r(0, 80, 234, 1, BP_TRACK);
    for (c = 0; c < 2; c++) {
        pd_setup(&b, c && w2 ? (uint32_t)w2 - 1u : (uint32_t)clamp(p[0], 0, 7), pd_dcw(c && d2 ? d2 : p[2]));
        for (i = 0; i < 24; i++) q[2 * i] = (int16_t)(12 + c * 112 + i * 4), q[2 * i + 1] = (int16_t)(80 - pd_wave(&b, (uint32_t)i * 65535u / 23u) * 40 / 32767);
        bp_pl(q, 24, 5, c ? UI_M : BP_INK);
        bp_tt(12 + c * 112, 128, c ? "LINE 2" : "LINE 1", &BRT12, c ? UI_M : BP_INK);
    }
    bp_tt(12, 146, trk[sel].pd_noise ? "NOISE ON" : "NOISE OFF", &BRT12, trk[sel].pd_noise ? ui_acc : UI_M);
}

static void bp_pdenv(void)                      /* (V2) PHASE ENV: DCW (black: up from the floor) and PITCH (grey: up or down from the middle), each over ~1.2 s */
{
    int32_t c, i, j;
    int16_t q[2 * 53];
    bp_ground(1);
    bp_tt(10, 9, trk[sel].pd_de ? "DCW ON" : "DCW OFF", &BRT14, trk[sel].pd_de ? ui_acc : BP_INK);
    bp_ttr(224, 9, trk[sel].pd_pe ? "PITCH ON" : "PITCH OFF", &BRT14, trk[sel].pd_pe ? ui_acc : UI_M);
    bp_r(0, 90, 234, 1, BP_TRACK);
    for (c = 0; c < 2; c++) {                   /* (the grey first: the black over it) */
        int32_t pt = 1 - c, d = pt ? clamp(trk[sel].pd_pe, -64, 63) : clamp(trk[sel].pd_de, 0, 127);
        uint32_t k = env_k(10u * pow2_q16(clamp(pt ? trk[sel].pd_pd : trk[sel].pd_dd, 0, 127) * 15)), l = 65536u;
        int32_t base = pt ? 90 : 148, span = pt ? clamp(55 * d / 24, -55, 55) : 105 * d / 127;
        for (i = 0; i < 53; i++) {
            q[2 * i] = (int16_t)(12 + i * 4), q[2 * i + 1] = (int16_t)(base - (int32_t)(((int64_t)span * l) >> 16));
            for (j = 0; j < 32; j++) l -= (uint32_t)mulq16((int32_t)l, k);
        }
        bp_pl(q, 53, pt ? 3 : 5, pt ? UI_M : BP_INK);
    }
}

static void bp_env(void)                        /* the shape of the track's ATTACK, DECAY, SUSTAIN, RELEASE (their letters over their parts) */
{
    const volatile int32_t *e = trk[sel].env;
    int32_t wa = 2 + clamp(e[0], 0, 127) * 18 / 127, wd = 2 + clamp(e[1], 0, 127) * 12 / 127, wr = 2 + clamp(e[3], 0, 127) * 14 / 127, ws = 53 - wa - wd - wr;
    int32_t ys = 148 - clamp(e[2], 0, 127) * 112 / 127, x1 = 10 + wa * 4, x2 = x1 + wd * 4, x3 = x2 + ws * 4, x4 = x3 + wr * 4;
    int16_t q[10];
    bp_ground(1);
    q[0] = 10, q[1] = 148, q[2] = (int16_t)x1, q[3] = 36, q[4] = (int16_t)x2, q[5] = (int16_t)ys, q[6] = (int16_t)x3, q[7] = (int16_t)ys, q[8] = (int16_t)x4, q[9] = 148;
    bp_pl(q, 5, 5, BP_INK);
    bp_ttc((10 + x1) / 2, 9, "A", &BRT16, UI_M);
    bp_ttc((x1 + x2) / 2, 9, "D", &BRT16, UI_M);
    bp_ttc((x2 + x3) / 2, 9, "S", &BRT16, UI_M);
    bp_ttc((x3 + x4) / 2, 9, "R", &BRT16, UI_M);
}

static void bp_dot_region(uint32_t col)         /* the LFO's dot at a column: its strip of the picture */
{
    ui_region(BP_X + 4 + (int32_t)col * 4, BP_Y, 14, 160, UI_K, ui_th->pic);
}
static void bp_lfo(void)                        /* a cycle of SHAPE across the picture, the red dot where the LFO is (at its rate) */
{
    uint32_t sh = (uint32_t)clamp(trk[sel].lfo_shape, 0, NLFOSHAPE - 1), dx = lfo_dot(), x;
    uint32_t ms = 3932160000u / lfo_hz_q16(trk[sel].lfo_rate);
    int16_t q[2 * 54];
    char l[32];
    lfo_rate_text(put_s(l, ms >= 995u ? "1 CYCLE = " : ""), trk[sel].lfo_rate, " ");
    put_s(l + ui_len(l), " / FREE");
    bp_ground(1);
    bp_head(LFO_SHAPE_NAME[sh], l, UI_M);
    bp_r(0, 80, 234, 1, BP_TRACK);
    for (x = 0; x < 54u; x++) q[2 * x] = (int16_t)(10u + x * 4u), q[2 * x + 1] = (int16_t)(80 - lfo_val(sh, x) * 55 / 32767);
    bp_pl(q, 54, 5, trk[sel].lfo_depth > 0 ? BP_INK : UI_M);
    {
        int32_t ds = BP_MIN ? 6 : 11;           /* (the dot: 11 px; MINIMAL's 6) */
        bp_r(10 + (int32_t)dx * 4 - ds / 2, 80 - lfo_val(sh, dx) * 55 / 32767 - ds / 2, ds, ds, ui_acc);
    }
}

static void bp_tape(void)                       /* the two reels and the tape between them, its wobble WOW; the tracks on it */
{
    int32_t k, r, amp = clamp(tf_wow, 0, 127) * 8 / 127;
    int16_t q[2 * 17];
    char on[16] = "ON:", *e = on + 3;
    for (k = 0; k < (int32_t)NT; k++)
        if (trk[k].tap > 0 && trk[k].fx > 0) *e++ = ' ', *e++ = 'T', *e++ = (char)('1' + k), *e = 0;
    bp_ground(1);
    for (r = 0; r < 2; r++) {
        if (BP_MIN) {                           /* (MINIMAL: a reel a 2 px frame in the mid grey, a small hub; no posts) */
            bp_r(35 + r * 104, 20, 60, 60, UI_M);
            bp_r(37 + r * 104, 22, 56, 56, UI_K);
            bp_r(62 + r * 104, 47, 6, 6, BP_INK);
            continue;
        }
        bp_r(35 + r * 104, 20, 60, 60, BP_INK);
        bp_r(55 + r * 104, 40, 20, 20, BP_C1);
        bp_r(65 + r * 104, 80, 4, 20, BP_INK);
    }
    for (k = 0; k < 17; k++) q[2 * k] = (int16_t)(35 + k * 10), q[2 * k + 1] = (int16_t)(103 + ((sine_i((uint32_t)k * 804212224u) * amp + 16384) >> 15));
    bp_pl(q, 17, 5, BP_INK);
    if (e > on + 3) bp_a(10, 134, on, &BRF16, 1, BP_INK);
    else bp_tt(10, 136, "OFF: TURN UP A TAPE TILE", &BRT14, UI_M);
}

static void bp_grain(void)                      /* the cloud: squares scattered by SCATTER round the position, as many as DENSITY, as big as SIZE */
{
    int32_t i, n = 8 + clamp(ge_dens, 0, 127) * 30 / 127, sp = 8 + clamp(ge_scat, 0, 127) * 100 / 127, bs = 6 + clamp(ge_size, 0, 127) * 15 / 127;
    char r[12];
    bp_ground(1);
    ui_rnd_s = 9;
    for (i = 0; i < n; i++) {
        int32_t sz = bs / 2 + (int32_t)ui_rnd((uint32_t)bs / 2u + 1u), gx = 117 - sp + (int32_t)ui_rnd((uint32_t)sp * 2u + 1u), gy = 24 + (int32_t)ui_rnd((uint32_t)(127 - sz));
        uint32_t cl = ui_rnd(8);
        if (BP_MIN) sz = (sz + 1) / 2;          /* (MINIMAL's grains: half the size) */
        bp_r(gx - sz / 2, gy, sz, sz, cl == 0 ? ui_acc : cl < 5 ? BP_INK : UI_M);
    }
    bp_tt(10, 9, "BUFFER 1.25 S", &BRT12, BP_INK);
    if (ge_freeze) {
        bp_r(176, 4, 54, 16, ui_acc);
        bp_ttc(203, 9, "FREEZE", &BRT12, BP_ON);
    } else {
        put_s(put_u(r, (pow2_q16(clamp(ge_size, 0, 127) * 8) * 10u + 32768u) >> 16), " MS");
        bp_ttr(224, 9, r, &BRT12, BP_INK);
    }
}

static void bp_delay(void)                      /* the echoes, smaller each time (FEEDBACK), apart by TIME */
{
    static const uint8_t HT[7] = {118, 86, 62, 45, 32, 23, 17};
    int32_t n = 1 + clamp(dl_fb, 0, 127) * 6 / 127, gap = 3 + clamp(dl_time, 0, 127) * 5 / 127, i, x = 117 - (n * 24 + (n - 1) * gap) / 2;
    char r[12];
    ui_time(r, DL_MS[clamp(dl_time, 0, 127)] << 16);
    bp_ground(1);
    bp_head("DELAY", r, BP_INK);
    for (i = 0; i < n; i++, x += 24 + gap) {
        if (BP_MIN) {                           /* (MINIMAL: a thin stem and a small cap on it, the first echo's cap amber) */
            bp_r(x + 11, 150 - HT[i], 2, HT[i], i ? UI_M : BP_INK);
            bp_r(x + 9, 150 - HT[i], 6, 2, i ? UI_M : ui_acc);
        } else
            bp_r(x, 150 - HT[i], 24, HT[i], i ? UI_M : BP_INK);
    }
}

static void bp_space(void)                      /* the reverb's tail, as long as SIZE (INF: it never falls) */
{
    int32_t v = clamp(rv_size, 0, 127), i, tl = 2 + v * 14 / 127;
    char r[12] = "SIZE ";
    ui_3(r + 5, ui_pc(v));
    bp_ground(1);
    bp_head("REVERB", v >= RV_INF ? "FREEZE" : r, v >= RV_INF ? ui_acc : BP_INK);
    for (i = 0; i < 26; i++) {
        int32_t h = v >= RV_INF ? 112 : (int32_t)((112u * pow2_q16(-(i * 272) / tl < -3000 ? -3000 : -(i * 272) / tl) + 32768u) >> 16);
        h += ((i * 7) % 3 - 1) * 2;
        h = h < 3 ? 3 : h > 112 ? 112 : h;
        if (BP_MIN) bp_r(13 + i * 8, 150 - h, 2, h, i < 3 ? BP_INK : UI_M);   /* (MINIMAL: thin stems) */
        else bp_r(12 + i * 8, 150 - h, 6, h, i < 3 ? BP_INK : UI_M);
    }
}

static void bp_master(void)                     /* the filter's curve: the cutoff where it bends (the red line), RESONANCE its peak there; open: flat */
{
    int32_t c = clamp(mf_cut, 0, 127), r = clamp(mf_res, 0, 127), ty = clamp(mf_type, 0, NFTYPE - 1), i;
    int open = ty == FT_LP ? c >= 127 : ty == FT_HP && c <= 0;
    int16_t q[2 * 53];
    bp_ground(1);
    bp_head(FT_NAME[ty], open ? "OPEN" : MP_NAME[clamp(mf_pos, 0, NMPOS - 1)], open ? ui_acc : UI_M);
    bp_r(12 + c * 210 / 127 - (BP_MIN ? 0 : 1), 30, BP_MIN ? 1 : 3, 122, ui_acc);   /* (the cutoff: MINIMAL's 1 px) */
    for (i = 0; i < 53; i++) {
        int32_t f = i * 127 / 52, d = ty == FT_HP ? c - f : f - c, h;
        if (ty == FT_BP) d = d < 0 ? -d : d;
        h = open || d <= 0 ? 112 : 112 - d * 3;   /* (an eighth of a cell: the line smooth) */
        if (!open && d > -5 && d < 5) h += r * 40 / 127 * (5 - (d < 0 ? -d : d)) / 5;
        h = h < 8 ? 8 : h > 144 ? 144 : h;
        q[2 * i] = (int16_t)(12 + i * 4), q[2 * i + 1] = (int16_t)(150 - h * 7 / 8);
    }
    bp_pl(q, 53, 5, BP_INK);
}

static void bp_drive(void)                      /* (V2) a sine through it (AMOUNT, TYPE; TONE left out), the clean one grey behind it; (V3.0) under it what is heard */
{
    uint32_t ty = (uint32_t)clamp(dr_type, 0, NDTYPE - 1);
    int32_t a = clamp(dr_amt, 0, 127), g = dr_gain(ty, a), lv = dr_level(ty, a), i, k;
    int16_t q[2 * 53], z[2 * 53];
    char r[8], m[28] = "DRIVEN:", *e = m + 7;
    ui_3(r, ui_pc(a));
    bp_ground(1);
    bp_head(DT_NAME[ty], a ? r : "OFF", a ? BP_INK : UI_M);
    bp_r(0, 88, 234, 1, BP_TRACK);
    for (i = 0; i < 53; i++) {
        int32_t x = sine_i((uint32_t)i * 165191050u) * 3 / 4, y = a ? (dr_shape(ty, (x * g) >> 8) * lv) >> 12 : x;
        q[2 * i] = z[2 * i] = (int16_t)(12 + i * 4);
        q[2 * i + 1] = (int16_t)(88 - x * 46 / 32768), z[2 * i + 1] = (int16_t)(88 - y * 46 / 32768);
    }
    bp_pl(q, 53, 3, UI_M);
    bp_pl(z, 53, 5, a ? BP_INK : UI_M);
    for (k = 0; k < (int32_t)NT; k++)
        if (trk[k].drv > 0 && trk[k].fx > 0) *e++ = ' ', *e++ = 'T', *e++ = (char)('1' + k), *e = 0;
    if (!a) put_s(m, "AMOUNT 0: NO DRIVE HEARD");
    else if (clamp(dr_mix, 0, 127) == 0) put_s(m, "MIX 0: NO DRIVE HEARD");
    else if (e == m + 7) put_s(m, "NO DRIVE SEND UP (MIXER 1)");
    bp_tt(10, 146, m, &BRT12, e > m + 7 && a && dr_mix > 0 ? BP_INK : UI_M);
}

/* ---- stage 2c: the sources' pictures (FM, SAMPLE, VOICE, WHEEL, PHYS), SCALE, CHORD, ARP, GRID, SYSTEM, SYSTEM 2 */
static void bp_algo(int32_t so, const char *r)  /* FM: the algorithm's operators as wired: a block each (a modulator above what it modulates, a line to each), the OUT bracket under the carriers (red),
                                                 * the operator `so` black with a red frame, the feedback a red hook on the operator it comes into */
{
    uint8_t tg[6];
    uint32_t car, o, j;
    int32_t fbin, fbout, x2[6], dep[6], sl, w, bw, h, pit, yb = 116, nr = 1, lo = 999, hi = -1, n = clamp(trk[sel].fm_alg, 0, 31) + 1;
    char t[10] = "ALGO ";
    sl = fm_layout((uint32_t)clamp(trk[sel].fm_alg, 0, 31), tg, &car, &fbin, &fbout, x2, dep);
    for (o = 0; o < 6u; o++) nr = dep[o] + 1 > nr ? dep[o] + 1 : nr;
    w = 210 / sl, bw = w - 6 < 40 ? w - 6 : 40, h = nr < 4 ? 22 : 20, pit = nr > 1 ? (yb - 36) / (nr - 1) : 0, pit = pit > 37 ? 37 : pit;
    t[5] = (char)('0' + n / 10), t[6] = (char)('0' + n % 10), t[7] = 0;
    bp_ground(1);
    bp_head(t, r, UI_M);
#define FMX(o) (12 + w / 2 + x2[o] * w / 2)
#define FMY(o) (yb - dep[o] * pit)
    for (o = 0; o < 6u; o++)                    /* the lines: down, across, down */
        for (j = 0; j < 6u; j++)
            if (tg[o] >> j & 1u) {
                int32_t xa = FMX(o), xb = FMX(j), ym = FMY(j) - 3, xl = xa < xb ? xa : xb;
                bp_r(xa - 1, FMY(o) + h, 3, ym - FMY(o) - h + 3, BP_INK);
                bp_r(xl - 1, ym, (xa > xb ? xa - xb : xb - xa) + 3, 3, BP_INK);
                bp_r(xb - 1, ym, 3, 4, BP_INK);
            }
    for (o = 0; o < 6u; o++) {
        int32_t x = FMX(o) - bw / 2, y = FMY(o), s = (int32_t)o == so;
        char d[2] = {(char)('1' + o), 0};
        if (car >> o & 1u) {
            lo = FMX(o) < lo ? FMX(o) : lo, hi = FMX(o) > hi ? FMX(o) : hi;
            bp_r(FMX(o) - 1, y + h, 3, 6, ui_acc);
        }
        bp_box(x, y, bw, h, BP_C3, s);
        bp_a(FMX(o) - bp_alen(&BRF16, d, 1) / 2, y + (h - bp_acap(&BRF16)) / 2, d, &BRF16, 1, s ? ui_acc : BP_INK);
        if ((int32_t)o == fbin) {               /* (the feedback: a hook on its right) */
            bp_r(x + bw, y + h / 2 - 1, 6, 3, ui_acc);
            bp_r(x + bw + 3, y - 5, 3, h / 2 + 5, ui_acc);
            bp_r(FMX(o), y - 5, x + bw + 6 - FMX(o), 3, ui_acc);
            bp_r(FMX(o) - 1, y - 5, 3, 5, ui_acc);
        }
    }
    bp_r(lo - 1, yb + h + 6, hi - lo + 3, 3, ui_acc);   /* OUT */
    bp_ttc(120, yb + h + 11, "OUT", &BRT12, UI_M);
#undef FMX
#undef FMY
}
static void bp_fmalgo(void)                     /* FM ALGO: the algorithm, the operator OP selects; the feedback */
{
    uint8_t tg[6];
    uint32_t car;
    int32_t fbin, fbout;
    char s[24], *e;
    fm_wiring((uint32_t)clamp(trk[sel].fm_alg, 0, 31), tg, &car, &fbin, &fbout);
    if (!trk[sel].fm_fb || fbin < 0) put_s(s, "NO FB");
    else {
        e = put_u(put_s(s, "FB "), (uint32_t)clamp(trk[sel].fm_fb, 0, 7));
        if (fbout >= 0 && fbout != fbin) e = put_s(put_u(put_s(e, " OP"), (uint32_t)fbout + 1u), ">OP");
        else e = put_s(e, " ON OP");
        put_u(e, (uint32_t)fbin + 1u);
    }
    bp_algo(clamp(fm_op, 0, 5), s);
}
static void bp_fmop(void)                       /* FM OP: the operator's envelope (ATTACK, DECAY; the rest the PATCH's), scaled by LEVEL, a red square at its peak; its ratio */
{
    const uint8_t *op = FM_PATCH[clamp(trk[sel].p[ENG_FM][0], 0, FM_NPATCH - 1)] + (5u - (uint32_t)clamp(fm_op, 0, 5)) * FP_OP;
    const volatile int32_t *f = trk[sel].fmo[clamp(fm_op, 0, 5)];
    uint8_t tg[6];
    uint32_t car;
    int32_t fbin, fbout, lv = clamp(f[FO_LEVEL], 0, 99), wa = 2 + clamp(f[FO_ATK], 0, 99) * 16 / 99, wd = 2 + clamp(f[FO_DEC], 0, 99) * 14 / 99;
    int32_t wr = 2 + (99 - op[FP_R1 + 3] % 100) * 12 / 99, ws = 53 - wa - wd - wr;
    int32_t y1 = clamp(28 - op[FP_L1] % 100 * 25 * lv / 9801, 3, 28), y2 = clamp(28 - op[FP_L1 + 2] % 100 * 25 * lv / 9801, 3, 28);
    int32_t y4 = clamp(28 - op[FP_L1 + 3] % 100 * 25 * lv / 9801, 3, 28);
    int16_t q[10];
    char s[20] = "OP ", r[20];
    fm_wiring((uint32_t)clamp(trk[sel].fm_alg, 0, 31), tg, &car, &fbin, &fbout);
    put_u(s + 3, (uint32_t)clamp(fm_op, 0, 5) + 1u);
    fm_ratio_text(put_s(r, "RATIO "), (uint32_t)clamp(fm_op, 0, 5));
    bp_ground(1);
    bp_head(s, r, BP_INK);
    bp_ttr(224, 25, car >> clamp(fm_op, 0, 5) & 1u ? "CARRIER" : "MODULATOR", &BRT12, UI_M);
#define EVY(e) (36 + ((e) - 3) * 112 / 25)
    q[0] = 10, q[1] = 148, q[2] = (int16_t)(10 + wa * 4), q[3] = (int16_t)EVY(y1), q[4] = (int16_t)(10 + (wa + wd) * 4), q[5] = (int16_t)EVY(y2);
    q[6] = (int16_t)(10 + (wa + wd + ws) * 4), q[7] = q[5], q[8] = 222, q[9] = (int16_t)EVY(y4);
    bp_pl(q, 5, 5, BP_INK);
    bp_r(10 + wa * 4 - 4, EVY(y1) - 4, 9, 9, ui_acc);
#undef EVY
}
static void bp_fm(void)                         /* FM (SOUND): the algorithm as wired, the PATCH it is from */
{
    bp_algo(-1, FM_PATCH_NAME[clamp(trk[sel].p[ENG_FM][0], 0, FM_NPATCH - 1)]);
}

static void bp_sample(void)                     /* the slot's sample (a bar a column, the loop's in black), the loop's ends in red with START and END */
{
    const volatile int32_t *p = trk[sel].p[ENG_SAMPLE];
    uint32_t slot = sp_slot(p);
    sp_loop_t L;
    char s[32], r[12];
    int32_t x, x0, x1;
    put_u(put_s(s, "SLOT "), slot + 1u);
    bp_ground(1);
    if (!smp[slot].ready) {
        bp_head(s, smp[slot].state == SS_SCAN ? "CHECKING" : "EMPTY", UI_M);
        bp_ttc(117, 75, smp[slot].state == SS_SCAN ? "CHECKING" : "EMPTY", &BRT16, UI_M);
        return;
    }
    if (smp[slot].name[0]) put_s(put_s(s + ui_len(s), " / "), smp[slot].name);
    sp_loop(p, &L);
    x = (int32_t)(L.len * 100u / 22050u);
    put_s(put_s(put_x10(r, (uint32_t)(x + 5) / 10u), " S"), L.rev ? " REV" : "");
    bp_head(s, r, UI_M);
    smp_peaks(slot);
    x0 = (int32_t)((uint64_t)L.l0 * 54u / smp[slot].n);
    x1 = (int32_t)((uint64_t)(L.l0 + L.len) * 54u / smp[slot].n);
    x1 = x1 > 53 ? 53 : x1;
    for (x = 0; x < 54; x++) {
        int32_t a = (smp_pk[slot][x] ? smp_pk[slot][x] : 1) * 4;
        bp_r(12 + x * 4, 94 - a, 3, 2 * a + 1, x >= x0 && x <= x1 ? BP_INK : UI_M);
    }
    bp_r(12 + x0 * 4 - 2, 30, 3, 122, ui_acc);
    bp_r(12 + x1 * 4 + 2, 30, 3, 122, ui_acc);
    bp_tt(12 + x0 * 4 + 3, 33, "START", &BRT12, ui_acc);
    bp_ttr(12 + x1 * 4 + 1, 33, "END", &BRT12, ui_acc);
}

static void bp_voice(void)                      /* the vowel (the nearest of A E I O U) big, the five as buttons (the one sung black with a red frame), the formants' SHIFT */
{
    static const char *const VW[5] = {"A", "E", "I", "O", "U"};
    const volatile int32_t *p = trk[sel].p[ENG_VOICE];
    int32_t v = (clamp(p[0], 0, 127) * 4 + 63) / 127, i;
    char t[16], s[24];
    bp_ground(1);
    bp_a(86 - bp_alen(&BRF24, VW[v], 5) / 2, 20, VW[v], &BRF24, 5, BP_INK);
    for (i = 0; i < 5; i++) {
        bp_box(165, 9 + i * 29, 59, 25, BP_TILE((uint32_t)i), i == v);
        bp_a(194 - bp_alen(&BRF16, VW[i], 1) / 2, 9 + i * 29 + (25 - bp_acap(&BRF16)) / 2, VW[i], &BRF16, 1, i == v ? ui_acc : BP_INK);
    }
    formant_text(p, 0, t);
    put_s(put_s(s, "VOWEL "), t);
    bp_tt(10, 128, s, &BRT12, BP_INK);
    formant_text(p, 1, t);
    put_s(put_s(s, "SHIFT "), t);
    bp_tt(10, 142, s, &BRT12, UI_M);
}

static void bp_wheel(void)                      /* the nine drawbars of the registration (the footages' colours: brown, white, black; PERC: its bar's end in red; the 1' bar off), the levels under them */
{
    static const char FOOT[9] = {'M', 'M', 'L', 'L', 'D', 'L', 'D', 'D', 'L'};
    const volatile int32_t *p = trk[sel].p[ENG_WHEEL];
    uint32_t reg = (uint32_t)clamp(p[0], 0, 15), perc = (uint32_t)clamp(p[1], 0, 6), k;
    int32_t pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : -1;
    bp_ground(1);
    for (k = 0; k < 9u; k++) {
        int32_t lv = DRW_REG[reg][k] - '0', x = 12 + (int32_t)k * 24;
        char d[2];
        if (perc && k == 8u) lv = 0;
        bp_r(x, 6, 20, 14 + lv * 12, FOOT[k] == 'M' ? UI_M : FOOT[k] == 'L' ? BP_ON : BP_INK);
        bp_r(x, 6 + lv * 12, 20, 14, (int32_t)k == pk ? ui_acc : BP_INK);
        d[0] = (char)('0' + lv), d[1] = 0;
        bp_ttc(x + 10, 126, d, &BRT14, BP_INK);
    }
    bp_tt(10, 146, N_DRW_REG[reg], &BRT12, BP_INK);
    bp_ttr(224, 146, perc ? N_DRW_PERC[perc] : "PERC OFF", &BRT12, perc ? ui_acc : UI_M);
}

static void bp_ell(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint16_t c)   /* a filled ellipse */
{
    int32_t dy;
    for (dy = -ry; dy <= ry; dy++) {
        int32_t xh = rx * br_isqrt((ry * ry - dy * dy) * 256) / (ry * 16);
        bp_r(cx - xh, cy + dy, 2 * xh + 1, 1, c);
    }
}
static void bp_phys(void)                       /* the model's body: a string (and the sympathetic ones), a bar on two cords, a drum head; the red block where it is struck or bowed */
{
    const volatile int32_t *p = trk[sel].p[ENG_PHYS];
    uint32_t md = phys_model(p);
    int32_t i, st = clamp(p[1], 0, 127), bw = clamp(p[3], 0, 127);
    int16_t q[2 * 15];
    char r[16];
    phys_text(p, 3, r);
    bp_ground(1);
    bp_head(N_PHYS_MODEL[md], r, UI_M);
    if (md == PM_MODAL) {                       /* a bar on two cords (its nodes), struck or bowed at its end */
        bp_r(12, 80, 210, 14, BP_INK);
        bp_r(60, 94, 3, 24, BP_INK), bp_r(171, 94, 3, 24, BP_INK);
        for (i = 0; i < 3; i++) bp_r(40 + i * (26 + st / 6), 66, 5, 10, UI_M);   /* (STRUCTURE spreads the partials) */
        bp_r(214, bw ? 64 : 100, 12, 12, ui_acc);
    } else if (md == PM_MEMB) {                 /* a drum head and its rings */
        bp_ell(117, 92, 92, 50, BP_INK);
        bp_ell(117, 92, 85, 43, BP_C3);
        bp_ell(117, 92, 56, 29, UI_M);
        bp_ell(117, 92, 51, 25, BP_C3);
        bp_ell(117, 92, 9, 5, BP_INK);
        bp_r(117 + st * 70 / 127 - 5, 87, 11, 11, ui_acc);
    } else {                                    /* a string between two posts, its swing; SYMPATH: two more under it */
        bp_r(12, 60, 14, 56, BP_INK), bp_r(208, 60, 14, 56, BP_INK);
        if (md == PM_SYMP) {
            bp_r(26, 104, 182, 3, UI_M);
            bp_r(26, 112, 182, 3, UI_M);
        }
        for (i = 0; i < 15; i++) {
            int32_t a = (sine_i((uint32_t)i * 153391689u) * (3 + bw / 8)) >> 15;   /* (half a sine over 15 points) */
            q[2 * i] = (int16_t)(26 + i * 13), q[2 * i + 1] = (int16_t)(88 - (a < 0 ? -a : a));
        }
        bp_pl(q, 15, 5, BP_INK);
        bp_r(26 + st * 170 / 127, 60, 14, 56, ui_acc);
    }
}

static void bp_sel(void)                        /* SCALE, CHORD: the keys from the octave's F (27 of them; the white ones as tiles, the black ones over them), a square on each key in the scale (black; white on a black
                                                 * key) and in the chord of the note last played (red), the chord's name and its notes under */
{
    static const int8_t WI[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};
    uint32_t base = KEY0 + (uint32_t)(12 * ui_oct), r, n, i, k, lit = 0;
    uint8_t ns[CH_MAX];
    char sc[24], cn[24], rg[12], in[24], *e;
    r = ch_last >= (int32_t)base && ch_last < (int32_t)base + 27 ? (uint32_t)ch_last : base + (uint32_t)(clamp(sc_root, 0, 11) + 12 - (int32_t)(base % 12u)) % 12u;
    n = chord_notes(sel, r, ns);
    for (i = 0; i < n; i++)
        if (ns[i] >= base && ns[i] < base + 27u) lit |= 1u << (ns[i] - base);
    e = put_s(put_s(sc, SC_NOTE[clamp(sc_root, 0, 11)]), " ");
    put_s(e, SC_NAME[clamp(sc_scale, 0, NSCALE - 1)]);
    ui_note(put_s(ui_note(rg, base), "-"), base + 26u);
    chord_name(sel, r, cn);
    if (!trk[sel].chord) put_s(in, "CHORD OFF: HOLD SEL");
    else {
        for (i = 0, k = 1; i < n; i++) k &= (uint32_t)sc_in(ns[i]);
        put_s(put_s(put_u(in, n), n == 1u ? " NOTE" : " NOTES"), k && sc_scale > 0 ? " IN KEY" : "");
    }
    bp_ground(1);
    if (page == PG_CHORD) bp_head(cn, rg, UI_M);
    else bp_head(sc, rg, UI_M);
    for (k = 0; k < 27u; k++)                   /* the white keys, then the black ones over them */
        if (WI[k % 12u] >= 0) {
            int32_t ix = (int32_t)(k / 12u) * 7 + WI[k % 12u], x = 13 + ix * 13;
            bp_r(x, 30, 12, 80, ix & 1 ? BP_C2 : BP_C3);
            if (lit >> k & 1u) bp_r(x + 1, 94, 10, 10, ui_acc);
            else if (sc_in((int32_t)(base + k))) bp_r(x + 1, 94, 10, 10, BP_INK);
        }
    for (k = 0; k < 27u; k++)
        if (WI[k % 12u] < 0) {
            int32_t x = 13 + ((int32_t)(k / 12u) * 7 + WI[(k - 1u) % 12u] + 1) * 13 - 5;
            bp_r(x, 30, 8, 48, BP_INK);
            if (lit >> k & 1u) bp_r(x + 2, 64, 4, 4, ui_acc);
            else if (sc_in((int32_t)(base + k))) bp_r(x + 2, 64, 4, 4, BP_ON);
        }
    bp_val(12, 140, page == PG_CHORD ? sc : cn, 110, page == PG_CHORD ? BP_INK : trk[sel].chord ? ui_acc : UI_M, 0);
    bp_ttr(224, 131, in, &BRT12, UI_M);
}

static void bp_arp(void)                        /* what the arpeggiator holds (OCTAVES), its steps (13 at most) as bars, their height the note, the step playing red */
{
    const arp_t *A = &arp[sel];
    uint8_t p[ARP_PN];
    uint32_t oct = (uint32_t)clamp(trk[sel].arp_oct, 1, 4), n, i, k, ids = 0, un = 0, lo = 127, hi = 0, from = 0, cnt;
    int32_t now = A->last, chord = clamp(trk[sel].arp_mode, 0, NARPMODE - 1) == ARP_CHORD;
    char l[48], *e;
    n = arp_pattern(sel, p, oct);
    cnt = A->n < ARP_N ? A->n : ARP_N;
    for (i = 0; i < cnt; i++) {
        for (k = 0; k < i && A->id[k] != A->id[i]; k++)
            ;
        ids += k == i;
        for (k = 0; k < i && A->note[k] != A->note[i]; k++)
            ;
        un += k == i;
    }
    e = put_s(l, "HELD: ");
    if (!trk[sel].arp) e = put_s(e, "ARP OFF");
    else if (!ids) e = put_s(e, "-");
    else if (ids == 1u) e = trk[sel].chord ? chord_name(sel, A->root[0], e) : ui_note(e, A->root[0]);
    else e = put_s(put_u(e, un), " NOTES");
    put_s(put_u(put_s(e, " / "), oct), oct == 1u ? " OCTAVE" : " OCTAVES");
    bp_ground(1);
    bp_head(ARP_MODE_NAME[clamp(trk[sel].arp_mode, 0, NARPMODE - 1)], l, UI_M);
    if (!trk[sel].arp || !n) {
        bp_ttc(117, 75, trk[sel].arp ? "PLAY KEYS" : "HOLD ARP: ON", &BRT16, UI_M);
        return;
    }
    if (chord) n = oct;
    for (i = 0; i < n; i++) {
        uint32_t v = chord ? i : p[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (now >= (int32_t)n) now = -1;
    if (n > 13u && now > 6) from = (uint32_t)now - 6u < n - 13u ? (uint32_t)now - 6u : n - 13u;
    for (i = 0; i < 13u && from + i < n; i++) {
        uint32_t v = chord ? from + i : p[from + i], lv = hi > lo ? (v - lo) * 7u / (hi - lo) : 3u, h = 24u + lv * 12u;
        bp_r(12 + (int32_t)i * 16, 150 - (int32_t)h, 13, (int32_t)h, (int32_t)(from + i) == now ? ui_acc : BP_INK);
    }
}

static void bp_row(int32_t y, int32_t h, int32_t i, const char *l, const char *v, uint16_t vc)   /* a row of SYSTEM: its name small at the left, its setting big at the right */
{
    bp_box(0, y, 234, h, BP_TILE((uint32_t)i), 0);
    bp_tt(8, y + (h - BRT12.cap) / 2, l, &BRT12, BP_INK);
    if (bp_alen(&BRF16, v, 1) <= 150) bp_a(226 - bp_alen(&BRF16, v, 1), y + (h - bp_acap(&BRF16)) / 2, v, &BRF16, 1, vc);
    else bp_ttr(226, y + (h - BRT14.cap) / 2, v, &BRT14, vc);
}
static void bp_system(void)                     /* COLOUR (the four, the one set underlined in black), then MIDI CHANNEL, TUNE, SCREEN OFF, MIDI LEARN, SEQ MIDI OUT as rows */
{
    int32_t i;
    char v[24];
    bp_ground(1);
    for (i = 0; i < 4; i++) {
        int32_t x = 3 + i * 58;
        bp_r(x, 4, 54, 36, UI_ACCENTS[i]);
        bp_tt(x + 5, 8, COL_NAME[i], &BRT12, BP_ON);
        if (i == (int32_t)set.col) bp_r(x, 42, 54, 4, BP_INK);
    }
    mch_text(v, set.mch);
    bp_row(50, 20, 0, "MIDI CHANNEL", v, BP_INK);
    tune_text(v, set.tune);
    bp_row(72, 20, 1, "TUNE", v, BP_INK);
    bp_row(94, 20, 0, "SCREEN OFF", SCR_NAME[set.scr % 5u], BP_INK);
    put_s(put_u(v, set.nlk), set.nlk == 1u ? " LINK" : " LINKS");
    bp_row(116, 20, 1, "MIDI LEARN", v, BP_INK);
    bp_row(138, 20, 0, "SEQ MIDI OUT", set.out ? "ON" : "OFF", set.out ? ui_acc : BP_INK);
}

static void bp_sys2_region(void)                /* SYSTEM 2's rows under the links (the voices, USB's drops, the CPU): redrawn when they change */
{
    ui_region(BP_X, BP_Y + 84, 234, 76, UI_K, ui_th->pic);
}
static void bp_sys2(void)                       /* MIDI LEARN's links (four, "+n MORE"), then the VOICES in use, USB REC DROPS, CPU NOW and PEAK (their bars; red over 60 %) */
{
    int32_t i, n = set.nlk, j;
    char l[24], v[24];
    put_s(put_u(v, (uint32_t)n), "/32");
    bp_ground(1);
    bp_head("MIDI LEARN", v, UI_M);
    if (!n) bp_ttc(117, 50, "NO LINKS", &BRT14, UI_M);
    for (i = 0; i < 4 && i < n; i++) {
        const uint8_t *L = set.lk[n - 1 - i];
        int32_t y = 24 + i * 15;
        bp_box(0, y, 234, 13, BP_TILE((uint32_t)i), 0);
        if (i == 3 && n > 4) {
            put_s(put_u(put_s(v, "+"), (uint32_t)(n - 3)), " MORE");
            bp_tt(8, y + 2, v, &BRT12, UI_M);
            break;
        }
        put_u(put_s(l, "CC "), L[0]);
        bp_tt(8, y + 2, l, &BRT12, BP_INK);
        link_name(v, L);
        bp_ttr(226, y + 2, v, &BRT12, BP_INK);
    }
    bp_box(0, 86, 234, 15, BP_C3, 0);           /* the voices: six segments */
    bp_tt(8, 90, "VOICES", &BRT12, BP_INK);
    for (j = 0; j < (int32_t)NV; j++) bp_r(226 - ((int32_t)NV - j) * 14, 90, 12, 8, j < (int32_t)voices_used() ? BP_INK : BP_TRACK);
    bp_box(0, 103, 234, 15, BP_C3, 0);
    bp_tt(8, 107, "USB REC DROPS", &BRT12, BP_INK);
    put_u(l, sys2_drops());
    bp_ttr(226, 107, l, &BRT12, sys2_drops() ? ui_acc : BP_INK);
    for (i = 0; i < 2; i++) {
        uint32_t pc = pct10(i ? cpu_peak : cpu_now);
        int32_t y = 120 + i * 19;
        char s[16];
        bp_box(0, y, 234, 17, BP_C3, 0);
        bp_tt(8, y + 2, i ? "CPU PEAK" : "CPU NOW", &BRT12, BP_INK);
        put_s(put_x10(s, pc), "%");
        bp_ttr(226, y + 2, s, &BRT12, BP_INK);
        bp_r(8, y + 12, 218, 3, BP_TRACK);
        bp_r(8, y + 12, (int32_t)(218u * (pc > 1000u ? 1000u : pc) / 1000u), 3, pc > 600u ? ui_acc : BP_INK);
    }
}

#ifdef DINGHY_HOST
static void br_prim_demo(void)                  /* (tests/brutal_test.py: a triangle, a line, a V, a slant, each of a known size) */
{
    static const int32_t TRI[6] = {20 * 16, 150 * 16, 80 * 16, 150 * 16, 50 * 16, 100 * 16};   /* base 60, height 50: 1500 pixels */
    static const int16_t HOR[4] = {100, 40, 200, 40}, VEE[6] = {110, 90, 140, 60, 170, 90}, SLA[4] = {150, 120, 210, 160};
    br_poly(TRI, 3, BP_ON);
    br_polyline(HOR, 2, 5, BP_ON);               /* rows 37 .. 41, columns 97 .. 201 */
    br_polyline(VEE, 3, 6, BP_ON);               /* a point 3 sqrt(2) above (140, 60) */
    br_polyline(SLA, 2, 4, BP_ON);
}
#endif

/* ---- which pages are BRUTAL's own, and the dispatch */
static int br_native(void)
{
#ifdef DINGHY_HOST
    if (host_prim) return 1;
#endif
    switch (page) {
    case PG_HOME: case PG_SOURCE: case PG_PERFORM: case PG_MIX1: case PG_MIX2: case PG_LEVELS: case PG_SOUNDS: case PG_PROJECTS: case PG_PATTERN: case PG_SONG: case PG_SYS3:
    case PG_ENV: case PG_LFO: case PG_PDLINE: case PG_PDENV: case PG_TAPE: case PG_GRAIN: case PG_DELAY: case PG_SPACE: case PG_MASTER: case PG_DRIVE:
        return 1;
    case PG_SOUND: case PG_FMALGO: case PG_FMOP: case PG_SCALE: case PG_CHORD: case PG_ARP: case PG_SYSTEM: case PG_SYS2:
        return 1;                               /* (all but TRACK, a page there is no longer) */
    default:
        return 0;
    }
}
static void br_pic(void)
{
#ifdef DINGHY_HOST
    if (host_prim) { br_prim_demo(); return; }
#endif
    switch (page) {
    case PG_HOME: br_home(); return;
    case PG_SOURCE: bp_source(); return;
    case PG_PERFORM: bp_perform(); return;
    case PG_MIX1: bp_mix1(); return;
    case PG_MIX2: bp_mix2(); return;
    case PG_LEVELS: bp_levels(); return;
    case PG_SOUNDS: bp_sounds(); return;
    case PG_PROJECTS: bp_projects(); return;
    case PG_PATTERN: bp_pattern(); return;
    case PG_SONG: bp_song(); return;
    case PG_SYS3: bp_sys3(); return;
    case PG_ENV: bp_env(); return;
    case PG_LFO: bp_lfo(); return;
    case PG_PDLINE: bp_pdline(); return;
    case PG_PDENV: bp_pdenv(); return;
    case PG_TAPE: bp_tape(); return;
    case PG_GRAIN: bp_grain(); return;
    case PG_DELAY: bp_delay(); return;
    case PG_SPACE: bp_space(); return;
    case PG_MASTER: bp_master(); return;
    case PG_DRIVE: bp_drive(); return;
    case PG_FMALGO: bp_fmalgo(); return;
    case PG_FMOP: bp_fmop(); return;
    case PG_SCALE: case PG_CHORD: bp_sel(); return;
    case PG_ARP: bp_arp(); return;
    case PG_SYSTEM: bp_system(); return;
    case PG_SYS2: bp_sys2(); return;
    case PG_SOUND:
        switch (sel_eng()) {
        case ENG_VA: bp_va(); return;
        case ENG_FM: bp_fm(); return;
        case ENG_SAMPLE: bp_sample(); return;
        case ENG_VOICE: bp_voice(); return;
        case ENG_WHEEL: bp_wheel(); return;
        case ENG_PHYS: bp_phys(); return;
        default: bp_phase(); return;
        }
    default: ui_pic_draw();
    }
}

/* ---- MINIMAL: HOME is its own (minimal.c); the pages whose picture is BRUTAL's shared one in MINIMAL's parameters (stage by stage); MIXER 1 and SEQ GRID keep EYE's layout, EYE's code in MINIMAL's colours;
 * the rest EYE's pictures in MINIMAL's colours for now */
static int bp_min_page(void)
{
    switch (page) {
    case PG_SOURCE: case PG_PERFORM: case PG_MIX2: case PG_LEVELS: case PG_SOUNDS: case PG_PROJECTS: case PG_PATTERN: case PG_SONG: case PG_SYS3:
    case PG_ENV: case PG_LFO: case PG_PDLINE: case PG_PDENV: case PG_TAPE: case PG_GRAIN: case PG_DELAY: case PG_SPACE: case PG_MASTER: case PG_DRIVE:
        return 1;
    case PG_SOUND:                              /* (VA and PHASE so far) */
        return sel_eng() == ENG_VA || sel_eng() == ENG_PHASE;
    default:
        return 0;
    }
}
static int mn_native(void) { return page == PG_HOME || bp_min_page(); }
static void mn_pic(void)
{
    if (page == PG_HOME) mn_home();
    else if (bp_min_page()) br_pic();
    else ui_pic_draw();
}
