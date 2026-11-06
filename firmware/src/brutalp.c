/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V3.0: BRUTAL's pictures: every page's, drawn as docs/themes/brutal-mockup.html draws them (there at 2x: here halved), from the live values. Included by ui.c
 * after EYE's pictures (before the themes' table). A picture is drawn in a region at the screen's coordinates, relative to the picture's corner (BP_X, BP_Y, 234 x 160), a
 * ground of black (the dark pages) or concrete with its faint lines (the light pages); small text and captions in Terminus Bold, big text and numbers in Archivo Black
 * (16 and 24 px, or those scaled by whole pixels), what does not fit big in Terminus. Nothing here uses the EYE pixfont. Only what moves is redrawn (the meters, the LFO's
 * dot: their own regions), no buffer of its own. */

#define BP_X 3                                  /* the picture's corner on the screen, */
#define BP_Y 32                                 /* its size: 234 x 160 */
#define BP_TILE(i) ((i) & 1 ? BR_C2 : BR_C1)    /* the tiles' two concretes, alternately */

static void bp_r(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { ui_rect(BP_X + x, BP_Y + y, w, h, c); }
static void bp_ground(int light)                /* the picture's ground: the light pages' concrete with its faint lines (every 11 px, as the mockup's) */
{
    int32_t y;
    if (!light) return;
    bp_r(0, 0, 234, 160, BR_C1);
    for (y = 10; y < 160; y += 11) bp_r(0, y, 234, 1, BR_C2);
}
static void bp_box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t f, int sel)   /* a tile: its colour, or black with a red frame when it is the selected one */
{
    if (sel) {
        bp_r(x, y, w, h, UI_K);
        bp_r(x, y, w, 2, ui_acc), bp_r(x, y + h - 2, w, 2, ui_acc), bp_r(x, y, 2, h, ui_acc), bp_r(x + w - 2, y, 2, h, ui_acc);
    } else
        bp_r(x, y, w, h, f);
}
static void bp_bar(int32_t x, int32_t y, int32_t w, int32_t h, int32_t num, int32_t den, int sel)   /* a bar num / den full: black on the concrete's track (white on black's) */
{
    bp_r(x, y, w, h, sel ? BR_SELBG : BR_TRACK);
    if (num > 0) bp_r(x, y, den > 0 && num < den ? w * num / den : w, h, sel ? UI_W : UI_K);
}
/* Terminus Bold at (x, y) = the capitals' ink top, left / right / centred on x */
static void bp_tt(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c) { br_tt(BP_X + x, BP_Y + y - f->capy, s, f, c); }
static void bp_ttr(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c) { bp_tt(x - br_tlen(f, s), y, s, f, c); }
static void bp_ttc(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c) { bp_tt(x - br_tlen(f, s) / 2, y, s, f, c); }
/* Archivo Black f (16 or 24 px) scaled sc, at (x, y) = the capitals' ink top */
static void bp_a(int32_t x, int32_t y, const char *s, const br_font_t *f, int32_t sc, uint16_t c) { br_text(BP_X + x, BP_Y + y - br_capy(f) * sc, s, f, sc, c); }
/* a value: as big as it fits in `avail` px (Archivo 24, 16, then Terminus 16, 14, 12), its capitals' ink ending at yb; a 0 left of x, 1 right of x, 2 centred on x */
static void bp_val(int32_t x, int32_t yb, const char *s, int32_t avail, uint16_t c, int al)
{
    const br_font_t *f = br_len(&BRF24, s, 1) <= avail ? &BRF24 : br_len(&BRF16, s, 1) <= avail ? &BRF16 : 0;
    const br_tfont_t *t = br_tlen(&BRT16, s) <= avail ? &BRT16 : br_tlen(&BRT14, s) <= avail ? &BRT14 : &BRT12;
    int32_t w = f ? br_len(f, s, 1) : br_tlen(t, s);
    x = al == 1 ? x - w : al == 2 ? x - w / 2 : x;
    if (f) bp_a(x, yb - br_cap(f), s, f, 1, c);
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
        bp_val(x + 28, y + 33, SRC_CODE[i], 52, i == e ? ui_acc : UI_K, 2);
    }
    bp_box(177, 54, 57, 51, trk[sel].latch ? ui_acc : BR_C2, 0);
    bp_caps(183, 60, "LATCH", trk[sel].latch ? UI_W : UI_K);
    bp_val(183, 98, trk[sel].latch ? "ON" : "OFF", 46, trk[sel].latch ? UI_W : UI_K, 0);
    bp_r(0, 109, 234, 51, BR_C3);
    if (e == ENG_VOICE) {                       /* (VOICE's TYPE: one singer, or a choir a note) */
        bp_caps(10, 119, "TYPE", UI_K);
        bp_val(10, 148, VT_NAME[clamp(trk[sel].vty, 0, NVT - 1)], 120, trk[sel].vty ? ui_acc : UI_K, 0);
    }
    bp_caps(224 - 6 * 5, 119, "GLIDE", UI_K);
    if (trk[sel].glide) ui_time(g, 10u * pow2_q16(clamp(trk[sel].glide, 0, 127) * 12)); else put_s(g, "OFF");
    bp_ttr(224, 134, g, &BRT14, UI_K);
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
        bp_tt(x + 6, y + 6, KEY[i], &BRT14, on ? ui_acc : UI_K);
        bp_tt(x + 6, y + 28, NM[i][0], &BRT12, on ? UI_W : UI_K);
        bp_tt(x + 6, y + 38, NM[i][1], &BRT12, on ? UI_W : UI_K);
    }
    bp_r(0, 109, 234, 51, BR_C3);
    bp_caps(8, 114, "GENERATE", UI_K);
    for (i = 0; i < (int32_t)NT; i++) {
        int32_t x = 8 + i * 56, y = 126;
        int on = (int)(gen_on >> i & 1u);
        char r[12], m[4];
        bp_r(x, y, 50, 20, on ? ui_acc : BR_C2);
        put_u(put_s(r, "T"), (uint32_t)i + 1u);
        bp_tt(x + 4, y + 6, r, &BRT12, on ? UI_W : UI_K);
        m[0] = GM_NAME[gen_md[i] % NGM][0], m[1] = GM_NAME[gen_md[i] % NGM][1], m[2] = GM_NAME[gen_md[i] % NGM][2], m[3] = 0;
        bp_tt(x + 22, y + 6, m, &BRT12, on ? UI_W : UI_K);
        if (gen_fl >> i & 1u) bp_r(x + 42, y + 3, 5, 5, on ? UI_W : UI_K);
        ui_note(put_s(ui_note(r, gen_low((uint32_t)i)), "-"), gen_high((uint32_t)i));
        bp_tt(x, y + 26, r, &BRT12, UI_K);
    }
}

