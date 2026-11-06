/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM V3.0's BRUTAL theme (docs/themes/brutal-mockup.html, drawn there at 2x: here every size and place halved): black, red and concrete, hard
 * edges, no glow; the header a grey bar (the held button's name a black chip at its left, the battery at its right end) with a red tag beside it;
 * the knob blocks concrete, the selected one black with a red frame; each with a thin value bar along its bottom. The pages' logic is EYE's (ui.c):
 * what is shown, when it changes; only the drawing differs. A page's picture is BRUTAL's own (native) or EYE's, moved into BRUTAL's frame, until
 * the pages are drawn here (brutalp.c: every page's picture).
 * Included by ui.c (before the themes' table). The fonts (tools/gen_brutalfont.py, firmware/gen/brutal_font.h): Archivo Black, 1 bit, only big (16 and 24 px: the
 * page title if it fits, big values, HOME's numbers, the boot wordmark: its strokes run together smaller); all the small text Terminus Bold (6x12, 8x14,
 * 8x16: the chip, the tag's label, the blocks' labels and the values that do not fit big, the messages). Drawing by the shared functions (ui_rect, ui_region). */

#include "brutal_font.h"

#define BR_K UI_K                               /* the colours of the mockup: black, the title bar's grey, the concrete of the knob blocks, the tiles' two */
#define BR_BAR RGB(0x9a, 0x96, 0x8f)
#define BR_C1 RGB(0xb8, 0xb4, 0xab)
#define BR_C2 RGB(0xa7, 0xa3, 0x9b)
#define BR_C3 RGB(0xc9, 0xc5, 0xbc)
#define BR_TRACK RGB(0xa8, 0xa4, 0x9d)          /* (a bar's empty part on concrete: black at 18 %) */
#define BR_SELBG RGB(0x3a, 0x38, 0x35)          /* (a bar's empty part on black) */
#define BR_SELLAB RGB(0xe0, 0x70, 0x5f)         /* (the selected block's label) */
static const ui_pal_t UI_PAL_BRUTAL = {RGB(0x14, 0x13, 0x12), RGB(0x2a, 0x28, 0x26), RGB(0x6e, 0x6b, 0x66), RGB(0xb8, 0xb4, 0xab), RGB(0xf2, 0xef, 0xe8),
                                       {RGB(0xb8, 0x26, 0x1a), RGB(0xd0, 0x8a, 0x1c), RGB(0x2f, 0x8f, 0x8a), RGB(0x6a, 0x4a, 0x8c)},   /* red, amber,
                                                 * teal, violet (SYSTEM's COLOUR: the tag, the selected tile's frame, the bars) */
                                       {RGB(0xf2, 0xef, 0xe8), RGB(0xd0, 0x8a, 0x1c), RGB(0xb8, 0x26, 0x1a)},   /* the live meters: cream, amber, red */
                                       {0x6e, 0x6b, 0x66}, {0xf2, 0xef, 0xe8}};   /* MIXER 1's LEVEL ramp: mid to white */

/* ---- the font */
static uint32_t br_ix(char ch)                  /* a character's glyph: lower case is upper case, what the face has not is "?" */
{
    uint32_t c = (uint8_t)ch;
    if (c >= 'a' && c <= 'z') c -= 32u;
    return c >= 32u && c <= 95u ? c - 32u : (uint32_t)'?' - 32u;
}
static int32_t br_len(const br_font_t *f, const char *s, int32_t sc)   /* a text's width */
{
    int32_t n = 0;
    for (; *s; s++) n += f->adv[br_ix(*s)];
    return n * sc;
}
static int32_t br_cap(const br_font_t *f) { return f->hh['H' - 32]; }   /* the capitals' height (ink) */
static int32_t br_capy(const br_font_t *f) { return f->yo['H' - 32]; }  /* .. and where it starts from the line's top */
/* s at (x, y) = the line's top, in f, each pixel sc pixels, in col; the end. A row's run of ink is one rectangle */
static int32_t br_text(int32_t x, int32_t y, const char *s, const br_font_t *f, int32_t sc, uint16_t col)
{
    for (; *s; s++) {
        uint32_t g = br_ix(*s), w = f->w[g], h = f->hh[g], r, c, b = 0;
        const uint8_t *d = f->data + f->off[g];
        for (r = 0; r < h; r++, b += w)
            for (c = 0; c < w;) {
                uint32_t c0;
                if (!(d[(b + c) >> 3] >> (7u - ((b + c) & 7u)) & 1u)) { c++; continue; }
                for (c0 = c; c < w && (d[(b + c) >> 3] >> (7u - ((b + c) & 7u)) & 1u); c++)
                    ;
                ui_rect(x + (f->xo[g] + (int32_t)c0) * sc, y + (f->yo[g] + (int32_t)r) * sc, (int32_t)(c - c0) * sc, sc, col);
            }
        x += f->adv[g] * sc;
    }
    return x;
}

/* Terminus Bold: a fixed cell, a byte a row; s at (x, y) = the cell's top, in f, in col; the end */
static int32_t br_tlen(const br_tfont_t *f, const char *s) { return ui_len(s) * f->w; }
static int32_t br_tt(int32_t x, int32_t y, const char *s, const br_tfont_t *f, uint16_t col)
{
    for (; *s; s++, x += f->w) {
        uint32_t c = (uint8_t)*s, r, b, b0;
        const uint8_t *d;
        c = c >= 32u && c <= 126u ? c - 32u : (uint32_t)'?' - 32u;
        d = f->data + c * f->h;
        for (r = 0; r < f->h; r++)
            for (b = 0; b < f->w;) {
                if (!(d[r] >> (7u - b) & 1u)) { b++; continue; }
                for (b0 = b; b < f->w && (d[r] >> (7u - b) & 1u); b++)
                    ;
                ui_rect(x + (int32_t)b0, y + (int32_t)r, (int32_t)(b - b0), 1, col);
            }
    }
    return x;
}
static void br_tfit(char *d, const char *s, const br_tfont_t *f, int32_t room)   /* s cut to fit room pixels */
{
    int32_t n = room / f->w;
    int32_t i;
    for (i = 0; s[i] && i < n && i < 23; i++) d[i] = s[i];
    d[i] = 0;
}

/* ---- polygons and lines (the pictures of the curves and the waves: thick lines with sharp (miter) corners). Points in 1/16 pixel (Q4), pixels
 * whose middle is inside; the rows of the strip being drawn only */
#define BR_PT 24u                               /* the corners of a polygon, at most */
static void br_poly(const int32_t *xy, uint32_t n, uint16_t col)   /* xy: x0, y0, x1, y1 .. in Q4; filled (even-odd) */
{
    int32_t y0 = -cv_oy - ui_dy, y1 = y0 + (int32_t)cv_h, ylo = 0x7FFFFFF, yhi = -0x7FFFFFF, y;
    uint32_t i;
    for (i = 0; i < n; i++) {
        ylo = xy[2 * i + 1] < ylo ? xy[2 * i + 1] : ylo;
        yhi = xy[2 * i + 1] > yhi ? xy[2 * i + 1] : yhi;
    }
    ylo = (ylo + 7) >> 4, yhi = (yhi + 7) >> 4;   /* (the rows whose middle can be inside: from the first at or under the top, to the last above the bottom) */
    for (y = ylo > y0 ? ylo : y0; y < yhi && y < y1; y++) {
        int32_t xs[BR_PT], yc = y * 16 + 8, m = 0, j, k;
        for (i = 0; i < n; i++) {
            int32_t ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1u) % n)], by = xy[2 * ((i + 1u) % n) + 1];
            if ((ay <= yc) == (by <= yc)) continue;   /* (the edge does not cross this row's middle) */
            xs[m++] = ax + (yc - ay) * (bx - ax) / (by - ay);
        }
        for (j = 1; j < m; j++)                 /* (sorted: a few) */
            for (k = j; k > 0 && xs[k - 1] > xs[k]; k--) {
                int32_t t = xs[k];
                xs[k] = xs[k - 1], xs[k - 1] = t;
            }
        for (j = 0; j + 1 < m; j += 2) {
            int32_t xa = (xs[j] + 7) >> 4, xb = (xs[j + 1] + 7) >> 4;   /* (the first pixel whose middle is in, the first past) */
            if (xb > xa) ui_rect(xa, y, xb - xa, 1, col);
        }
    }
}
static int32_t br_isqrt(int32_t v)
{
    int32_t r = 0, b = 1 << 28;
    while (b > v) b >>= 2;
    for (; b; b >>= 2) {
        if (v >= r + b) v -= r + b, r = (r >> 1) + b;
        else r >>= 1;
    }
    return r;
}
/* a line of w pixels through the points p (n of them, pixels), in col; square ends, sharp (miter) corners, a bevel where one would be too long */
static void br_polyline(const int16_t *p, uint32_t n, int32_t w, uint16_t col)
{
    int32_t h = w * 8, i;                       /* (half the width, Q4) */
    int32_t pnx = 0, pny = 0;                   /* (the last segment's normal, half a width long, Q4) */
    for (i = 0; i + 1 < (int32_t)n; i++) {
        int32_t cx = p[2 * i] * 16, cy = p[2 * i + 1] * 16, bx = p[2 * i + 2] * 16, by = p[2 * i + 3] * 16, dx = bx - cx, dy = by - cy;
        int32_t len = br_isqrt(dx * dx + dy * dy), nx, ny, ax = cx, ay = cy, q[8];
        if (!len) continue;
        nx = -dy * h / len, ny = dx * h / len;
        if (i == 0) ax -= dx * h / len, ay -= dy * h / len;                   /* (square ends) */
        if (i + 2 == (int32_t)n) bx += dx * h / len, by += dy * h / len;
        q[0] = ax + nx, q[1] = ay + ny, q[2] = bx + nx, q[3] = by + ny, q[4] = bx - nx, q[5] = by - ny, q[6] = ax - nx, q[7] = ay - ny;
        br_poly(q, 4, col);
        if (i) {                                /* the corner at c: the gap on the outer side, a miter, or a bevel */
            int32_t s = pnx * ny - pny * nx > 0 ? -1 : 1, den = h * h + pnx * nx + pny * ny, t[8];
            t[0] = cx, t[1] = cy, t[2] = cx + s * pnx, t[3] = cy + s * pny, t[6] = cx + s * nx, t[7] = cy + s * ny;
            if (den > h * h / 8) {              /* (the miter point: c + s (n1 + n2) h^2 / (h^2 + n1 . n2); a needle: the bevel) */
                t[4] = cx + s * (pnx + nx) * (h * h / 16) / (den / 16), t[5] = cy + s * (pny + ny) * (h * h / 16) / (den / 16);
                br_poly(t, 4, col);
            } else {
                t[4] = t[6], t[5] = t[7];
                br_poly(t, 3, col);
            }
        }
        pnx = nx, pny = ny;
    }
}

