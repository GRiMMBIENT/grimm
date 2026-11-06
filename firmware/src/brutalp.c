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
        return 1;
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
    default: ui_pic_draw();
    }
}