/* ---- MIXER 1: six tiles over the whole page (3 x 2, the selected track's: LEVEL, DELAY, REVERB, TAPE, GRAIN, DRIVE: a name, the value, a bar; the one SELECT picks black with
 * a red frame); LEVEL's bar is the live meter (8 segments, the theme's colours) over the bar of the level set */
#define BM_X(i) (3 + (int32_t)((i) % 3u) * 79)  /* a tile's corner: the picture's whole page, from row 29 to 236 */
#define BM_Y(i) (29 + (int32_t)((i) / 3u) * 105)
#define BM_W 76
#define BM_H 102
static void bp_mix1(void)
{
    static const char *const LAB[MX_N] = {"LEVEL", "DELAY", "REVERB", "TAPE", "GRAIN", "DRIVE"};
    uint32_t i;
    int32_t j;
    for (i = 0; i < MX_N; i++) {
        int32_t x = BM_X(i), y = BM_Y(i), v = clamp(*mx_tile(sel, i), 0, 127), on = (int32_t)i == mx_sq;
        int dry = i != MX_LEVEL && trk[sel].fx <= 0;   /* (the sends do nothing: FX 0 on HOME: dim, "FX0") */
        char s[6];
        ui_rect(x, y, BM_W, BM_H, UI_K);
        if (on) ui_rect(x, y, BM_W, 2, ui_acc), ui_rect(x, y + BM_H - 2, BM_W, 2, ui_acc), ui_rect(x, y, 2, BM_H, ui_acc), ui_rect(x + BM_W - 2, y, 2, BM_H, ui_acc);
        else ui_rect(x, y, BM_W, BM_H, BP_TILE(i));
        br_tt(x + 6, y + 6 - BRT12.capy, LAB[i], &BRT12, on ? BR_SELLAB : UI_K);
        ui_3(s, ui_pc(v));
        if (dry) put_s(s, "FX0");
        br_text(x + 6, y + 24 - br_capy(&BRF24), s, &BRF24, 1, dry ? ui_acc : on ? UI_W : UI_K);
        if (i == MX_LEVEL) {                    /* (the sound now: 8 segments; a notch over them where LEVEL is set) */
            for (j = 0; j < 8; j++) ui_rect(x + 6 + j * 8, y + 52, 7, 14, j < (int32_t)mx_h[sel] ? ui_meter_col(j) : on ? BR_SELBG : BR_TRACK);
            ui_rect(x + 6 + v * 62 / 127, y + 68, 3, 5, on ? UI_W : UI_K);
        }
        ui_rect(x + 6, y + BM_H - 14, BM_W - 12, 6, on ? BR_SELBG : BR_TRACK);
        if (v) ui_rect(x + 6, y + BM_H - 14, (BM_W - 12) * v / 127, 6, dry ? UI_M : on ? UI_W : UI_K);
    }
}
static void bp_meter_region(uint32_t t)         /* a meter moved: its region (MIXER 1: the selected track's LEVEL bar; LEVELS: that track's meter) */
{
    if (page == PG_MIX1) ui_region(BM_X(0) + 6, BM_Y(0) + 52, 64, 14, UI_K, ui_th->pic);
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
        bp_a(x + 6, 6, nm, &BRF16, 1, on ? ui_acc : UI_K);
        bp_r(x + 6, 36, 45, 6, on ? BR_SELBG : BR_TRACK);
        bp_r(x + 6 + (45 - 6) * p / 127, 33, 6, 12, on ? UI_W : UI_K);
        if (p == 64) put_s(v, "C"); else put_u(put_s(v, p < 64 ? "L" : "R"), (uint32_t)(p < 64 ? 64 - p : p - 64));
        bp_tt(x + 6, 50, v, &BRT12, on ? UI_W : UI_K);
        bp_val(x + 6, 118, FT_NAME[clamp(trk[t].ftype, 0, NFTYPE - 1)], 45, on ? UI_W : UI_K, 0);
        bp_tt(x + 6, 128, trk[t].fpos ? "POST" : "PRE", &BRT12, on ? UI_W : UI_K);
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
        bp_val(x + 6, 36, q ? "MUTE" : v, 45, q ? UI_M : UI_W, 0);
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
    bp_tt(8, y + 7, l, &BRT14, on ? UI_W : UI_K);
    bp_ttr(226, y + 7, r, &BRT14, on ? ui_acc : UI_K);
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
        bp_box(x, y, 115, 78, slot_used((uint32_t)i) ? BR_C1 : BR_C2, on);
        bp_a(x + 8, y + 10, nm, &BRF16, 3, on ? ui_acc : slot_used((uint32_t)i) ? UI_K : UI_M);
        bp_ttr(x + 107, y + 62, st, &BRT14, i == slot_cur ? ui_acc : on ? UI_W : UI_K);
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
        bp_a(x + 28 - br_len(&BRF24, nm, 1) / 2, 8, nm, &BRF24, 1, i == 0 ? ui_acc : UI_M);
        if (!i) {
            if (seq_on) put_s(v, "PLAY"); else put_s(put_u(v, (uint32_t)seq_steps(S)), " ST");
            bp_ttc(x + 28, 36, v, &BRT12, UI_W);
        } else
            bp_ttc(x + 28, 36, "EMPTY", &BRT12, UI_M);
    }
    bp_r(0, 53, 234, 107, BR_C3);
    put_u(v, (uint32_t)seq_steps(S));
    bp_a(8, 70, v, &BRF16, 4, UI_K);
    bp_ttr(226, 62, "STEPS", &BRT12, UI_K);
    bp_val(226, 100, SEQ_DIV_NAME[clamp(S->div, 0, 7)], 90, UI_K, 1);
    put_s(v, "SWING ");
    ui_3(v + 6, ui_pc(S->swing));
    bp_ttr(226, 122, v, &BRT12, UI_K);
    bp_ttr(226, 138, SEQ_MODE_NAME[clamp(S->mode, 0, SEQ_NMODE - 1)], &BRT12, UI_K);
}

