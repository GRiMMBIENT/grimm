/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V3.0's MINIMAL theme (docs/themes/minimal-mockup.html, drawn there at 2x: here every size and place halved): a black ground, greys, one amber accent, thin 1 px lines.
 * The header is the page's name and the tag (a label, a value) over a 1 px rule; the knob row has thin dividers, each knob its label (dim, light cut), its value (bold) and a thin
 * value bar, the selected one amber with an amber underline. All the text is Terminus (tools/gen_minimalfont.py: the regular cuts, the light text; brutal_font.h: the bold ones, the
 * values and titles), bigger only by whole-number scaling. GLOW (SYSTEM 3, KNOB 4): a halo, a dimmer colour 1 px round the lines and the text, a blend worked out as it is
 * drawn (no blur, no buffer). The pages' logic is EYE's (ui.c); HOME is MINIMAL's own, the other pages' pictures are EYE's drawn in MINIMAL's colours, their text in Terminus.
 * Included by ui.c (before the boot screen's start, after the boot's eye: the boot is here too). */

#include "minimal_font.h"

#define MN_RULE RGB(0x3a, 0x3e, 0x42)           /* the colours of the mockup besides the palette's: the rules and dividers, a bar's empty part, the selected tile, the dim labels, a bar's fill */
#define MN_TRACK RGB(0x34, 0x38, 0x3c)
#define MN_DIM RGB(0x7c, 0x80, 0x86)
#define MN_FILL RGB(0x8a, 0x8e, 0x93)
#define MN_GLOW (ui_th->bfin == 2 && sy_glow)   /* GLOW on, in MINIMAL */

static const ui_pal_t UI_PAL_MINIMAL = {RGB(0x00, 0x00, 0x00), RGB(0x22, 0x25, 0x28), RGB(0x6a, 0x6e, 0x73), RGB(0xd6, 0xd6, 0xd2), RGB(0xf0, 0xf0, 0xec),
                                        {RGB(0xe3, 0x9b, 0x2d), RGB(0xe8, 0xc5, 0x47), RGB(0x3f, 0xb8, 0xaf), RGB(0x9a, 0x7b, 0xd0)},   /* amber (the default), yellow, teal,
                                                 * violet (SYSTEM's COLOUR) */
                                        {RGB(0xd6, 0xd6, 0xd2), RGB(0xe3, 0x9b, 0x2d), RGB(0xd9, 0x4a, 0x2a)},   /* the live meters: light, amber, red */
                                        {0x6a, 0x6e, 0x73}, {0xf0, 0xf0, 0xec}};

