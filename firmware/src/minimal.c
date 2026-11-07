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
#define MN_BKEY RGB(0x4a, 0x4e, 0x53)              /* (the black keys of SCALE and CHORD) */
#define MN_SEL RGB(0x2c, 0x30, 0x33)               /* (the selected tile's fill) */
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
static uint32_t mn_lum(uint16_t c) { return ((c >> 11) * 8u + ((c >> 5) & 63u) * 4u + (c & 31u) * 8u) / 3u; }   /* a colour's brightness, 0 .. 255 */
static int mn_lit(uint16_t c)                   /* does this ink glow: GLOW on, and the ink as bright as the mid grey or brighter (dark and black ink never) */
{
    return MN_GLOW && mn_lum(c) >= mn_lum(UI_M);
}
/* Terminus at (x, y) = the cell's top, in f, each pixel sc pixels, in c; with GLOW the halo first (every pixel of the ink 1 px round it, a third of the colour); the end */
static int32_t mn_tt(int32_t x, int32_t y, const char *s, const br_tfont_t *f, int32_t sc, uint16_t c)
{
    int32_t x0 = x, pass;
    for (pass = mn_lit(c) ? 0 : 1; pass < 2; pass++) {
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
static void mn_line(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)   /* a rule (with GLOW, if it is a light one: a halo each side) */
{
    if (mn_lit(c)) ui_rect(x - (w > h ? 0 : 1), y - (w > h ? 1 : 0), w + (w > h ? 0 : 2), h + (w > h ? 2 : 0), mn_mix(c, 1, 3));
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
    if (page == PG_SYSTEM || page == PG_SYS2 || page == PG_SYS3) put_s(val, APP_VER);   /* (the build's whole stamp, 3.0-d1234: which build is installed) */
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

/* ---- the boot: a DOT FIELD (UI_BOOT_MS in all, 5.6 s; EYE's is the eye and the bats, BRUTAL's a concrete door). A grid of faint dots (2 px, every 8 px, the mockup's dark grey) fades in from the
 * middle out (0 .. 0.55 s); a ripple runs out from the centre (0.3 .. 1.65 s): the dots it passes brighten, then settle back; the eye forms out of the dots (1.0 .. 1.6 s): the dots along the open
 * eye's outline brighten one by one, clockwise from the left, then its iris's, amber; at MB_EYE (1.65 s) it becomes the thin outline eye (shut) and EYE's own motion starts, frame for frame on
 * EYE's 55 ms grid (its clock starts at MB_EYE, 30 frames in: frame n of this is frame n - 30 of EYE's; opens, trembles, looks round, blinks); GR!MM fades in under it (3.2 .. 3.8 s: the dots
 * under the words go), a line draws out under it (3.7 .. 4.2 s), the version line fades in (3.9 .. 4.5 s); at 4.8 s the eye shuts, from 4.9 s the dots fade out from the edges inward (a ring at
 * a time); at UI_BOOT_MS a clean cut to HOME. A dot is worked out from the time (mb_v: no buffer); each frame draws again only the dots whose value changed, a 6 x 6 px region each, the eye's
 * region when its pose changed, the words' where they moved. GLOW: the lit dots and the eye glow (the faint ones, being dark, never) */
#define MB_FORM 1000u                           /* the eye's outline begins to form, */
#define MB_EYE 1650u                            /* the thin eye takes over (30 x UI_ANIM_MS: its frames are EYE's), */
#define MB_TXT 3200u                            /* GR!MM begins to fade in, */
#define MB_SHUT 4800u                           /* the eye shuts (half, then all), */
#define MB_FO 4900u                             /* the dots fade out: the outermost ring first */
static uint32_t mb_a, mb_l, mb_b, mb_de, mb_td, mb_bt, mb_eye, mb_sup;   /* the wordmark's brightness (0 .. 8), the line's length (px, 0 .. 64), the version line's brightness; the time the dots were drawn at, when, the eye's
                                                 * half-speed time of the last frame, the thin eye is up, the dots under the words are off */
static bt_eye_t mb_ey;                          /* the eye's pose on the screen */
static void mn_eye(int32_t x, int32_t y, bt_eye_t e)   /* the thin eye at x, y (EYE's 5 px cells): the outline of its shape, the iris */
{
    int32_t r, c, pass;
    if (!ui_in(x - 1, y - 1, 82, 42)) return;
    for (pass = mn_lit(UI_L) ? 0 : 1; pass < 2; pass++) {
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
static uint32_t mb_pa(int32_t dx, int32_t dy)   /* an angle round the eye, clockwise from the left: 0 .. 1023 (west 0, north 256, east 512, south 768) */
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, s = ax + ay;
    if (!s) return 0;
    if (dx < 0 && dy <= 0) return (uint32_t)(256 * ay / s);
    if (dx >= 0 && dy < 0) return (uint32_t)(256 + 256 * ax / s);
    if (dx > 0 && dy >= 0) return (uint32_t)(512 + 256 * ay / s);
    return (uint32_t)(768 + 256 * ax / s);
}
static int32_t mb_form(int32_t x, int32_t y)    /* the dot at x, y against the open eye (at 80, 48): 1 on its outline, 2 on its iris, else 0 */
{
    int32_t c, r, ir, ic;
    char ch;
    if (x + 1 < 80 || y + 1 < 48) return 0;
    c = (x + 1 - 80) / 5, r = (y + 1 - 48) / 5;
    if (c > 15 || r > 7 || EYE[r][c] == '.') return 0;
    ir = r - 2, ic = c - 5;
    ch = ir >= 0 && ir < 4 && ic >= 0 && ic < 6 ? ui_iris[0][ir][ic] : '.';
    if (ch == 'a' || ch == 'w') return 2;
    if (ch == 'k') return 0;
    return r == 0 || r == 7 || c == 0 || c == 15 || EYE[r - 1][c] == '.' || EYE[r + 1][c] == '.' || EYE[r][c - 1] == '.' || EYE[r][c + 1] == '.' ? 1 : 0;
}
/* the value of the dot i, j at time e (sup: the dots under the words are off): 0 black, 1 .. 8 the faint grey at that eighth (the fade in and out: 4, 8), 10 .. 12 the ripple's three brightnesses, 13 the
 * eye's outline, 14 its iris */
static uint32_t mb_v(int32_t i, int32_t j, uint32_t e, uint32_t sup)
{
    int32_t x = 4 + 8 * i, y = 4 + 8 * j, dx = x + 1 - 120, dy = y + 1 - 120, d2 = dx * dx + dy * dy, ei = (int32_t)e, f, v, ring = i < 29 - i ? i : 29 - i, t = ei / 3, u = ei < 60 ? -1 : (ei - 60) / 3;
    ring = j < ring ? j : ring;
    ring = 29 - j < ring ? 29 - j : ring;
    v = d2 > t * t ? 0 : u >= 0 && d2 <= u * u ? 8 : 4;   /* (in: a front 333 px a second from the middle out, two steps, 60 ms apart: so few dots change in a frame; no root: squares) */
    f = ei < (int32_t)MB_FO + ring * 40 ? 8 : ei < (int32_t)MB_FO + ring * 40 + 40 ? 4 : 0;   /* (out: from the edges in, two steps) */
    v = f < v ? f : v;
    if (!v || (sup && x >= 24 && x < 216 && y >= 96 && y < 160)) return 0;
    if (ei >= 300 && ei < 1100) {               /* (the ripple: 240 px a second, its front brightest, three steps, 18 px of tail) */
        int32_t r = (ei - 300) * 24 / 100, k, hi, lo;
        for (k = 3; k > 0; k--) {
            hi = r - (3 - k) * 6, lo = hi - 6;
            if (hi >= 0 && d2 <= hi * hi && (lo < 0 || d2 > lo * lo)) { v = 9 + k; break; }
        }
    }
    if (e >= MB_FORM && e < MB_EYE && (f = mb_form(x, y))) {   /* (the eye forms: the outline, then the iris) */
        uint32_t pa = mb_pa(x + 1 - 120, y + 1 - 68);
        if (e >= (f == 1 ? MB_FORM + pa * 500u / 1024u : 1450u + pa * 150u / 1024u)) v = f == 1 ? 13 : 14;
    }
    return (uint32_t)v;
}
static uint16_t mb_col(uint32_t v)
{
    if (v <= 8u) return mn_mix(MN_TRACK, v, 8);
    if (v <= 12u) return (uint16_t)(mn_mix(MN_TRACK, 12u - v, 4) + mn_mix(UI_L, v - 8u, 4));
    return v == 13u ? UI_L : ui_acc;
}
static void mb_draw(void)                       /* the whole boot picture, as far as the region being drawn reaches */
{
    int32_t i, j;
    char s[2] = {0, 0};
    for (j = 0; j < 30; j++)
        for (i = 0; i < 30; i++) {
            int32_t x = 4 + 8 * i, y = 4 + 8 * j;
            uint32_t v;
            uint16_t c;
            if (!ui_in(x - 1, y - 1, 4, 4)) continue;
            v = mb_v(i, j, mb_de, mb_sup);
            if (!v) continue;
            c = mb_col(v);
            if (mn_lit(c)) ui_rect(x - 1, y - 1, 4, 4, mn_mix(c, 1, 3));
            ui_rect(x, y, 2, 2, c);
        }
    if (mb_eye) mn_eye(80 + mb_ey.jx, 48 + mb_ey.jy, mb_ey);
    if (mb_a && ui_in(52, 98, 136, 28))
        for (i = 0; i < 5; i++) {
            s[0] = "GR!MM"[i];
            mn_tt(56 + i * 28, 100, s, &BRT16, 2, mn_mix(UI_L, mb_a, 8));
        }
    if (mb_l) mn_line(120 - (int32_t)mb_l, 134, 2 * (int32_t)mb_l, 1, MN_FILL);
    if (mb_b || bt_note) {
        char vb[12] = "V";
        const char *l = bt_note;
        int32_t n;
        for (n = 0; !l && n < 10 && APP_VER[n] && APP_VER[n] != '-'; n++) vb[n + 1] = APP_VER[n], vb[n + 2] = 0;   /* (just V3.0: the build's stamp is on the SYSTEM pages' header) */
        if (!l) l = vb;
        mn_t(120 - br_tlen(&BRT12R, l) / 2, 146, l, &BRT12R, mn_mix(bt_note ? ui_acc : MN_DIM, bt_note ? 8u : mb_b, 8));
    }
}
static void mb_start(void)
{
    mb_de = 0, mb_bt = 0, mb_eye = 0, mb_a = mb_l = mb_b = 0, mb_sup = bt_note != 0, mb_td = 0;
    ui_region(0, 0, 240, 240, UI_K, mb_draw);
}
static void mb_dots(uint32_t e)                 /* a frame of the dots (every 33 ms, their own clock): where a run of dots in a row changed, one region over the run (4 px high: a dot 2 px, the halo's 1 px
                                                 * round it); no buffer: each dot's value is worked out from the time it was drawn at and now */
{
    uint32_t e0 = mb_de, sup0 = mb_sup;
    int32_t i, j, n;
#ifdef DINGHY_HOST
    static uint32_t worst;
    uint32_t px0 = host_lcd_px, rg = 0, nd = 0;
#endif
    mb_de = e, mb_sup = e >= MB_TXT || bt_note != 0;
    for (j = 0; j < 30; j++)
        for (i = 0; i < 30;) {
            if (mb_v(i, j, e0, sup0) == mb_v(i, j, e, mb_sup)) { i++; continue; }
            for (n = 1; i + n < 30 && mb_v(i + n, j, e0, sup0) != mb_v(i + n, j, e, mb_sup); n++)
                ;
            ui_region(4 + 8 * i - 1, 4 + 8 * j - 1, 8 * (n - 1) + 4, 4, UI_K, mb_draw);
#ifdef DINGHY_HOST
            rg++, nd += (uint32_t)n;
#endif
            i += n;
        }
#ifdef DINGHY_HOST
    if (host_lcd_px - px0 + rg * 40u > worst) {  /* (tests/minimal_test.py: the worst frame of the dots, its pixels, its regions: the time it takes is the pixels at the display's rate, 16 bits at 12 MHz,
                                                 * and each region's set-up) */
        worst = host_lcd_px - px0 + rg * 40u;
        printf("mbstat %u ms: %u px in %u regions, %u dots\n", e, host_lcd_px - px0, rg, nd);
    }
#endif
}
static void mb_frame(uint32_t e)                /* a frame on EYE's 55 ms grid: the eye, the words, the line */
{
    uint32_t a = e < MB_TXT ? 0u : e >= MB_TXT + 600u ? 8u : (e - MB_TXT) * 8u / 600u;
    uint32_t l = e < 3700u ? 0u : e >= 4200u ? 64u : (e - 3700u) * 64u / 500u, b = e < 3900u ? 0u : e >= 4500u ? 8u : (e - 3900u) * 8u / 600u;
    if (e >= MB_EYE) {                          /* the eye: EYE's own rule (drawn again when the pose a frame before it is not the pose now), from its clock's start; shut at the end */
        uint32_t eb = e - MB_EYE;
        bt_eye_t now = e >= MB_FO ? (bt_eye_t){0, 0, 0, 0, 0, 0} : e >= MB_SHUT ? (bt_eye_t){2, 0, 0, 0, 0, 0} : bt_eye(eb / 2u);
        int redraw = !mb_eye || (e >= MB_SHUT ? !bt_eye_same(mb_ey, now) : !bt_eye_same(bt_eye(mb_bt), bt_eye(eb)));
        mb_bt = eb / 2u;
        if (redraw) {
            mb_ey = now, mb_eye = 1;
            ui_region(70, 38, 100, 56, UI_K, mb_draw);
        }
    }
    if (a != mb_a) mb_a = a, ui_region(50, 94, 140, 36, UI_K, mb_draw);
    if (l != mb_l) {
        mb_l = l;
        ui_region(120 - (int32_t)l - 2, 130, 2 * (int32_t)l + 4, 8, UI_K, mb_draw);
    }
    if (b != mb_b) mb_b = b, ui_region(0, 142, 240, 16, UI_K, mb_draw);
}