/* ---- SONG: the chain's 16 steps, 8 a row (its slot's letter, its REPEATS, its number; the step playing or shown black with a red frame; END; the rest empty), the MODE under them */
static void bp_song(void)
{
    int32_t cur = song_cur(), len = (int32_t)song_length(), i;
    char b[8];
    for (i = 0; i < SONG_N; i++) {
        int32_t x = (i & 7) * 29, y = (i >> 3) * 52, on = i == cur, p = song_pat[i];
        if (i > len) { bp_box(x, y, 27, 50, UI_D, 0); continue; }
        bp_box(x, y, 27, 50, i == len ? BR_C3 : BP_TILE(i), on);
        put_u(b, (uint32_t)i + 1u);
        bp_tt(x + 4, y + 5, b, &BRT12, on ? UI_W : UI_M);
        if (i == len) { bp_ttc(x + 13, y + 28, "END", &BRT12, UI_K); continue; }
        b[0] = p >= 0 && p < SONG_END ? (char)('A' + p) : '-', b[1] = 0;
        bp_a(x + 13 - br_len(&BRF16, b, 1) / 2, y + 16, b, &BRF16, 1, on ? ui_acc : UI_K);
        if (i == song_at && song_rec != 2u) put_u(put_s(put_u(b, song_left), "/"), (uint32_t)clamp(song_rep[i], 1, 16));
        else put_u(put_s(b, "X"), (uint32_t)clamp(song_rep[i], 1, 16));
        bp_ttc(x + 13, y + 38, b, &BRT12, on ? UI_W : UI_K);
    }
    bp_r(0, 108, 234, 52, BR_C3);
    bp_val(8, 142, SONG_MODE_NAME[clamp(song_mode, 0, NSONGMODE - 1)], 130, UI_K, 0);
    put_u(put_s(b, "LEN "), (uint32_t)len);
    bp_ttr(226, 122, b, &BRT14, UI_K);
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
        bp_caps(10, i * 54 + 8, NM[i], UI_K);
        bp_val(10, i * 54 + 42, v, 214, UI_K, 0);
    }
}