static uint16_t mn_mix(uint16_t c, uint32_t a, uint32_t n)   /* c, a parts in n of its brightness (565) */
{
    return (uint16_t)((((c >> 11) * a / n) << 11) | ((((c >> 5) & 63u) * a / n) << 5) | ((c & 31u) * a / n));
}
/* Terminus at (x, y) = the cell's top, in f, each pixel sc pixels, in c; with GLOW the halo first (every pixel of the ink 1 px round it, a third of the colour); the end */
static int32_t mn_tt(int32_t x, int32_t y, const char *s, const br_tfont_t *f, int32_t sc, uint16_t c)
{
    int32_t x0 = x, pass;
    for (pass = MN_GLOW ? 0 : 1; pass < 2; pass++) {
        const char *p = s;
        uint16_t col = pass ? c : mn_mix(c, 1, 3);
        int32_t g = pass ? 0 : 1;
        for (x = x0; *p; p++, x += f->w * sc) {
            uint32_t ch = (uint8_t)*p, r, b, b0;
            const uint8_t *d;
            ch = ch >= 32u && ch <= 126u ? ch - 32u : (uint32_t)'?' - 32u;
            d = f->data + ch * f->h;
            for (r = 0; r < f->h; r++)
                for (b = 0; b < f->w;) {
                    if (!(d[r] >> (7u - b) & 1u)) { b++; continue; }
                    for (b0 = b; b < f->w && (d[r] >> (7u - b) & 1u); b++)
                        ;
                    ui_rect(x + (int32_t)b0 * sc - g, y + (int32_t)r * sc - g, (int32_t)(b - b0) * sc + 2 * g, sc + 2 * g, col);
                }
        }
    }
    return x;
}
static int32_t mn_t(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c)   /* y = the capitals' ink top */
{
    return mn_tt(x, y - f->capy, s, f, 1, c);
}
static int32_t mn_tr(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t c)   /* .. right-aligned: ends at x */
{
    return mn_t(x - br_tlen(f, s), y, s, f, c);
}
static void mn_line(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a rule (with GLOW: a halo each side) */
{
    if (MN_GLOW) ui_rect(x - (w > h ? 0 : 1), y - (w > h ? 1 : 0), w + (w > h ? 0 : 2), h + (w > h ? 2 : 0), mn_mix(c, 1, 3));
    ui_rect(x, y, w, h, c);
}

/* a borrowed EYE picture's text (the pages EYE draws): Terminus, the regular for the dim colours, the bold for the rest; 2x text one size up */
static int32_t mn_ui_text(int32_t x, int32_t y, const char *s, int32_t sc, uint16_t c)
{
    const br_tfont_t *f = sc >= 2 ? &BRT16 : c == UI_M || c == UI_D ? &BRT12R : &BRT12;
#ifdef DINGHY_HOST
    host_btext++;
#endif
    return mn_t(x, y, s, f, c);
}

/* ---- the header: the button's name (dim, light), the title (bold), the battery, the tag (a label dim, its value bold) over a 1 px rule; a message turns it all to the message in amber */
static void mn_bat(int32_t x, int32_t y)        /* the battery: a thin outline, a bar a level (the lowest blinks in the accent), a bolt charging */
{
    static const char *const BOLT[6] = {"..##", ".##.", "####", ".##.", "##..", "#..."};
    int32_t l = ui_bat / 2, i, j;
    ui_rect(x, y, 14, 1, MN_DIM), ui_rect(x, y + 7, 14, 1, MN_DIM), ui_rect(x, y, 1, 8, MN_DIM), ui_rect(x + 13, y, 1, 8, MN_DIM), ui_rect(x + 14, y + 2, 2, 4, MN_DIM);
    if (l == 4) {
        for (j = 0; j < 6; j++)
            for (i = 0; i < 4; i++)
                if (BOLT[j][i] == '#') ui_rect(x + 5 + i, y + 1 + j, 1, 1, ui_acc);
        return;
    }
    for (i = 0; i <= l; i++)
        if (l || !(ui_bat & 1)) ui_rect(x + 2 + 3 * i, y + 2, 2, 4, l ? UI_L : ui_acc);
}
static void mn_head_draw(void)
{
    char b[24], lab[8], val[16];
    int32_t x, tx, room;
    mn_line(0, 27, 240, 1, MN_RULE);
    if (ui_hmsg) {                              /* a message (SAVED, CLICK ON, VOICES FULL ..): the whole header, bold, amber */
        br_tfit(b, ui_hl, &BRT16, 232);
        mn_t(5, 9, b, &BRT16, ui_acc);
        return;
    }
    br_tag(lab, val);
    tx = 236 - br_tlen(&BRT12R, lab) - 6 - br_tlen(&BRT16, val);
    mn_t(tx, 10, lab, &BRT12R, MN_DIM);
    mn_t(tx + br_tlen(&BRT12R, lab) + 6, 9, val, &BRT16, UI_L);
    if (ui_bat >= 0) mn_bat(tx - 24, 10);
    x = 5 + br_tlen(&BRT12R, ui_hb) + 8;
    mn_t(5, 10, ui_hb, &BRT12R, MN_DIM);
    room = tx - 6 - x - (ui_bat >= 0 ? 24 : 0);
    br_tfit(b, ui_hl, &BRT16, room);
    mn_t(x, 9, b, &BRT16, UI_L);
}

/* ---- a knob's block: a rule above, a divider at its right, the label (dim, regular), the value (bold, as big as fits), a 2 px value bar (the selected one's amber) and, selected, an amber
 * underline; the selected block's label and value amber */
static void mn_knob_draw(void)
{
    const slab_t *S = &ui_sl[ui_k];
    int32_t x = 3 + (int32_t)ui_k * 59, y = ui_th->sy, w = ui_th->kw, h = ui_th->sh, bw = w - 12;
    int sel = S->sel, empty = S->lab[0] == '-' && !S->lab[1] && !S->val[0];
    uint16_t fg = sel || S->acc ? ui_acc : S->dim ? UI_M : UI_L;
    const br_tfont_t *vf = br_tlen(&BRT16, S->val) <= w - 8 ? &BRT16 : br_tlen(&BRT14, S->val) <= w - 8 ? &BRT14 : &BRT12;
    ui_rect(x, y, w, 1, MN_RULE);
    if (ui_k < 3u) ui_rect(x + w - 1, y + 4, 1, h - 8, MN_RULE);
    if (empty) return;
    mn_t(x + 5, y + 7, S->lab, &BRT12R, sel ? ui_acc : MN_DIM);
    mn_t(x + 5, y + 19, S->val, vf, fg);
    ui_rect(x + 5, y + h - 9, bw, 2, MN_TRACK);
    if (S->fill) ui_rect(x + 5, y + h - 9, bw * S->fill / 8, 2, sel ? ui_acc : MN_FILL);
    if (sel) ui_rect(x, y + h - 2, w - (ui_k < 3u), 2, ui_acc);
}

/* ---- HOME: a row a track (150 px apart: T1, its source, CHD / CUT / DRY, its level as a bar and in %, MUTE; under it the first 16 steps of its pattern, a note one amber (the selected
 * track's) or grey, the step playing light); the selected track's amber */
static void mn_home(void)
{
    uint32_t t;
    int32_t i;
    for (t = 0; t < NT; t++) {
        int32_t y = 32 + (int32_t)t * 40, lv = (int32_t)ui_pc(trk[t].level), cx = 48, n;
        int isel = t == sel, q = (int)(trk_quiet >> t & 1u);
        const seq_trk_t *S = &seq[t];
        const seq_pat_t *P = &S->pat[clamp(S->cur, 0, SEQ_NPAT - 1)];
        char num[3] = {'T', (char)('1' + (int32_t)t), 0}, v[8];
        mn_t(4, y + 6, num, &BRT16, isel ? ui_acc : UI_L);
        mn_t(28, y + 8, SRC_CODE[clamp(trk[t].eng, 0, NENG - 1)], &BRT12R, MN_DIM);
        if (trk[t].chord) cx = mn_t(cx, y + 8, "CHD", &BRT12R, ui_acc) + 6;
        if (trk_closed(t)) mn_t(cx, y + 8, "CUT", &BRT12R, ui_acc);
        else if (trk_dry(t)) mn_t(cx, y + 8, "DRY", &BRT12R, ui_acc);
        ui_rect(100, y + 9, 106, 4, MN_TRACK);
        if (lv) ui_rect(100, y + 9, 106 * lv / 100, 4, isel ? ui_acc : MN_FILL);
        if (q) mn_tr(236, y + 8, "MUTE", &BRT12R, ui_acc);
        else mn_tr(236, y + 8, (put_u(v, (uint32_t)lv), v), &BRT12R, MN_DIM);
        n = seq_steps(S);
        for (i = 0; i < 16 && i < n; i++)
            ui_rect(100 + i * 7, y + 24, 5, 4, i == S->pos && seq_on ? UI_L : P->st[i].note ? (isel ? ui_acc : UI_M) : UI_D);
    }
}
static int mn_native(void) { return page == PG_HOME; }
static void mn_pic(void)
{
    if (page == PG_HOME) mn_home();
    else ui_pic_draw();
}

/* ---- the boot: EYE's eye (the same frames at the same times: its pose from bt_eye, as the boot's redraws follow EYE's own rule) as a thin outline (1 px round its shape; with GLOW the halo)
 * with the iris amber, white and black, on black; then GR!MM fades in (3.6 .. 4.2 s), a thin line, as wide as the word, draws out from its middle (4.2 .. 4.8 s), the version line fades in (4.4 .. 5.0 s); then, at UI_BOOT_MS,
 * a clean cut to HOME. No bats, no stars, no drips */
static uint32_t mb_a, mb_l, mb_b;               /* the wordmark's brightness (0 .. 8), the line's length (px, 0 .. 64), the version line's brightness */
static void mn_eye(int32_t x, int32_t y, bt_eye_t e)   /* the eye at x, y (EYE's 5 px cells): the outline of its shape, the iris */
{
    int32_t r, c, pass;
    if (!ui_in(x - 1, y - 1, 82, 42)) return;
    for (pass = MN_GLOW ? 0 : 1; pass < 2; pass++) {
        int32_t g = pass ? 0 : 1;
        uint16_t col = pass ? UI_L : mn_mix(UI_L, 1, 3);
        for (r = 0; r < 8; r++) {
            int32_t d = r < 4 ? 3 - r : r - 4;
            if (d > e.k) continue;
            for (c = 0; c < 16; c++) {
                int32_t ux = x + c * 5, uy = y + r * 5;
                if (EYE[r][c] == '.') continue;
#define VIS(rr, cc) ((rr) >= 0 && (rr) < 8 && (cc) >= 0 && (cc) < 16 && EYE[rr][cc] != '.' && ((rr) < 4 ? 3 - (rr) : (rr) - 4) <= e.k)
                if (!VIS(r - 1, c)) ui_rect(ux - g, uy - g, 5 + 2 * g, 1 + 2 * g, col);
                if (!VIS(r + 1, c)) ui_rect(ux - g, uy + 4 - g, 5 + 2 * g, 1 + 2 * g, col);
                if (!VIS(r, c - 1)) ui_rect(ux - g, uy - g, 1 + 2 * g, 5 + 2 * g, col);
                if (!VIS(r, c + 1)) ui_rect(ux + 4 - g, uy - g, 1 + 2 * g, 5 + 2 * g, col);
#undef VIS
            }
        }
    }
    for (r = 0; r < 8; r++) {                   /* the iris (the lid's row has none) */
        int32_t d = r < 4 ? 3 - r : r - 4;
        if (d > e.k || (d == e.k && e.k < 4)) continue;
        for (c = 0; c < 16; c++) {
            int32_t ir = r - 2 - e.dy, ic = c - 5 - e.dx;
            char ch;
            if (EYE[r][c] == '.' || EYE[r][c] == 'l' || ir < 0 || ir >= 4 || ic < 0 || ic >= 6) continue;
            ch = ui_iris[e.tiny ? 1 : 0][ir][ic];
            if (ch != '.') ui_rect(x + c * 5 + 1, y + r * 5 + 1, 3, 3, ch == 'a' ? ui_acc : ch == 'w' ? UI_W : UI_K);
        }
    }
}
static void mb_draw(void)                       /* the whole boot picture, as far as the region being drawn reaches */
{
    int32_t i;
    char s[2] = {0, 0};
    mn_eye(80 + bt_eye(bt_e).jx, 48 + bt_eye(bt_e).jy, bt_eye(bt_e));
    if (mb_a && ui_in(52, 98, 136, 28))
        for (i = 0; i < 5; i++) {
            s[0] = "GR!MM"[i];
            mn_tt(56 + i * 28, 100, s, &BRT16, 2, mn_mix(UI_L, mb_a, 8));
        }
    if (mb_l) mn_line(120 - (int32_t)mb_l, 134, 2 * (int32_t)mb_l, 1, MN_FILL);
    if (mb_b || bt_note) {
        const char *l = bt_note ? bt_note : BOOT_LINE;
        mn_t(120 - br_tlen(&BRT12R, l) / 2, 146, l, &BRT12R, mn_mix(bt_note ? ui_acc : MN_DIM, bt_note ? 8u : mb_b, 8));
    }
}
static void mb_start(void)
{
    bt_e = 0, mb_a = mb_l = mb_b = 0;
    ui_region(0, 0, 240, 240, UI_K, mb_draw);
}
static void mb_frame(uint32_t e)                /* a frame at e: where it moved */
{
    uint32_t e0 = bt_e, a = e < 3600u ? 0u : e >= 4200u ? 8u : (e - 3600u) * 8u / 600u, l = e < 4200u ? 0u : e >= 4800u ? 64u : (e - 4200u) * 64u / 600u;
    uint32_t b = e < 4400u ? 0u : e >= 5000u ? 8u : (e - 4400u) * 8u / 600u;
    bt_e = e / 2u;                              /* (the eye at half speed, as EYE's: and drawn again by EYE's own rule) */
    if (!bt_eye_same(bt_eye(e0), bt_eye(e))) ui_region(70, 38, 100, 56, UI_K, mb_draw);
    if (a != mb_a) mb_a = a, ui_region(50, 94, 140, 36, UI_K, mb_draw);
    if (l != mb_l) {
        mb_l = l;
        ui_region(120 - (int32_t)l - 2, 130, 2 * (int32_t)l + 4, 8, UI_K, mb_draw);
    }
    if (b != mb_b) mb_b = b, ui_region(0, 142, 240, 16, UI_K, mb_draw);
}