/* ---- the header: the title bar (the held button's name a black chip, the title, the battery), the tag (a label and a value); a message over all of it */
static void br_bat_draw(int32_t x, int32_t y)   /* the battery as the title bar shows it: black on the grey (the lowest bar blinks in the accent), a bolt */
{
    static const char *const BOLT[6] = {"..##", ".##.", "####", ".##.", "##..", "#..."};
    int32_t l = ui_bat / 2, i, j;
    ui_rect(x, y, 14, 8, UI_K), ui_rect(x + 1, y + 1, 12, 6, BR_BAR), ui_rect(x + 14, y + 2, 2, 4, UI_K);
    if (l == 4) {
        for (j = 0; j < 6; j++)
            for (i = 0; i < 4; i++)
                if (BOLT[j][i] == '#') ui_rect(x + 5 + i, y + 1 + j, 1, 1, UI_K);
        return;
    }
    for (i = 0; i <= l; i++)
        if (l || !(ui_bat & 1)) ui_rect(x + 2 + 3 * i, y + 2, 2, 4, l ? UI_K : ui_acc);
}
static void br_fit(char *d, const char *s, const br_font_t *f, int32_t room)   /* s cut to fit room pixels in f */
{
    int32_t n = 0, w = 0;
    for (; s[n] && n < 23; n++) {
        w += f->adv[br_ix(s[n])];
        if (w > room) break;
        d[n] = s[n];
    }
    d[n] = 0;
}
static void br_tag(char *lab, char *val)        /* the tag: TRACK 03, BPM 120, VER 3.0 (as the mockup: the tempo on the pages of the effects, the song, HOME, ...) */
{
    switch (page) {
    case PG_HOME: case PG_LEVELS: case PG_TAPE: case PG_GRAIN: case PG_DELAY: case PG_SPACE: case PG_MASTER: case PG_DRIVE: case PG_PERFORM: case PG_SONG:
    case PG_PROJECTS:
        put_s(lab, "BPM"), tempo_text(val, "");
        if (ck_ext) put_s(val, "EXT");
        return;
    case PG_SYSTEM: case PG_SYS2: case PG_SYS3:
        put_s(lab, "VER"), put_s(val, "3.0");
        return;
    default:
        put_s(lab, "TRACK");
        val[0] = '0', val[1] = (char)('1' + sel), val[2] = 0;
    }
}
static void br_head_draw(void)
{
    char b[24], lab[8], val[16];
    int32_t x, room, w;
    if (ui_hmsg) {                              /* a message (SAVED, CLICK ON, VOICES FULL ..): the whole header, red, Terminus Bold */
        ui_rect(3, 3, 234, 26, ui_acc);
        br_tfit(b, ui_hl, &BRT16, 222);
        br_tt(9, 3 + (26 - BRT16.cap) / 2 - BRT16.capy, b, &BRT16, UI_W);
        return;
    }
    ui_rect(3, 3, 175, 26, BR_BAR);
    x = 3 + 4;
    ui_rect(x, 9, br_tlen(&BRT14, ui_hb) + 8, 14, UI_K);   /* the button's name: a black chip */
    br_tt(x + 4, 9, ui_hb, &BRT14, UI_W);
    x += br_tlen(&BRT14, ui_hb) + 8 + 6;
    room = 175 - (x - 3) - 5 - (ui_bat >= 0 ? 21 : 0);
    w = br_len(&BRF16, ui_hl, 1);
    if (w <= room) br_text(x, 3 + (26 - br_cap(&BRF16)) / 2 - br_capy(&BRF16), ui_hl, &BRF16, 1, UI_K);   /* (the title, big if it fits) */
    else {
        br_tfit(b, ui_hl, &BRT16, room);
        br_tt(x, 3 + (26 - BRT16.cap) / 2 - BRT16.capy, b, &BRT16, UI_K);
    }
    if (ui_bat >= 0) br_bat_draw(3 + 175 - 5 - 16, 12);
    ui_rect(181, 3, 56, 26, ui_acc);            /* the tag */
    br_tag(lab, val);
    br_tt(187, 5 - BRT12.capy, lab, &BRT12, UI_W);
    if (br_len(&BRF16, val, 1) <= 44) br_text(187, 15 - br_capy(&BRF16), val, &BRF16, 1, UI_W);
    else br_tt(187, 15 - BRT14.capy, val, &BRT14, UI_W);
}