/* ---- the curve pages (stage 2b): light ground, a title (Archivo 16) with a note at the right (Terminus), the curve a thick line (5 px; the second, behind it, 3 px in the
 * mid grey), from the live values; points in picture coordinates, a polyline of at most 53 */
static void bp_pl(int16_t *q, uint32_t n, int32_t w, uint16_t c)   /* a line through n points (in place: moved to the screen) */
{
    uint32_t i;
    for (i = 0; i < n; i++) q[2 * i] = (int16_t)(q[2 * i] + BP_X), q[2 * i + 1] = (int16_t)(q[2 * i + 1] + BP_Y);
    br_polyline(q, n, w, c);
}
static void bp_head(const char *l, const char *r, uint16_t rc)   /* the title, and a note at the right */
{
    int32_t av = r ? 214 - br_tlen(&BRT12, r) - 12 : 214;
    if (br_len(&BRF16, l, 1) <= av) bp_a(10, 8, l, &BRF16, 1, UI_K);
    else bp_tt(10, 9, l, &BRT14, UI_K);
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
    bp_head(w == WAVE_SUPER ? "SUPERSAW" : w == WAVE_SQR ? "SQUARE" : "SAW", 0, UI_K);
    bp_r(0, 146, 234, 1, BR_TRACK);
    if (w == WAVE_SUPER) bp_wave(0, 57, 33, 3, UI_M), bp_wave(0, 51, 15, 3, UI_M), bp_wave(0, 54, 0, 5, UI_K);
    else bp_wave(w == WAVE_SQR, 54 + dt, 9, 3, UI_M), bp_wave(w == WAVE_SQR, 54, 0, 5, UI_K);
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
    bp_r(0, 85, 234, 1, BR_TRACK);
    for (c = 0; c < 2; c++) {
        int32_t x0 = 12 + c * 110;
        pd_setup(&b, c && w2 ? w2 - 1u : w1, dcw);
        for (i = 0; i < 26; i++) q[2 * i] = (int16_t)(x0 + i * 4), q[2 * i + 1] = (int16_t)(85 - pd_wave(&b, (uint32_t)i * 65535u / 25u) * 50 / 32767);
        bp_pl(q, 26, 5, c ? UI_M : UI_K);
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
    bp_r(0, 80, 234, 1, BR_TRACK);
    for (c = 0; c < 2; c++) {
        pd_setup(&b, c && w2 ? (uint32_t)w2 - 1u : (uint32_t)clamp(p[0], 0, 7), pd_dcw(c && d2 ? d2 : p[2]));
        for (i = 0; i < 24; i++) q[2 * i] = (int16_t)(12 + c * 112 + i * 4), q[2 * i + 1] = (int16_t)(80 - pd_wave(&b, (uint32_t)i * 65535u / 23u) * 40 / 32767);
        bp_pl(q, 24, 5, c ? UI_M : UI_K);
        bp_tt(12 + c * 112, 128, c ? "LINE 2" : "LINE 1", &BRT12, c ? UI_M : UI_K);
    }
    bp_tt(12, 146, trk[sel].pd_noise ? "NOISE ON" : "NOISE OFF", &BRT12, trk[sel].pd_noise ? ui_acc : UI_M);
}

static void bp_pdenv(void)                      /* (V2) PHASE ENV: DCW (black: up from the floor) and PITCH (grey: up or down from the middle), each over ~1.2 s */
{
    int32_t c, i, j;
    int16_t q[2 * 53];
    bp_ground(1);
    bp_tt(10, 9, trk[sel].pd_de ? "DCW ON" : "DCW OFF", &BRT14, trk[sel].pd_de ? ui_acc : UI_K);
    bp_ttr(224, 9, trk[sel].pd_pe ? "PITCH ON" : "PITCH OFF", &BRT14, trk[sel].pd_pe ? ui_acc : UI_M);
    bp_r(0, 90, 234, 1, BR_TRACK);
    for (c = 0; c < 2; c++) {                   /* (the grey first: the black over it) */
        int32_t pt = 1 - c, d = pt ? clamp(trk[sel].pd_pe, -64, 63) : clamp(trk[sel].pd_de, 0, 127);
        uint32_t k = env_k(10u * pow2_q16(clamp(pt ? trk[sel].pd_pd : trk[sel].pd_dd, 0, 127) * 15)), l = 65536u;
        int32_t base = pt ? 90 : 148, span = pt ? clamp(55 * d / 24, -55, 55) : 105 * d / 127;
        for (i = 0; i < 53; i++) {
            q[2 * i] = (int16_t)(12 + i * 4), q[2 * i + 1] = (int16_t)(base - (int32_t)(((int64_t)span * l) >> 16));
            for (j = 0; j < 32; j++) l -= (uint32_t)mulq16((int32_t)l, k);
        }
        bp_pl(q, 53, pt ? 3 : 5, pt ? UI_M : UI_K);
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
    bp_pl(q, 5, 5, UI_K);
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
    bp_r(0, 80, 234, 1, BR_TRACK);
    for (x = 0; x < 54u; x++) q[2 * x] = (int16_t)(10u + x * 4u), q[2 * x + 1] = (int16_t)(80 - lfo_val(sh, x) * 55 / 32767);
    bp_pl(q, 54, 5, trk[sel].lfo_depth > 0 ? UI_K : UI_M);
    bp_r(10 + (int32_t)dx * 4 - 5, 80 - lfo_val(sh, dx) * 55 / 32767 - 5, 11, 11, ui_acc);
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
        bp_r(35 + r * 104, 20, 60, 60, UI_K);
        bp_r(55 + r * 104, 40, 20, 20, BR_C1);
        bp_r(65 + r * 104, 80, 4, 20, UI_K);
    }
    for (k = 0; k < 17; k++) q[2 * k] = (int16_t)(35 + k * 10), q[2 * k + 1] = (int16_t)(103 + ((sine_i((uint32_t)k * 804212224u) * amp + 16384) >> 15));
    bp_pl(q, 17, 5, UI_K);
    if (e > on + 3) bp_a(10, 134, on, &BRF16, 1, UI_K);
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
        bp_r(gx - sz / 2, gy, sz, sz, cl == 0 ? ui_acc : cl < 5 ? UI_K : UI_M);
    }
    bp_tt(10, 9, "BUFFER 1.25 S", &BRT12, UI_K);
    if (ge_freeze) {
        bp_r(176, 4, 54, 16, ui_acc);
        bp_ttc(203, 9, "FREEZE", &BRT12, UI_W);
    } else {
        put_s(put_u(r, (pow2_q16(clamp(ge_size, 0, 127) * 8) * 10u + 32768u) >> 16), " MS");
        bp_ttr(224, 9, r, &BRT12, UI_K);
    }
}

static void bp_delay(void)                      /* the echoes, smaller each time (FEEDBACK), apart by TIME */
{
    static const uint8_t HT[7] = {118, 86, 62, 45, 32, 23, 17};
    int32_t n = 1 + clamp(dl_fb, 0, 127) * 6 / 127, gap = 3 + clamp(dl_time, 0, 127) * 5 / 127, i, x = 117 - (n * 24 + (n - 1) * gap) / 2;
    char r[12];
    ui_time(r, DL_MS[clamp(dl_time, 0, 127)] << 16);
    bp_ground(1);
    bp_head("DELAY", r, UI_K);
    for (i = 0; i < n; i++, x += 24 + gap) bp_r(x, 150 - HT[i], 24, HT[i], i ? UI_M : UI_K);
}

static void bp_space(void)                      /* the reverb's tail, as long as SIZE (INF: it never falls) */
{
    int32_t v = clamp(rv_size, 0, 127), i, tl = 2 + v * 14 / 127;
    char r[12] = "SIZE ";
    ui_3(r + 5, ui_pc(v));
    bp_ground(1);
    bp_head("REVERB", v >= RV_INF ? "FREEZE" : r, v >= RV_INF ? ui_acc : UI_K);
    for (i = 0; i < 26; i++) {
        int32_t h = v >= RV_INF ? 112 : (int32_t)((112u * pow2_q16(-(i * 272) / tl < -3000 ? -3000 : -(i * 272) / tl) + 32768u) >> 16);
        h += ((i * 7) % 3 - 1) * 2;
        h = h < 3 ? 3 : h > 112 ? 112 : h;
        bp_r(12 + i * 8, 150 - h, 6, h, i < 3 ? UI_K : UI_M);
    }
}