/* ---- a knob's block: the label (Terminus Bold), the value (Archivo Black if it fits, else Terminus Bold), a 3 px bar along the bottom: how far the knob is turned */
static void br_knob_draw(void)
{
    const slab_t *S = &ui_sl[ui_k];
    int32_t x = 3 + (int32_t)ui_k * 59, y = ui_th->sy, w = 56, h = ui_th->sh, bw = w - 10, base = y + h - 8;   /* (base: the value's ink ends here) */
    int sel = S->sel, empty = S->lab[0] == '-' && !S->lab[1] && !S->val[0];
    uint16_t fg = sel ? UI_W : S->dim ? UI_M : UI_K;
    if (empty) { ui_rect(x, y, w, h, UI_D); return; }
    if (sel) {
        ui_rect(x, y, w, h, UI_K);
        ui_rect(x, y, w, 2, ui_acc), ui_rect(x, y + h - 2, w, 2, ui_acc), ui_rect(x, y, 2, h, ui_acc), ui_rect(x + w - 2, y, 2, h, ui_acc);
    } else
        ui_rect(x, y, w, h, BR_C3);
    br_tt(x + 4, y + 5 - BRT14.capy, S->lab, &BRT14, sel ? BR_SELLAB : UI_K);
    if (br_len(&BRF16, S->val, 1) <= bw) br_text(x + 5, base - br_cap(&BRF16) - br_capy(&BRF16), S->val, &BRF16, 1, fg);
    else if (br_tlen(&BRT14, S->val) <= w - 6) br_tt(x + 4, base - BRT14.cap - BRT14.capy, S->val, &BRT14, fg);
    else br_tt(x + 4, base - BRT12.cap - BRT12.capy, S->val, &BRT12, fg);
    ui_rect(x + 5, y + h - 5, bw, 3, sel ? BR_SELBG : BR_TRACK);
    if (S->fill) ui_rect(x + 5, y + h - 5, bw * S->fill / 8, 3, sel ? ui_acc : UI_K);
}

/* ---- HOME: the four tracks as tiles (the number, its source, its level; the selected one black with a red frame), CHD, CUT, DRY and MUTE as chips */
static void br_chip(int32_t x, int32_t y, const char *t, uint16_t bg, uint16_t fg)
{
    ui_rect(x, y, br_tlen(&BRT12, t) + 4, 10, bg);
    br_tt(x + 2, y + 1 - BRT12.capy, t, &BRT12, fg);
}
static void br_home(void)
{
    uint32_t t;
    for (t = 0; t < NT; t++) {
        int32_t x = 3 + (int32_t)(t & 1u) * 119, y = 32 + (int32_t)(t >> 1) * 82, w = 115, h = 78, isel = t == sel;
        int32_t lv = (int32_t)ui_pc(trk[t].level);
        char num[3] = {'0', (char)('1' + (int32_t)t), 0};
        const char *code = SRC_CODE[clamp(trk[t].eng, 0, NENG - 1)];
        int32_t cy = y + 34;
        if (isel) {
            ui_rect(x, y, w, h, UI_K);
            ui_rect(x, y, w, 2, ui_acc), ui_rect(x, y + h - 2, w, 2, ui_acc), ui_rect(x, y, 2, h, ui_acc), ui_rect(x + w - 2, y, 2, h, ui_acc);
        } else
            ui_rect(x, y, w, h, t & 1u ? BR_C2 : BR_C1);
        br_text(x + 7, y + 28 - br_cap(&BRF24) - br_capy(&BRF24), num, &BRF24, 1, isel ? ui_acc : UI_K);   /* (the number's ink ends at y + 28) */
        br_tt(x + w - 7 - br_tlen(&BRT14, code), y + 6 - BRT14.capy, code, &BRT14, isel ? UI_W : UI_K);
        if (trk[t].chord) br_chip(x + 7, cy, "CHD", isel ? UI_W : UI_K, isel ? UI_K : UI_W), cy += 12;
        if (trk_closed(t)) br_chip(x + 7, cy, "CUT", ui_acc, UI_W);
        else if (trk_dry(t)) br_chip(x + 7, cy, "DRY", ui_acc, UI_W);
        if (trk_quiet >> t & 1u) br_chip(x + w - 7 - 28, y + 34, "MUTE", ui_acc, UI_W);
        ui_rect(x + 7, y + h - 15, w - 14, 7, isel ? BR_SELBG : BR_TRACK);
        if (lv) ui_rect(x + 7, y + h - 15, (w - 14) * lv / 100, 7, isel ? UI_W : trk_quiet >> t & 1u ? UI_M : UI_K);
    }
}