static void bp_master(void)                     /* the filter's curve: the cutoff where it bends (the red line), RESONANCE its peak there; open: flat */
{
    int32_t c = clamp(mf_cut, 0, 127), r = clamp(mf_res, 0, 127), ty = clamp(mf_type, 0, NFTYPE - 1), i;
    int open = ty == FT_LP ? c >= 127 : ty == FT_HP && c <= 0;
    int16_t q[2 * 53];
    bp_ground(1);
    bp_head(FT_NAME[ty], open ? "OPEN" : MP_NAME[clamp(mf_pos, 0, NMPOS - 1)], open ? ui_acc : UI_M);
    bp_r(12 + c * 210 / 127 - 1, 30, 3, 122, ui_acc);
    for (i = 0; i < 53; i++) {
        int32_t f = i * 127 / 52, d = ty == FT_HP ? c - f : f - c, h;
        if (ty == FT_BP) d = d < 0 ? -d : d;
        h = open || d <= 0 ? 112 : 112 - d * 3;   /* (an eighth of a cell: the line smooth) */
        if (!open && d > -5 && d < 5) h += r * 40 / 127 * (5 - (d < 0 ? -d : d)) / 5;
        h = h < 8 ? 8 : h > 144 ? 144 : h;
        q[2 * i] = (int16_t)(12 + i * 4), q[2 * i + 1] = (int16_t)(150 - h * 7 / 8);
    }
    bp_pl(q, 53, 5, UI_K);
}

static void bp_drive(void)                      /* (V2) a sine through it (AMOUNT, TYPE; TONE left out), the clean one grey behind it; (V3.0) under it what is heard */
{
    uint32_t ty = (uint32_t)clamp(dr_type, 0, NDTYPE - 1);
    int32_t a = clamp(dr_amt, 0, 127), g = dr_gain(ty, a), lv = dr_level(ty, a), i, k;
    int16_t q[2 * 53], z[2 * 53];
    char r[8], m[28] = "DRIVEN:", *e = m + 7;
    ui_3(r, ui_pc(a));
    bp_ground(1);
    bp_head(DT_NAME[ty], a ? r : "OFF", a ? UI_K : UI_M);
    bp_r(0, 88, 234, 1, BR_TRACK);
    for (i = 0; i < 53; i++) {
        int32_t x = sine_i((uint32_t)i * 165191050u) * 3 / 4, y = a ? (dr_shape(ty, (x * g) >> 8) * lv) >> 12 : x;
        q[2 * i] = z[2 * i] = (int16_t)(12 + i * 4);
        q[2 * i + 1] = (int16_t)(88 - x * 46 / 32768), z[2 * i + 1] = (int16_t)(88 - y * 46 / 32768);
    }
    bp_pl(q, 53, 3, UI_M);
    bp_pl(z, 53, 5, a ? UI_K : UI_M);
    for (k = 0; k < (int32_t)NT; k++)
        if (trk[k].drv > 0 && trk[k].fx > 0) *e++ = ' ', *e++ = 'T', *e++ = (char)('1' + k), *e = 0;
    if (!a) put_s(m, "AMOUNT 0: NO DRIVE HEARD");
    else if (clamp(dr_mix, 0, 127) == 0) put_s(m, "MIX 0: NO DRIVE HEARD");
    else if (e == m + 7) put_s(m, "NO DRIVE SEND UP (MIXER 1)");
    bp_tt(10, 146, m, &BRT12, e > m + 7 && a && dr_mix > 0 ? UI_K : UI_M);
}

#ifdef DINGHY_HOST
static void br_prim_demo(void)                  /* (tests/brutal_test.py: a triangle, a line, a V, a slant, each of a known size) */
{
    static const int32_t TRI[6] = {20 * 16, 150 * 16, 80 * 16, 150 * 16, 50 * 16, 100 * 16};   /* base 60, height 50: 1500 pixels */
    static const int16_t HOR[4] = {100, 40, 200, 40}, VEE[6] = {110, 90, 140, 60, 170, 90}, SLA[4] = {150, 120, 210, 160};
    br_poly(TRI, 3, UI_W);
    br_polyline(HOR, 2, 5, UI_W);               /* rows 37 .. 41, columns 97 .. 201 */
    br_polyline(VEE, 3, 6, UI_W);               /* a point 3 sqrt(2) above (140, 60) */
    br_polyline(SLA, 2, 4, UI_W);
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
    case PG_SOUND:                              /* (the sources one by one: VA, PHASE so far) */
        return sel_eng() == ENG_VA || sel_eng() == ENG_PHASE;
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
    case PG_SOUND:
        if (sel_eng() == ENG_VA) { bp_va(); return; }
        if (sel_eng() == ENG_PHASE) { bp_phase(); return; }
        ui_pic_draw();
        return;
    default: ui_pic_draw();
    }
}
