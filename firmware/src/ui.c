/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's screens, after the concept designs (docs/ui-concept, the .dc.html files, drawn there at 2x: here every size and place
 * halved). Plain pixels, five greys and one accent, the pixfont (Press Start 2P at its 8 px grid, SIL OFL 1.1:
 * firmware/gen/grimm_pixfont.h, tools/gen_pixfont.py) at 8 px and, for values and big numbers, 16 px.
 *   the frame, every page: the header (the top 24 px: an accent block with the button, a dark bar with the page and the
 *   track on the left and the page's number or a state on the right); the picture (24 .. 164: that page's pixel art,
 *   on a grid of 54 x 30 cells of 4 px from (12, 40), a caption line at y 31); four slabs (165 .. 236), one a knob:
 *   its label, 8 segments (light: filled), its value (16 px up to 3 characters, else 8 px)
 *   pages not built yet: the frame, their slabs named with empty bars and "---", NOT BUILT YET in the picture
 *   moving: the boot screen and HOME's picture, a frame every UI_ANIM_MS, only what moved drawn (ui_boot_frame,
 *   ui_home_frame; the audio ISR leaves its peaks in vis_pk, vis_lr)
 * Drawn in regions (the header, the picture, each slab) into the canvas, a strip at a time (gfx.c: 240 x 64), and only
 * when what they show changes; the picture at most every UI_PIC_MS while a knob turns. Main loop only. */

#include "grimm_pixfont.h"

#define UI_K RGB(0x0b, 0x0b, 0x0a)              /* the palette: black (the background), */
#define UI_D RGB(0x26, 0x25, 0x1f)              /* dark */
#define UI_M RGB(0x5e, 0x5b, 0x54)              /* mid */
#define UI_L RGB(0xb8, 0xb4, 0xa9)              /* light */
#define UI_W RGB(0xe8, 0xe4, 0xd8)              /* white */
static const uint16_t UI_ACCENTS[4] = {RGB(0xc8, 0x32, 0x1e), RGB(0xd0, 0x8a, 0x1c), RGB(0x2f, 0x8f, 0x8a),
                                       RGB(0x7a, 0x4f, 0xb0)};   /* red (the default), amber, teal, violet (SYSTEM) */
static uint16_t ui_acc = RGB(0xc8, 0x32, 0x1e); /* the accent */
#define UI_PIC_MS 40u                           /* the picture redrawn at most this often */
#define UI_BAT_W 16                             /* the header's battery (and its tip) */
#define UI_BOOT_MS 3000u                        /* the boot screen this long */
#define UI_ANIM_MS 55u                          /* a frame of the moving pictures (the boot screen, HOME): ~18 a second */
#define PX0 12                                  /* the picture grid: 54 x 30 cells of 4 px from (PX0, PY0) */
#define PY0 40

/* ---- drawing in screen coordinates into a strip of a region */
static int32_t ui_ox;                           /* the region's left edge (cv_oy: its strip's top) */
static uint32_t ui_k;                           /* (the slab being drawn) */
static void ui_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x - ui_ox, y, w, h, c); }
static void ui_cell(int32_t x, int32_t y, uint16_t c) { ui_rect(PX0 + x * 4, PY0 + y * 4, 4, 4, c); }
static int32_t ui_text(int32_t x, int32_t y, const char *s, int32_t sc, uint16_t c)   /* pixfont x sc; the end */
{
    for (; *s; s++, x += 8 * sc) {
        uint32_t ch = (uint8_t)*s, r, b;
        const uint8_t *g = PIXFONT[(ch >= 32u && ch < 127u ? ch : '?') - 32u];
        for (r = 0; r < 8u; r++)
            for (b = 0; b < 8u; b++)
                if (g[r] & (0x80u >> b))
                    ui_rect(x + (int32_t)b * sc, y + (int32_t)r * sc, sc, sc, c);
    }
    return x;
}
static int32_t ui_len(const char *s) { int32_t n = 0; while (s[n]) n++; return n; }
static void ui_region(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t bg, void (*f)(void))
{
    int32_t rows = (int32_t)(CV_MAX / (uint32_t)w), y0, hh;
    for (y0 = y; y0 < y + h; y0 += rows) {
        hh = y + h - y0 < rows ? y + h - y0 : rows;
        cv_begin((uint32_t)w, (uint32_t)hh, bg);
        ui_ox = x;
        cv_oy = -y0;
        f();
        cv_oy = 0;
        cv_blit((uint32_t)x, (uint32_t)y0);
    }
}
static uint32_t ui_rnd_s;                       /* the concepts' random: (s 9301 + 49297) mod 233280 */
static uint32_t ui_rnd(uint32_t n) { ui_rnd_s = (ui_rnd_s * 9301u + 49297u) % 233280u; return ui_rnd_s * n / 233280u; }
static char *ui_note(char *s, uint32_t n)       /* a MIDI note's name: "C4", "C#4" (60 = C4) */
{
    static const char NN[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t o = (int32_t)(n / 12u) - 1;
    s = put_s(s, NN[n % 12u]);
    if (o < 0) return put_u(put_s(s, "-"), (uint32_t)-o);
    return put_u(s, (uint32_t)o);
}
static char *ui_3(char *s, uint32_t v) { s[0] = (char)('0' + v / 100u % 10u), s[1] = (char)('0' + v / 10u % 10u), s[2] = (char)('0' + v % 10u), s[3] = 0; return s + 3; }
static uint32_t ui_pc(int32_t v) { return ((uint32_t)clamp(v, 0, 127) * 100u + 63u) / 127u; }

/* a source's short name (the HOME slabs: "T2 FM") */
static const char *const SRC_CODE[NENG] = {"VA", "FM", "SA", "VO", "WH", "PH", "PD"};

/* ---- the header */
static char ui_hb[12], ui_hl[24], ui_hr[12];    /* the button, the left, the right */
static int32_t ui_bat;                          /* the battery as the header shows it (beside the tempo): bat_lvl x 2 +
                                                 * its blink (lowest: on, off), -1 none */
static void ui_head_text(void)
{
    const page_t *P = &PAGES[page];
    uint32_t p, n = 0, i = 0;
    char *e = ui_hl;
    put_s(ui_hb, BTN_NAME[P->btn]);
    *e = 0;
    if (!same(P->name, BTN_NAME[P->btn]))
        e = put_s(e, page == PG_PATTERN ? "PATTERN A" : P->name);
    if (page == PG_HOME) {                      /* HOME: the track and its sound ("T1 CRYPT PAD": the one PRESETS or
                                                 * SOUNDS loaded or the editor named, its source still the sound's;
                                                 * else its source), cut to fit beside the tempo and the battery */
        e = put_s(put_u(put_s(e, "T"), sel + 1u), " ");
        if (trk_sname(sel, e) == e) put_s(e, ENGINES[sel_eng()]->name);
    } else if (P->own) {                        /* the track and its source: "T1 VA" */
        if (e != ui_hl) e = put_s(e, " ");
        e = put_s(put_u(put_s(e, "T"), sel + 1u), " ");
        put_s(e, ENGINES[sel_eng()]->name);
        if (trk[sel].chord || trk[sel].arp) {   /* chord mode, ARP on: CHORD, ARP after the track (no room: in the
                                                 * source's place, shorter if need be) */
            const char *tg = trk[sel].chord && trk[sel].arp ? "CHORD ARP" : trk[sel].chord ? "CHORD" : "ARP";
            int32_t room = 20 - ui_len(ui_hb);  /* (characters before the right side's) */
            if (ui_len(ui_hl) + 1 + ui_len(tg) <= room) put_s(put_s(ui_hl + ui_len(ui_hl), " "), tg);
            else put_s(e, (int32_t)(e - ui_hl) + ui_len(tg) <= room || !trk[sel].chord || !trk[sel].arp ? tg : "CH+ARP");
        }
    }
    if (P->btn == BTN_SEL)                      /* (Scale: the chord mode, the track on the right) */
        put_s(ui_hl, page == PG_CHORD ? (trk[sel].chord ? "CHORD ON" : "CHORD OFF") : trk[sel].chord ? "SCALE CHORD" : "SCALE");
    for (p = 0; p < NPAGE; p++)                 /* its number in the button's round: "1/2" */
        if (PAGES[p].btn == P->btn && pg_on(p)) {
            n++;
            if (pg_on(page) && pg_key(p) <= pg_key(page)) i++;
        }
    if (save_held) {                            /* SAVE held: the slots, "SLOTS A B - D" (- empty) */
        char *q = put_s(ui_hl, "SLOTS");
        for (p = 0; p < 4u; p++) *q++ = ' ', *q++ = slot_used(p) ? (char)('A' + p) : '-';
        *q = 0;
    }
    if (sv_wait) put_s(ui_hl, "SAVES WHEN SILENT");   /* (a save waits: a voice sounds, a latched note) */
    if (fm1_ms < hd_until) put_s(ui_hl, hd_msg);   /* (a moment: SAVED, LOADED, a sound's name) */
    if (learn_st) put_s(ui_hl, learn_st == 1u ? "TURN A KNOB" : "MOVE A CC");   /* (MIDI LEARN, till it ends) */
    if (as_err) put_s(ui_hl, "SAVE ERROR: RETRYING");

    if (page == PG_SOUNDS) put_u(put_s(ui_hr, "T"), sel + 1u);   /* (Save: the track) */
    else if (page == PG_PROJECTS) ui_hr[0] = (char)('A' + (slot_cur & 3)), ui_hr[1] = 0;   /* (the slot in use) */
    else if (ge_freeze && P->btn == BTN_FX) put_s(ui_hr, "FREEZE");
    else if (page == PG_GRID) put_s(put_u(ui_hr, (uint32_t)seq_steps(&seq[sel])), " ST");   /* (Seq: the steps) */
    else if (page == PG_HOME) put_u(put_s(ui_hr, "> "), (uint32_t)clamp(seq_bpm, 20, 300));   /* (Main: the tempo) */
    else if (P->btn == BTN_SEL) put_u(put_s(ui_hr, "T"), sel + 1u);
    else if (page == PG_ARP) put_s(ui_hr, trk[sel].latch ? "LATCH" : "");   /* (Arp: LATCH, if on) */
    else if (page == PG_SYSTEM || page == PG_SYS2) put_s(ui_hr, "V" APP_VER);   /* (System: the version) */
    else if (page == PG_FMALGO || page == PG_FMOP) put_u(put_s(ui_hr, "OP "), (uint32_t)clamp(fm_op, 0, 5) + 1u);   /* (EditFM:
                                                 * the operator) */
    else if (page == PG_SONG) put_u(put_s(put_u(ui_hr, (uint32_t)song_cur() + 1u), "/"), song_length());   /* (Song: the
                                                 * step, the chain's length) */
    else if (!i) put_s(ui_hr, "HOLD");
    else put_u(put_s(put_u(ui_hr, i), "/"), n);
    ui_bat = page == PG_HOME && bat_lvl >= 0 ? bat_lvl * 2 + (bat_lvl == 0 && (fm1_ms / 500u & 1u)) : -1;   /* (the
                                                 * battery: where the tempo shows) */
    if (song_mode != SM_OFF) {                  /* a song MODE: SONG, SONG LOOP on every page (the left cut short to
                                                 * fit) */
        int32_t room = (232 - 8 * (int32_t)ui_len(SONG_MODE_NAME[song_mode]) - 8 - 21 - 8 * ui_len(ui_hb)) / 8;
        put_s(ui_hr, SONG_MODE_NAME[clamp(song_mode, 0, NSONGMODE - 1)]);
        if (room >= 0 && ui_len(ui_hl) > room) ui_hl[room] = 0;
        ui_bat = -1;                            /* (no tempo: no battery) */
    }
    {                                           /* the left fits beside the right (the battery, the tempo); a message
                                                 * that does not: the right side gives way while it shows; then cut */
        int32_t x0 = 3 + 10 + 8 * ui_len(ui_hb) + 3 + 5, rw = 8 * ui_len(ui_hr) + (ui_bat >= 0 ? UI_BAT_W + 4 : 0), room;
        int msg = fm1_ms < hd_until || sv_wait || as_err || learn_st || save_held;
        room = (232 - (rw ? rw + 8 : 0) - x0) / 8;
        if (msg && ui_len(ui_hl) > room) ui_hr[0] = 0, ui_bat = -1, room = (232 - x0) / 8;
        if (room >= 0 && ui_len(ui_hl) > room) ui_hl[room] = 0;
    }
}
static void ui_bat_draw(int32_t x, int32_t y)   /* the battery, 16 x 8: an outline, 4 bars (the lowest one, in the
                                                 * accent, blinks), or a bolt (USB power) */
{
    static const char *const BOLT[6] = {"..##", ".##.", "####", ".##.", "##..", "#..."};
    int32_t l = ui_bat / 2, i, j;
    ui_rect(x, y, 14, 8, UI_M), ui_rect(x + 1, y + 1, 12, 6, UI_K), ui_rect(x + 14, y + 2, 2, 4, UI_M);
    if (l == 4) {
        for (j = 0; j < 6; j++)
            for (i = 0; i < 4; i++)
                if (BOLT[j][i] == '#') ui_rect(x + 5 + i, y + 1 + j, 1, 1, UI_L);
        return;
    }
    for (i = 0; i <= l; i++)                    /* (level 0 .. 3: 1 .. 4 bars) */
        if (l || !(ui_bat & 1)) ui_rect(x + 2 + 3 * i, y + 2, 2, 4, l ? UI_L : ui_acc);
}
static void ui_head_draw(void)
{
    int32_t bw = 10 + 8 * ui_len(ui_hb), bx = 3 + bw + 3;
    ui_rect(3, 3, bw, 18, ui_acc);
    ui_text(8, 8, ui_hb, 1, UI_K);
    ui_rect(bx, 3, 237 - bx, 18, UI_D);
    ui_text(bx + 5, 8, ui_hl, 1, UI_L);
    ui_text(232 - 8 * ui_len(ui_hr), 8, ui_hr, 1, UI_M);
    if (ui_bat >= 0) ui_bat_draw(232 - 8 * ui_len(ui_hr) - (ui_len(ui_hr) ? 4 : 0) - UI_BAT_W, 8);
}

/* ---- the slabs: KNOB 1..4 of the page */
typedef struct { char lab[12], val[12]; uint8_t fill, acc, dim; } slab_t;
static slab_t ui_sl[4], ui_slab_shown[4];
static const char *const LONG_LAB[][2] = {
    {"RESONANCE", "RES"}, {"DETUNE", "DTUNE"}, {"DRAWBARS", "BARS"}, {"STRUCTURE", "STRUCT"}, {"SATURATION", "SAT"},
    {"DESTINATION", "DEST"}, {"PRE-DELAY", "PREDLY"}, {"FEEDBACK", "FDBK"}, {"CHORD TYPE", "CHORD"},
    {"MIDI CHANNEL", "MIDI"}, {"SCREEN OFF", "SCREEN"}, {"CATEGORY", "CAT"}, {"DIVISION", "DIV"},
    {"PLAY MODE", "PLAY"}, {"DLY SEND", "DLY"}, {"REV SEND", "REV"}, {"GRAIN SEND", "GRAIN"}, {"ATTACK", "ATK"},
    {"DECAY", "DEC"}, {"SUSTAIN", "SUS"}, {"RELEASE", "REL"}, {"DENSITY", "DENS"}, {"SCATTER", "SCAT"},
    {"REVERSE", "REV"}, {"SECTION", "SECT"}, {"PATTERN", "PATT"}, {"REPEATS", "REPEAT"}, {"OCTAVES", "OCTAVE"},
    {"RATCHET", "RATCH"}, {"VOICING", "VOICNG"}, {"CHANCE", "CHNC"}, {"ACTION", "DO"}, {"MIDI LEARN", "LEARN"},
    {"SEQ MIDI OUT", "SEQOUT"}, {"TRK CUTOFF", "TRKCUT"},
};
static void ui_label(char *d, const char *s)    /* a knob's name on a slab (at most 6 characters: 48 px) */
{
    uint32_t i;
    for (i = 0; i < sizeof LONG_LAB / sizeof LONG_LAB[0]; i++)
        if (same(s, LONG_LAB[i][0])) { put_s(d, LONG_LAB[i][1]); return; }
    for (i = 0; i < 6u && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}
static void ui_squeeze(char *d, const char *s)  /* a text value on a slab: capitals, no spaces, at most 6 characters */
{
    uint32_t n = 0;
    for (; *s && n < 6u; s++)
        if (*s != ' ') d[n++] = (char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s);
    d[n] = 0;
}
static char *ui_time(char *s, uint32_t q16)     /* ms (Q16): "5MS", "120MS", "1.5S", "12S" */
{
    uint32_t ms = (q16 + 32768u) >> 16;
    if (ms < 995u) return put_s(put_u(s, ms), "MS");
    if (ms < 9950u) {
        ms = (ms + 50u) / 100u;
        s = put_u(s, ms / 10u);
        *s++ = '.';
        *s++ = (char)('0' + ms % 10u);
        return put_s(s, "S");
    }
    return put_s(put_u(s, (ms + 500u) / 1000u), "S");
}
static void perform_slab(uint32_t k, slab_t *S)   /* PERFORM: FILT (the low-pass's cutoff; "H": the high-pass's), REV,
                                                 * ECHO (THROW on), PITCH (TAPE's semitones; BACK, STUT) */
{
    int32_t v;
    switch (k) {
    case 0:
        if (pf_hc > 0 && pf_lc >= (127 << 8)) S->val[0] = 'H', ui_3(S->val + 1, (uint32_t)(pf_hc >> 8));
        else ui_3(S->val, (uint32_t)(pf_lc >> 8));
        S->fill = (uint8_t)(((pf_hc > 0 && pf_lc >= (127 << 8) ? pf_hc : pf_lc) >> 8) * 8 / 127);
        return;
    case 1: case 2:
        v = (int32_t)(pf_held >> (k == 1u ? PF_REV : PF_ECHO) & 1u);
        put_s(S->val, v ? "ON" : "OFF"), S->fill = (uint8_t)(v * 8), S->acc = (uint8_t)v;
        return;
    default:
        if (pf_cur == PR_BACK || pf_cur == PR_STUT) { put_s(S->val, pf_cur == PR_BACK ? "BACK" : "STUT"); S->fill = 4; return; }
        v = perform_semis();
        if (v <= -99) put_s(S->val, "STOP");
        else S->val[0] = v < 0 ? '-' : '+', S->val[1] = (char)('0' + (-v) / 10 % 10), S->val[2] = (char)('0' + (-v) % 10),
             S->val[3] = 0;
        S->fill = (uint8_t)(v <= -99 ? 0 : clamp(4 + v / 12, 0, 4));
        return;
    }
}
static void lock_slab(uint32_t k, slab_t *S)    /* SEL held on SEQ's pages: the source's knob k, the selected step's
                                                 * lock (in the accent), else the knob's value */
{
    const seq_step_t *st = &seq[sel].pat[clamp(seq[sel].cur, 0, SEQ_NPAT - 1)].st[clamp(seq[sel].cursor, 0, SEQ_N - 1)];
    const engine_t *E = sel_engine();
    int32_t pv[4], j, mx = E->max[k];
    char t[32];
    for (j = 0; j < 4; j++) pv[j] = st->lmask >> j & 1u ? st->lv[j] : trk[sel].p[sel_eng()][j];
    ui_label(S->lab, E->knob[k]);
    S->fill = (uint8_t)((clamp(pv[k], 0, mx) * 8 + mx / 2) / (mx ? mx : 1));
    S->acc = (uint8_t)(st->lmask >> k & 1u);
    if (mx <= 24) { E->text(pv, k, t); ui_squeeze(S->val, t); }
    else ui_3(S->val, ui_pc(pv[k]));
}
static char *fm_ratio_text(char *s, uint32_t i)   /* operator i's ratio, RATIO (0: 0.5) times 1 + FINE / 100: "2.00",
                                                 * "14.1" */
{
    uint32_t c = (uint32_t)clamp(trk[sel].fmo[i][FO_RATIO], 0, 31), r = (c ? c * 100u : 50u) *
                 (100u + (uint32_t)clamp(trk[sel].fmo[i][FO_FINE], 0, 99)) / 100u;
    s = put_u(s, r / 100u);
    *s++ = '.';
    if (r >= 1000u) { *s++ = (char)('0' + r / 10u % 10u); *s = 0; return s; }
    *s++ = (char)('0' + r / 10u % 10u), *s++ = (char)('0' + r % 10u), *s = 0;
    return s;
}
static void ui_slab_make(uint32_t k, slab_t *S)
{
    volatile int32_t *p = knob_param(page, k);
    const char *name = knob_name(page, k);
    int32_t v = p ? *p : 0, mx = knob_max(page, k), md = (int32_t)phys_model(trk[sel].p[ENG_PHYS]);
    uint32_t eng = sel_eng();
    char t[32];
    S->acc = S->dim = 0;
    S->val[0] = 0;
    ui_label(S->lab, name[0] ? name : "-");
    if (page == PG_GRID && k == 3u) put_s(S->lab, "LEN");   /* (Seq's; SAMPLE keeps its LENGTH) */
    if (!PAGES[page].built) {                   /* (named, nothing behind it yet) */
        S->fill = 0;
        S->dim = 1;
        if (name[0]) put_s(S->val, "---");
        return;
    }
    if (page == PG_PERFORM) { perform_slab(k, S); return; }   /* (what the tricks do, no knobs) */
    if (PAGES[page].btn == BTN_SEQ && sel_held) { S->dim = 0; lock_slab(k, S); return; }   /* (the step's locks) */
    if (!p) { S->fill = 0; if (page == PG_PROJECTS) put_s(S->val, "-"); return; }   /* ("-": no knob there) */
    S->fill = (uint8_t)((clamp(v, 0, mx) * 8 + mx / 2) / (mx ? mx : 1));
    if (page == PG_GRID && k == 0u && v) S->fill = (uint8_t)(1 + (v - 1) * 7 / 126);   /* (a note: at least 1) */
    switch (page) {
    case PG_SOUND:
        if (eng == ENG_PHYS && k == 1u) put_s(S->lab, md == PM_MEMB ? "HARM" : md == PM_SYMP ? "CHORD" : "STRUCT");
        if (eng == ENG_PHYS && k == 3u) put_s(S->lab, md == PM_MEMB ? "BEND" : md == PM_SYMP ? "BUZZ" : "BOW");
        if (eng == ENG_SAMPLE && k == 0u) { put_u(S->val, (uint32_t)v + 1u); return; }   /* (the name: the caption) */
        if (eng == ENG_VA && k == 0u) { put_s(S->val, v == WAVE_SUPER ? "SUPER" : v == WAVE_SQR ? "SQR" : "SAW"); return; }
        if (eng == ENG_PHYS && k == 0u) { put_s(S->val, md == PM_MEMB ? "MEMB" : md == PM_SYMP ? "SYMP" : N_PHYS_MODEL[md]); return; }
        if (mx <= 24 || (eng == ENG_SAMPLE && k == 2u) || (eng == ENG_VOICE && k == 0u) ||
            (eng == ENG_PHYS && k == 1u && md == PM_SYMP)) {   /* (the source's own words) */
            sel_engine()->text(trk[sel].p[eng], k, t);
            ui_squeeze(S->val, t);
            return;
        }
        ui_3(S->val, ui_pc(v));
        return;
    case PG_SOURCE: put_s(S->val, k ? (v ? "ON" : "OFF") : ENGINES[clamp(v, 0, NENG - 1)]->name); return;
    case PG_SYSTEM:                             /* COLOUR, MIDI ("ALL": ALL > SEL), TUNE, SCREEN OFF ("NVR": full) */
        if (k == 0u) S->fill = (uint8_t)(2 * v + 2), put_s(S->val, COL_NAME[clamp(v, 0, 3)]);
        else if (k == 1u) { if (v >= MCH_ALL) put_s(S->val, "ALL"); else mch_text(S->val, v); }
        else if (k == 2u) S->fill = (uint8_t)((clamp(v, -50, 50) + 50) * 8 / 100), tune_text(S->val, v);
        else if (v) S->fill = (uint8_t)(2 * v), put_s(put_u(S->val, SCR_S[clamp(v, 0, 4)] / 60u), "M");
        else S->fill = 8, put_s(S->val, "NVR");
        return;
    case PG_SYS2: put_s(S->val, k == 2u ? (v ? "RESTORE" : "KEEP") : k ? (v ? "ON" : "OFF") : v == LEARN_CLEAR ? "CLEAR" :
                        LEARN_NAME[clamp(v, 0, 2)]); return;
    case PG_SOUNDS:
        if (k == 0u) { ui_3(S->val, (uint32_t)v + 1u); return; }
        if (k == 1u) { ui_squeeze(S->val, CAT_NAME[clamp(v, 0, NCAT - 1)]); return; }
        if (k == 2u) { put_s(S->val, v ? ENGINES[clamp(v - 1, 0, NENG - 1)]->name : "ALL"); return; }
        put_s(S->val, SV_ACT[clamp(v, 0, 2)]);
        return;
    case PG_SCALE: put_s(S->val, k ? SC_SHORT[clamp(v, 0, NSCALE - 1)] : SC_NOTE[clamp(v, 0, 11)]); return;
    case PG_SONG:
        if (k == 0u || k == 2u) {               /* SECTION, REPEATS: "03" */
            v = clamp(v + (k ? 0 : 1), 1, 16);
            S->val[0] = (char)('0' + v / 10), S->val[1] = (char)('0' + v % 10), S->val[2] = 0;
        } else if (k == 1u) {                   /* PATTERN: A..D, END */
            if (v >= SONG_END) put_s(S->val, "END");
            else S->val[0] = (char)('A' + clamp(v, 0, 3)), S->val[1] = 0;
        } else
            put_s(S->val, SONG_MODE_SHORT[clamp(v, 0, NSONGMODE - 1)]);
        return;
    case PG_LFO:
        if (k == 0u) lfo_rate_text(S->val, v, "");
        else if (k == 1u) ui_3(S->val, (uint32_t)clamp(v, 0, 100));
        else if (k == 2u) ui_label(S->val, lfo_dest_name(v));
        else put_s(S->val, LFO_SHAPE_SHORT[clamp(v, 0, NLFOSHAPE - 1)]);
        return;
    case PG_ARP:
        if (k == 0u) put_s(S->val, ARP_MODE_SHORT[clamp(v, 0, NARPMODE - 1)]);
        else if (k == 1u) put_s(S->val, ARP_RATE_NAME[clamp(v, 0, NARPRATE - 1)]);
        else if (k == 2u) put_u(S->val, (uint32_t)clamp(v, 1, 4));
        else ui_3(S->val, (uint32_t)clamp(v, 10, 100));
        return;
    case PG_CHORD: put_s(S->val, k ? VC_NAME[clamp(v, 0, NVOICING - 1)] : CH_SHORT[clamp(v, 0, NCHORD - 1)]); return;
    case PG_PROJECTS:
        if (k == 0u) { S->val[0] = (char)('A' + (v & 3)), S->val[1] = 0; return; }
        put_s(S->val, PJ_ACT[clamp(v, 0, 3)]);
        return;
    case PG_GRID:
        if (k == 0u) { if (v) ui_note(S->val, (uint32_t)v); else put_s(S->val, "OFF"); return; }
        put_u(S->val, (uint32_t)v);
        return;
    case PG_PATTERN:
        if (k == 1u) { put_s(S->val, SEQ_DIV_NAME[clamp(v, 0, 7)]); return; }
        if (k == 3u) { put_s(S->val, SEQ_MODE_SHORT[clamp(v, 0, SEQ_NMODE - 1)]); return; }
        if (k == 0u) { put_u(S->val, (uint32_t)v); return; }
        break;
    case PG_ENV:
        if (k == 2u) { ui_3(S->val, ui_pc(v)); return; }
        ui_time(S->val, k == 0u ? pow2_q16(v * 20) : k == 1u ? 10u * pow2_q16(v * 15) : 10u * pow2_q16((v * 33) >> 1));
        return;
    case PG_FMALGO:                             /* ALGO 01..32, FEEDBACK, OP 1..6, FINE (the operator's ratio now) */
        if (k == 0u) { S->val[0] = (char)('0' + (v + 1) / 10), S->val[1] = (char)('0' + (v + 1) % 10), S->val[2] = 0; return; }
        if (k < 3u) { put_u(S->val, (uint32_t)v + (k == 2u ? 1u : 0u)); return; }
        fm_ratio_text(S->val, (uint32_t)clamp(fm_op, 0, 5));
        return;
    case PG_FMOP:                               /* RATIO (with FINE), LEVEL, ATTACK, DECAY 0..99 */
        if (k == 0u) fm_ratio_text(S->val, (uint32_t)clamp(fm_op, 0, 5));
        else ui_3(S->val, (uint32_t)clamp(v, 0, 99));
        return;
    case PG_HOME:                               /* the four tracks (the knobs are the selected one's: VOLUME, CUTOFF,
                                                 * RESONANCE, FX; the header says what they change): "T2 FM", its LEVEL;
                                                 * the selected track in the accent; MUTE */
        put_s(put_s(put_u(put_s(S->lab, "T"), k + 1u), " "), SRC_CODE[clamp(trk[k].eng, 0, NENG - 1)]);
        S->fill = (uint8_t)((clamp(trk[k].level, 0, 127) * 8 + 63) / 127);
        ui_3(S->val, ui_pc(trk[k].level));
        if (trk_quiet >> k & 1u) { put_s(S->val, "MUTE"); S->dim = 1; }
        S->acc = k == sel;
        return;
    case PG_MIX2:
        if (v == 64) put_s(S->val, "C");
        else put_u(put_s(S->val, v < 64 ? "L" : "R"), (uint32_t)(v < 64 ? 64 - v : v - 64));
        return;
    case PG_GRAIN:
        if (k == 0u) { put_s(put_u(S->val, (pow2_q16(clamp(v, 0, 127) * 8) * 10u + 32768u) >> 16), "MS"); return; }
        if (k == 1u) { uint32_t i = gr_iv(v); put_s(put_u(S->val, (44100u + i / 2u) / (i ? i : 1u)), "/S"); return; }
        break;
    case PG_DELAY:
        if (k == 0u) { ui_time(S->val, DL_MS[clamp(v, 0, 127)] << 16); return; }
        if (k == 1u) { ui_3(S->val, ((uint32_t)v * 110u + 63u) / 127u); return; }
        break;
    case PG_SPACE:
        if (k == 0u) {
            if (v >= RV_INF) put_s(S->val, "INF");
            else { put_x10(S->val, RV_RT60_X10[clamp(v, 0, 126)]); put_s(S->val + ui_len(S->val), "S"); }
            return;
        }
        if (k == 3u) { put_s(put_u(S->val, ((uint32_t)v * 250u + 63u) / 127u), "MS"); return; }
        break;
    default: break;
    }
    ui_3(S->val, ui_pc(v));
}
static void ui_slab_draw(void)
{
    const slab_t *S = &ui_sl[ui_k];
    int32_t x = 3 + (int32_t)ui_k * 59, j, n = ui_len(S->val);
    uint16_t fc = S->acc ? ui_acc : UI_L;
    ui_rect(x, 165, 56, 72, UI_D);
    ui_text(x + 4, 170, S->lab, 1, UI_L);
    for (j = 0; j < 8; j++)                     /* 8 segments: light (the accent) filled, black empty */
        ui_rect(x + 4 + j * 6, 190, 5, 14, j < S->fill ? fc : UI_K);
    if (n <= 3) ui_text(x + 4, 214, S->val, 2, S->dim ? UI_M : S->acc ? ui_acc : UI_L);
    else ui_text(x + 4, 222, S->val, 1, S->dim ? UI_M : UI_L);
}

/* ---- the pictures */
static void ui_caption(const char *l, const char *r)
{
    ui_text(PX0, 31, l, 1, UI_M);
    if (r) ui_text(PX0 + 216 - 8 * ui_len(r), 31, r, 1, UI_M);
}
static const char *const EYE[8] = {"....llllllll....", "..llmmmmmmmmll..", ".lmmmmaaaammmml.", "lmmmmaawkaammmml",
                                   "lmmmmaakkaammmml", ".lmmmmaaaammmml.", "..llmmmmmmmmll..", "....llllllll...."};
static uint16_t ui_col(char ch)
{
    switch (ch) {
    case 'k': return UI_K;
    case 'd': return UI_D;
    case 'm': return UI_M;
    case 'l': return UI_L;
    case 'w': return UI_W;
    default: return ui_acc;
    }
}
static void ui_sprite(int32_t x, int32_t y, int32_t s, const char *const *rows, int32_t nr)   /* '.': none */
{
    int32_t r, c;
    for (r = 0; r < nr; r++)
        for (c = 0; rows[r][c]; c++)
            if (rows[r][c] != '.') ui_rect(x + c * s, y + r * s, s, s, ui_col(rows[r][c]));
}
static int ui_in(int32_t x, int32_t y, int32_t w, int32_t h)   /* does (x, y, w, h) reach into the region being drawn */
{
    return x < ui_ox + (int32_t)cv_w && x + w > ui_ox && y < -cv_oy + (int32_t)cv_h && y + h > -cv_oy;
}
/* the eye at (x, y), 5 px a cell: its lids k open (4: wide, 0: shut; the lid's edge a light row), the iris dx, dy cells
 * off the middle (inside the white only) */
static void ui_eye(int32_t x, int32_t y, int32_t k, int32_t dx, int32_t dy)
{
    static const char *const IRIS[4] = {".aaaa.", "aawkaa", "aakkaa", ".aaaa."};
    int32_t r, c;
    if (!ui_in(x, y, 80, 40)) return;
    for (r = 0; r < 8; r++) {
        int32_t d = r < 4 ? 3 - r : r - 4;      /* (rows from the middle) */
        for (c = 0; c < 16; c++) {
            char ch = EYE[r][c];
            if (ch == '.' || d > k) continue;
            if (d == k && k < 4) ch = 'l';
            else if (ch != 'l') {
                int32_t ir = r - 2 - dy, ic = c - 5 - dx;
                ch = ir >= 0 && ir < 4 && ic >= 0 && ic < 6 && IRIS[ir][ic] != '.' ? IRIS[ir][ic] : 'm';
            }
            ui_rect(x + c * 5, y + r * 5, 5, 5, ui_col(ch));
        }
    }
}

/* ---- HOME's moving picture (Main), a frame every UI_ANIM_MS while it is shown and the screen lit, from the main loop:
 * what the audio ISR left in vis_pk, vis_lr (its peaks) and the notes on (their pitch). Only what changed is drawn.
 *   candles: LEVEL the most a flame can be (muted, LEVEL 0: out, a wisp of smoke); a silent track's burns low, a shape
 *   now and then; a sounding one's grows with its peak, dances (a row up or down) and flickers (three shapes, uneven)
 *   stars: the fog's pixels, a few at a time brighten to light and fade back; more of them the louder it is
 *   the eye: its iris goes towards the sound (left, right: the output's balance; up, down: high, low notes), a cell at
 *   a time; it blinks now and then, and at a sudden loud hit; silent ~4 s, it half-closes and drifts (dozing); the
 *   first sound wakes it with a blink */
#define NSTAR 46u
static uint8_t sk_x[NSTAR], sk_y[NSTAR], sk_b[NSTAR], sk_ph[NSTAR], sk_ok;   /* a star: where, M (else D), its sparkle's
                                                 * frame (0: none) */
static uint8_t cn_h[NT], cn_s[NT], cn_n[NT];    /* a candle: its flame's rows (0: out), shape, frames to its next shape */
static int32_t ey_k = 4, ey_dx, ey_dy, ey_tx, ey_ty, ey_bl, ey_doze;   /* the eye: lids, iris, its target, the blink's
                                                 * frame (0: none), dozing */
static uint32_t an_rng = 0x2545F491u, an_e[NT], an_avg, an_n, an_sound_t, an_blink_t, an_next, an_drift_t;
static uint32_t an_rnd(uint32_t n)
{
    an_rng ^= an_rng << 13, an_rng ^= an_rng >> 17, an_rng ^= an_rng << 5;
    return an_rng % n;
}
static uint16_t sk_col(uint32_t i)              /* a star's colour now: D or M; sparkling, M, L, M */
{
    uint32_t p = sk_ph[i];
    return !p ? (sk_b[i] ? UI_M : UI_D) : p >= 4u && p <= 9u ? UI_L : UI_M;
}
static void sk_init(void)                       /* the stars where Main has its fog (the concepts' random, seed 7) */
{
    uint32_t i;
    ui_rnd_s = 7;
    for (i = 0; i < NSTAR; i++) {
        sk_x[i] = (uint8_t)ui_rnd(79), sk_y[i] = (uint8_t)ui_rnd(44);
        sk_b[i] = ui_rnd(100) >= 65u;
    }
    sk_ok = 1;
}
static void pic_home(void)                      /* Main: the stars, the eye, a candle a track */
{
    static const char *const FLAME[3][4] = {{"..aa..", ".aaaa.", ".alla.", "..la.."}, {"...a..", "..aaa.", ".alla.", "..la.."},
                                            {"..a...", ".aaa..", ".alla.", "..la.."}};
    static const char *const BODY[9] = {"...m..", ".wlll.", ".wlll.", ".llll.", ".llll.", "lllll.", ".llll.", ".llll.",
                                        "dddddd"};
    uint32_t i;
    int32_t t;
    if (!sk_ok) sk_init();
    for (i = 0; i < NSTAR; i++) ui_rect(sk_x[i] * 3, 27 + sk_y[i] * 3, 3, 3, sk_col(i));
    ui_eye(80, 39, ey_k, ey_dx, ey_dy);
    for (t = 0; t < (int32_t)NT; t++) {         /* four candles, 27 px apart, centred */
        int32_t x = 43 + t * 45, y = 92, h = cn_h[t], r;
        const char *f[4];
        char lab[2] = {(char)('1' + t), 0};
        if (!ui_in(x, y, 18, 52)) continue;
        for (r = 0; r < 4; r++) f[r] = r >= 4 - h ? FLAME[cn_s[t] % 3u][r] : "......";
        if (!h) f[1] = "..m...", f[2] = "...m..";   /* (out: a wisp of smoke) */
        ui_sprite(x, y, 3, f, 4);
        ui_sprite(x, y + 12, 3, BODY, 9);
        ui_text(x + 5, y + 43, lab, 1, UI_M);
    }
}
static void ui_pic_draw(void);
static void ui_home_part(int32_t x, int32_t y, int32_t w, int32_t h) { ui_region(x, y, w, h, UI_K, ui_pic_draw); }
static void ui_home_frame(int draw)             /* a frame of HOME's picture (draw: what changed, drawn) */
{
    static const int8_t BLINK[6] = {4, 2, 0, 0, 2, 3};   /* (a blink's lids, a frame each) */
    uint32_t pk[NT], l = vis_lr[0], r = vis_lr[1], loud, t, i, sum = 0, n = 0, sound;
    int32_t k, dx, dy;
    an_n++;
    for (t = 0; t < NT; t++) pk[t] = vis_pk[t], vis_pk[t] = 0;
    vis_lr[0] = vis_lr[1] = 0;
    loud = (l > r ? l : r) >> 7;                /* (0 .. 256: the output's peak, full scale) */
    loud = loud > 256u ? 256u : loud;
    for (i = 0; i < nlat && i < NV; i++) sum += lat_note[i], n++;
    sound = loud > 2u || n;
    if (!sk_ok) sk_init();

    for (t = 0; t < NT; t++) {                  /* the candles */
        uint32_t e = pk[t] / 96u, lv = trk[t].level <= 0 || (trk_quiet >> t & 1u) ? 0u : (ui_pc(trk[t].level) + 24u) / 25u;
        uint32_t h, s = cn_s[t], lo = lv < 2u ? lv : 2u;
        e = e > 256u ? 256u : e;
        an_e[t] = e >= an_e[t] ? e : an_e[t] - (an_e[t] + 5u) / 6u;   /* (up at once, down over ~0.3 s) */
        if (an_e[t] < 6u) h = lo;               /* (silent: low) */
        else {
            h = lo + ((lv - lo) * an_e[t] + 128u) / 256u;
            if (!an_rnd(3) && h < lv) h++;      /* (the dance) */
            else if (!an_rnd(4) && h > lo) h--;
        }
        if (!cn_n[t] || !--cn_n[t]) {           /* (a new shape, at uneven times: sounding often, silent seldom) */
            s = (s + 1u + an_rnd(2)) % 3u;
            cn_n[t] = (uint8_t)(an_e[t] >= 6u ? 1u + an_rnd(3) : 6u + an_rnd(14));
        }
        if (h != cn_h[t] || (s != cn_s[t] && h > 2u)) {
            cn_h[t] = (uint8_t)h, cn_s[t] = (uint8_t)s;
            if (draw) ui_home_part(43 + (int32_t)t * 45, 92, 18, 12);
        }
        cn_s[t] = (uint8_t)s;
    }

    for (i = 0; i < NSTAR; i++)                 /* the stars: the sparkling ones on; new ones (louder: more) */
        if (sk_ph[i]) {
            uint16_t c = sk_col(i);
            sk_ph[i] = sk_ph[i] >= 12u ? 0u : (uint8_t)(sk_ph[i] + 1u);
            if (draw && sk_col(i) != c) ui_home_part(sk_x[i] * 3, 27 + sk_y[i] * 3, 3, 3);
        }
    for (t = 0; t < 1u + loud / 96u; t++)
        if (an_rnd(64) < 3u + loud / 12u) {
            i = an_rnd(NSTAR);
            if (!sk_ph[i]) sk_ph[i] = 1;
        }

    if (sound) {                                /* the eye: awake (the first sound: a blink), or dozing */
        if (ey_doze) ey_doze = 0, ey_bl = 1;
        an_sound_t = fm1_ms;
    } else if (fm1_ms - an_sound_t > 4000u)
        ey_doze = 1;
    if (!ey_doze) {
        ey_tx = l + r > 64u ? ((int32_t)r - (int32_t)l) * 3 / (int32_t)(l + r) : 0;   /* (the balance: -3 .. 3) */
        ey_ty = !n ? 0 : sum / n >= 72u ? -1 : sum / n < 50u ? 1 : 0;   /* (high notes up, low down) */
        if (loud > 48u && loud > 3u * an_avg + 24u && fm1_ms - an_blink_t > 600u) ey_bl = 1;   /* (a sudden hit) */
    } else if (fm1_ms >= an_drift_t) {          /* (dozing: somewhere else now and then) */
        ey_tx = (int32_t)an_rnd(5) - 2, ey_ty = (int32_t)an_rnd(2);
        an_drift_t = fm1_ms + 1200u + an_rnd(1500);
    }
    an_avg = (an_avg * 7u + loud) / 8u;
    if (!ey_bl && fm1_ms >= an_next) ey_bl = 1; /* (now and then) */
    if (ey_bl) {
        if (ey_bl == 1) an_next = fm1_ms + 2500u + an_rnd(5000);
        an_blink_t = fm1_ms;
        k = ey_bl < 6 ? BLINK[ey_bl] : ey_doze ? 2 : 4;
        ey_bl = ey_bl >= 6 ? 0 : ey_bl + 1;
    } else
        k = ey_doze ? (ey_k > 2 && !(an_n % 9u) ? ey_k - 1 : ey_k < 2 ? ey_k + 1 : ey_k) : ey_k < 4 ? ey_k + 1 : 4;   /* (dozing:
                                                 * the lids down slowly) */
    dx = ey_dx, dy = ey_dy;
    if (!(an_n % (ey_doze ? 5u : 2u))) {        /* (the iris: a cell at a time) */
        dx += ey_tx > dx ? 1 : ey_tx < dx ? -1 : 0;
        dy += ey_ty > dy ? 1 : ey_ty < dy ? -1 : 0;
    }
    if (k != ey_k || dx != ey_dx || dy != ey_dy) {
        ey_k = k, ey_dx = dx, ey_dy = dy;
        if (draw) ui_home_part(80, 39, 80, 40);
    }
}
static int32_t ui_oct;                          /* (the octave: SEL's keys) */

static int32_t va_saw(int32_t x, int32_t y, int32_t p, int32_t o)   /* EditVA's saw */
{
    int32_t ph = (x + o) % p, v = 26 - ph * 23 / p;
    return y == v || (ph == 0 && y >= 3 && y <= 26);
}
static int32_t va_sqr(int32_t x, int32_t y, int32_t p, int32_t o)
{
    int32_t ph = (x + o) % p, v = ph < p / 2 ? 4 : 25;
    return y == v || ((ph == 0 || ph == p / 2) && y >= 4 && y <= 25);
}
static void pic_va(void)                        /* EditVA: the waveform of WAVE (SUPERSAW: three detuned saws) */
{
    const volatile int32_t *p = trk[sel].p[ENG_VA];
    int32_t x, y, w = p[0], dt = clamp(p[1], 0, 127) / 16;
    ui_caption(w == WAVE_SUPER ? "SUPERSAW X7" : w == WAVE_SQR ? "SQUARE X2" : "SAW X2", 0);
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            uint16_t c = 0;
            if (y == 29 && !(x & 1)) c = UI_D;
            if (w == WAVE_SUPER) {
                if (va_saw(x, y, 19, 11)) c = UI_D;
                if (va_saw(x, y, 17, 5)) c = UI_M;
                if (va_saw(x, y, 18, 0)) c = UI_L;
            } else if (w == WAVE_SQR) {
                if (va_sqr(x, y, 18 + dt, 3)) c = UI_M;
                if (va_sqr(x, y, 18, 0)) c = UI_L;
            } else {
                if (va_saw(x, y, 18 + dt, 3)) c = UI_M;
                if (va_saw(x, y, 18, 0)) c = UI_L;
            }
            if (c) ui_cell(x, y, c);
        }
}

/* EditFM: an algorithm's operators as wired (FM6_ALG, the core's own table): a modulator a dark block above what it
 * modulates (a line to each), a carrier an accent frame on the OUT line, the operator sel (-1: none) light / filled,
 * the feedback a hook on the operator it comes into */
static void fm_wiring(uint32_t alg, uint8_t *tg, uint32_t *car, int32_t *fbin, int32_t *fbout)
{
    uint32_t bus[3] = {0, 0, 0}, k, j;
    *car = 0, *fbin = *fbout = -1;
    for (k = 0; k < 6u; k++) {                  /* (the sixth first, as the core runs them) */
        uint32_t f = FM6_ALG[alg & 31u][k], o = 5u - k, ib = f >> 4 & 3u, ob = f & 3u;
        tg[o] = 0;
        if (ib)
            for (j = 0; j < 6u; j++)
                if (bus[ib] >> j & 1u) tg[j] |= (uint8_t)(1u << o);   /* (j modulates o) */
        if (f & FM6_FBIN) *fbin = (int32_t)o;
        if (f & FM6_FBOUT) *fbout = (int32_t)o;
        if (ob) bus[ob] = (f & FM6_OADD ? bus[ob] : 0u) | 1u << o;
        else *car |= 1u << o;
    }
}
static int32_t fm_slot;
static void fm_place(const uint8_t *tg, int32_t *x2, uint32_t o)   /* operator o and what it is the first target of:
                                                 * their columns (x2: half columns) */
{
    uint32_t j;
    int32_t lo = 999, hi = -1;
    for (j = 0; j < 6u; j++) {
        uint32_t first = 0;
        while (first < 6u && !(tg[j] >> first & 1u)) first++;
        if (first != o || x2[j] >= 0) continue;
        fm_place(tg, x2, j);
        lo = x2[j] < lo ? x2[j] : lo, hi = x2[j] > hi ? x2[j] : hi;
    }
    x2[o] = hi < 0 ? 2 * fm_slot++ : (lo + hi) / 2;
}
static void fm_algo_draw(uint32_t alg, int32_t sel_op)
{
    uint8_t tg[6];
    uint32_t car, o, j, r;
    int32_t fbin, fbout, x2[6], dep[6], w, bw, lo = 999, hi = -1;
    fm_wiring(alg, tg, &car, &fbin, &fbout);
    for (o = 0; o < 6u; o++) x2[o] = -1, dep[o] = 0;
    for (r = 0; r < 6u; r++)                    /* (a modulator a row above the highest it modulates) */
        for (o = 0; o < 6u; o++)
            for (j = 0; j < 6u; j++)
                if (tg[o] >> j & 1u && dep[j] + 1 > dep[o]) dep[o] = dep[j] + 1;
    fm_slot = 0;
    for (o = 0; o < 6u; o++)
        if (car >> o & 1u) fm_place(tg, x2, o);
    for (o = 0; o < 6u; o++) {                  /* (one over several: between them) */
        int32_t a = 999, b = -1, n = 0;
        for (j = 0; j < 6u; j++)
            if (tg[o] >> j & 1u) a = x2[j] < a ? x2[j] : a, b = x2[j] > b ? x2[j] : b, n++;
        if (n > 1) x2[o] = (a + b) / 2;
        if (x2[o] < 0) x2[o] = 2 * fm_slot++;
    }
    w = 216 / (fm_slot ? fm_slot : 1), bw = w - 6 < 28 ? w - 6 : 28;
#define FMX(o) (PX0 + w / 2 + x2[o] * w / 2)
#define FMY(o) (106 - dep[o] * 22)
    for (o = 0; o < 6u; o++)                    /* the lines: down, across, down */
        for (j = 0; j < 6u; j++)
            if (tg[o] >> j & 1u) {
                int32_t xa = FMX(o), xb = FMX(j), ym = FMY(j) - 4, xl = xa < xb ? xa : xb;
                ui_rect(xa - 1, FMY(o) + 14, 2, ym - FMY(o) - 14, UI_M);
                ui_rect(xl - 1, ym, (xa > xb ? xa - xb : xb - xa) + 2, 2, UI_M);
                ui_rect(xb - 1, ym, 2, 4, UI_M);
            }
    for (o = 0; o < 6u; o++) {
        int32_t x = FMX(o) - bw / 2, y = FMY(o), c = (int32_t)(car >> o & 1u), s = (int32_t)o == sel_op;
        char n[2] = {(char)('1' + o), 0};
        if (c) {
            lo = FMX(o) < lo ? FMX(o) : lo, hi = FMX(o) > hi ? FMX(o) : hi;
            ui_rect(FMX(o) - 1, y + 14, 2, 8, UI_L);
            ui_rect(x, y, bw, 14, ui_acc);
            if (!s) ui_rect(x + 2, y + 2, bw - 4, 10, UI_K);
        } else
            ui_rect(x, y, bw, 14, s ? UI_L : UI_D);
        ui_text(FMX(o) - 4, y + 3, n, 1, s ? UI_K : c ? ui_acc : UI_L);
        if ((int32_t)o == fbin) {               /* (the feedback: a hook on its right) */
            ui_rect(x + bw, y + 6, 4, 2, UI_M);
            ui_rect(x + bw + 2, y - 4, 2, 10, UI_M);
            ui_rect(FMX(o), y - 4, x + bw + 4 - FMX(o), 2, UI_M);
            ui_rect(FMX(o), y - 4, 2, 4, UI_M);
        }
    }
    ui_rect(lo - 1, 128, hi - lo + 2, 2, UI_L); /* OUT */
    ui_text(120 - 12, 133, "OUT", 1, UI_M);
    ui_rect(PX0, 150, 7, 7, UI_D);              /* (the key) */
    ui_text(PX0 + 11, 149, "MODULATOR", 1, UI_M);
    ui_rect(PX0 + 100, 150, 7, 7, ui_acc), ui_rect(PX0 + 102, 152, 3, 3, UI_K);
    ui_text(PX0 + 111, 149, "CARRIER", 1, UI_M);
#undef FMX
#undef FMY
}
static void fm_algo_caption(const char *r)
{
    char s[24];
    put_u(put_s(s, "ALGORITHM "), (uint32_t)clamp(trk[sel].fm_alg, 0, 31) + 1u);
    ui_caption(s, r);
}
static void pic_fmalgo(void)                    /* EditFM (FM ALGO): the track's algorithm, the operator OP selects */
{
    uint8_t tg[6];
    uint32_t car;
    int32_t fbin, fbout;
    char s[24], *e;
    fm_wiring((uint32_t)clamp(trk[sel].fm_alg, 0, 31), tg, &car, &fbin, &fbout);
    if (!trk[sel].fm_fb || fbin < 0) put_s(s, "NO FB");
    else {
        e = put_u(put_s(s, "FB "), (uint32_t)clamp(trk[sel].fm_fb, 0, 7));   /* ("FB 3 ON OP6", "FB 3 OP4>OP6") */
        if (fbout >= 0 && fbout != fbin) e = put_s(put_u(put_s(e, " OP"), (uint32_t)fbout + 1u), ">OP");
        else e = put_s(e, " ON OP");
        put_u(e, (uint32_t)fbin + 1u);
    }
    fm_algo_caption(s);
    fm_algo_draw((uint32_t)clamp(trk[sel].fm_alg, 0, 31), clamp(fm_op, 0, 5));
}
static void pic_fmop(void)                      /* EditFM (FM OP): the operator's envelope (ATTACK, DECAY; the rest the
                                                 * PATCH's), scaled by LEVEL; its ratio */
{
    const uint8_t *op = FM_PATCH[clamp(trk[sel].p[ENG_FM][0], 0, FM_NPATCH - 1)] + (5u - (uint32_t)clamp(fm_op, 0, 5)) * FP_OP;
    const volatile int32_t *f = trk[sel].fmo[clamp(fm_op, 0, 5)];
    uint8_t tg[6];
    uint32_t car;
    int32_t fbin, fbout, lv = clamp(f[FO_LEVEL], 0, 99), wa = 2 + clamp(f[FO_ATK], 0, 99) * 16 / 99;
    int32_t wd = 2 + clamp(f[FO_DEC], 0, 99) * 14 / 99, wr = 2 + (99 - op[FP_R1 + 3] % 100) * 12 / 99, ws = 53 - wa - wd - wr;
    int32_t y1 = 28 - op[FP_L1] % 100 * 25 * lv / 9801, y2 = 28 - op[FP_L1 + 2] % 100 * 25 * lv / 9801;
    int32_t y4 = 28 - op[FP_L1 + 3] % 100 * 25 * lv / 9801, x, y, ev[55];
    char s[24];
    fm_wiring((uint32_t)clamp(trk[sel].fm_alg, 0, 31), tg, &car, &fbin, &fbout);
    fm_ratio_text(put_s(put_u(put_s(s, "OP "), (uint32_t)clamp(fm_op, 0, 5) + 1u), " RATIO "), (uint32_t)clamp(fm_op, 0, 5));
    ui_caption(s, car >> clamp(fm_op, 0, 5) & 1u ? "CARRIER" : "MODULATOR");
    for (x = 0; x <= 54; x++) {                 /* (the level a column: 28 none, 3 full) */
        if (x <= wa) ev[x] = 28 - (28 - y1) * x / wa;
        else if (x <= wa + wd) ev[x] = y1 + (y2 - y1) * (x - wa) / wd;
        else if (x <= wa + wd + ws) ev[x] = y2;
        else ev[x] = y2 + (y4 - y2) * (x - wa - wd - ws) / wr;
        ev[x] = clamp(ev[x], 3, 28);
    }
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            int32_t n = x < 53 ? ev[x + 1] : ev[x], a = ev[x] < n ? ev[x] : n, b = ev[x] < n ? n : ev[x];
            uint16_t c = 0;
            if (y > ev[x] && y <= 28 && !((x + y) & 1)) c = UI_D;
            if (y >= a && y <= b) c = UI_L;
            if (y == 29 && !(x & 1)) c = UI_D;
            if (c) ui_cell(x, y, c);
        }
    ui_cell(wa - 1, y1, ui_acc), ui_cell(wa, y1, ui_acc);
}
static void pic_fm(void)                        /* EditFM (SOUND): the track's algorithm as wired, the PATCH it is from */
{
    uint32_t pt = (uint32_t)clamp(trk[sel].p[ENG_FM][0], 0, FM_NPATCH - 1);
    fm_algo_caption(FM_PATCH_NAME[pt]);
    fm_algo_draw((uint32_t)clamp(trk[sel].fm_alg, 0, 31), -1);
}
static uint8_t smp_pk[SMP_SLOTS][54];           /* a slot's peaks, a column of the picture each (0..13) */
static const uint8_t *smp_pk_data[SMP_SLOTS];
static uint32_t smp_pk_n[SMP_SLOTS];
static void smp_peaks(uint32_t slot)            /* (decoded once a slot, when it is first shown) */
{
    const smp_slot_t *s = &smp[slot];
    uint32_t i, col = 0, per;
    int32_t pred = 0, idx = 0, pk = 0;
    if (smp_pk_data[slot] == s->data && smp_pk_n[slot] == s->n) return;
    per = s->n / 54u + 1u;
    for (i = 0; i < s->n && col < 54u; i++) {
        int32_t y = ima_dec(s->data, i, &pred, &idx);
        y = y < 0 ? -y : y;
        pk = y > pk ? y : pk;
        if ((i + 1u) % per == 0u) {
            smp_pk[slot][col++] = (uint8_t)((pk * 13 + 16383) >> 15);
            pk = 0;
        }
    }
    while (col < 54u) smp_pk[slot][col++] = (uint8_t)((pk * 13 + 16383) >> 15), pk = 0;
    smp_pk_data[slot] = s->data;
    smp_pk_n[slot] = s->n;
}
static void pic_sample(void)                    /* the slot's sample, the loop (START, LENGTH) marked */
{
    const volatile int32_t *p = trk[sel].p[ENG_SAMPLE];
    uint32_t slot = sp_slot(p);
    sp_loop_t L;
    char s[32], r[12];
    int32_t x, y, x0, x1;
    put_u(put_s(s, "SLOT "), slot + 1u);
    if (!smp[slot].ready) {
        put_s(s + ui_len(s), smp[slot].state == SS_SCAN ? " / CHECKING" : " / EMPTY");
        ui_caption(s, 0);
        ui_text(120 - 4 * 5, 96, "EMPTY", 1, UI_M);
        return;
    }
    if (smp[slot].name[0]) put_s(put_s(s + ui_len(s), " / "), smp[slot].name);
    sp_loop(p, &L);
    x = (int32_t)(L.len * 100u / 22050u);
    put_s(put_s(put_x10(r, (uint32_t)(x + 5) / 10u), " S"), L.rev ? " REV" : "");
    ui_caption(s, r);
    smp_peaks(slot);
    x0 = (int32_t)((uint64_t)L.l0 * 54u / smp[slot].n);
    x1 = (int32_t)((uint64_t)(L.l0 + L.len) * 54u / smp[slot].n);
    for (x = 0; x < 54; x++) {
        int32_t a = smp_pk[slot][x] ? smp_pk[slot][x] : 1, in = x >= x0 && x <= x1;
        for (y = 0; y < 30; y++) {
            int32_t dy = y - 15 < 0 ? 15 - y : y - 15;
            if (dy <= a) ui_cell(x, y, dy == a ? (in ? UI_L : UI_M) : (in ? UI_M : UI_D));
        }
    }
    for (y = 0; y < 30; y++) {                  /* the loop's ends */
        ui_cell(x0, y, ui_acc);
        ui_cell(x1 > 53 ? 53 : x1, y, ui_acc);
    }
}

static void pic_voice(void)                     /* a mouth singing VOWEL: A open, E wide, I narrow, O round, U small */
{
    static const int8_t RX[5] = {14, 17, 18, 9, 6}, RY[5] = {10, 6, 3, 10, 6};
    const volatile int32_t *p = trk[sel].p[ENG_VOICE];
    int32_t v = clamp(p[0], 0, 127) * 4 * 256 / 127, i = v >> 8, f = v & 255, x, y, rx, ry, sh = clamp(p[1], 0, 24) - 12;
    char s[24], t[12];
    if (i >= 4) i = 3, f = 256;
    rx = (RX[i] * (256 - f) + RX[i + 1] * f) / 256 + sh / 4;
    ry = (RY[i] * (256 - f) + RY[i + 1] * f) / 256;
    formant_text(p, 0, t);
    put_s(put_s(s, "VOWEL "), t);
    formant_text(p, 1, t);
    {
        char r[16];
        put_s(put_s(r, "SHIFT "), t);
        ui_caption(s, r);
    }
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            int32_t dx = x - 27, dy = y - 15, o = (dx * dx) * (ry + 3) * (ry + 3) + (dy * dy) * (rx + 3) * (rx + 3),
                    oo = (rx + 3) * (rx + 3) * (ry + 3) * (ry + 3), n = (dx * dx) * ry * ry + (dy * dy) * rx * rx,
                    nn = rx * rx * ry * ry;
            if (o <= oo) ui_cell(x, y, n <= nn ? (dy > ry / 2 && dy * 3 > ry && n * 4 <= nn * 3 ? ui_acc : UI_K) : UI_L);
        }
}

static void pic_wheel(void)                     /* the nine drawbars of the registration (PERC: its bar's tip) */
{
    static const char FOOT[9] = {'M', 'M', 'L', 'L', 'D', 'L', 'D', 'D', 'L'};   /* brown, white, black as greys */
    const volatile int32_t *p = trk[sel].p[ENG_WHEEL];
    uint32_t reg = (uint32_t)clamp(p[0], 0, 15), perc = (uint32_t)clamp(p[1], 0, 6), k;
    int32_t pk = perc ? (int32_t)(3u + ((perc - 1u) & 1u)) : -1, y;
    ui_caption(N_DRW_REG[reg], perc ? N_DRW_PERC[perc] : "PERC OFF");
    for (k = 0; k < 9u; k++) {
        int32_t lv = DRW_REG[reg][k] - '0', x = 5 + (int32_t)k * 5;
        uint16_t c = FOOT[k] == 'M' ? UI_M : FOOT[k] == 'L' ? UI_L : UI_D;
        if (perc && k == 8u) lv = 0;            /* (PERC on: the 1' bar is off) */
        ui_rect(PX0 + x * 4, PY0, 16, 2, UI_D);
        for (y = 0; y < lv * 3; y++)
            ui_rect(PX0 + x * 4 + 2, PY0 + 2 + y * 4, 12, 4, c);
        ui_rect(PX0 + x * 4, PY0 + 2 + lv * 12, 16, 6, (int32_t)k == pk ? ui_acc : c);
    }
}

static void pic_phys(void)                      /* the model's body: a bar, a string (and its sympathetic ones), a drum */
{
    const volatile int32_t *p = trk[sel].p[ENG_PHYS];
    uint32_t md = phys_model(p);
    int32_t x, y, st = clamp(p[1], 0, 127), bw = clamp(p[3], 0, 127);
    char r[16];
    phys_text(p, 3, r);
    ui_caption(N_PHYS_MODEL[md], r);
    if (md == PM_MODAL) {                       /* a bar on two cords (its nodes), struck or bowed at its end */
        for (x = 6; x < 48; x++)
            for (y = 12; y < 18; y++) ui_cell(x, y, y == 12 ? UI_L : UI_M);
        for (x = 0; x < 3; x++) ui_cell(15 + x * (12 + st / 32), 19, UI_D);   /* (STRUCTURE spreads the partials) */
        ui_cell(15, 9, UI_D), ui_cell(15, 10, UI_D), ui_cell(15, 11, UI_D);
        ui_cell(38, 9, UI_D), ui_cell(38, 10, UI_D), ui_cell(38, 11, UI_D);
        ui_cell(47, bw ? 15 : 21, ui_acc), ui_cell(48, bw ? 15 : 22, ui_acc);
    } else if (md == PM_MEMB) {                 /* a drum head and its rings */
        for (y = 0; y < 30; y++)
            for (x = 0; x < 54; x++) {
                int32_t dx = x - 27, dy = (y - 15) * 2, d = dx * dx + dy * dy;
                if (d <= 22 * 22) ui_cell(x, y, d >= 20 * 20 ? UI_L : (d >= 11 * 11 && d < 13 * 13) || d < 4 * 4 ? UI_M : UI_D);
            }
        ui_cell(27 + st / 16, 15, ui_acc);
    } else {                                    /* a string, its swing (and SYMPATH's three strings below) */
        for (x = 4; x < 50; x++) {
            int32_t a = (sine_i((uint32_t)(x - 4) * 46684427u) * (2 + bw / 32)) >> 15;   /* (half a sine over 46) */
            ui_cell(x, 12 - (a < 0 ? -a : a), UI_L);
            ui_cell(x, 12, UI_D);
        }
        ui_cell(4, 11, UI_M), ui_cell(4, 12, UI_M), ui_cell(4, 13, UI_M);
        ui_cell(49, 11, UI_M), ui_cell(49, 12, UI_M), ui_cell(49, 13, UI_M);
        ui_cell(4 + 46 * clamp(st, 0, 127) / 254, 14, ui_acc);
        if (md == PM_SYMP)
            for (y = 0; y < 3; y++)
                for (x = 8; x < 46; x++) ui_cell(x, 20 + y * 3, UI_M);
    }
}

static void pic_phase(void)                     /* the bent waveform of WAVE at DCW (WAVE2: the next cycle, darker) */
{
    const volatile int32_t *p = trk[sel].p[ENG_PHASE];
    uint32_t w1 = (uint32_t)clamp(p[0], 0, 7), w2 = (uint32_t)clamp(p[1], 0, 8), dcw;
    int32_t x, y0 = -1, c;
    pd_t b;
    char s[24], t[8];
    dcw = ((uint32_t)clamp(p[2], 0, 127) * 65535u / 127u * 56000u) >> 16;
    phase_text(p, 0, t);
    put_s(put_s(s, "WAVE "), t);
    {
        char r[20], u[8];
        phase_text(p, 1, u);
        put_s(put_s(r, "WAVE2 "), u);
        ui_caption(s, r);
    }
    for (c = 0; c < 2; c++) {                   /* two cycles: WAVE, then WAVE2 (or WAVE again) */
        pd_setup(&b, c && w2 ? w2 - 1u : w1, dcw);
        for (x = 0; x < 27; x++) {
            int32_t yy = 15 - pd_wave(&b, (uint32_t)x * 65535u / 26u) * 13 / 32767, a = y0 < 0 ? yy : y0, z;
            for (z = a < yy ? a : yy; z <= (a < yy ? yy : a); z++) ui_cell(c * 27 + x, z, c ? UI_M : UI_L);
            y0 = yy;
        }
        ui_cell(c * 27 + (int32_t)(b.x * 26u / 65535u), 29, ui_acc);   /* the bend's point */
    }
}

static void pic_source(void)                    /* the seven sources, the track's in the accent; LATCH */
{
    int32_t i, e = (int32_t)sel_eng();
    ui_caption("SOURCE", 0);
    for (i = 0; i < (int32_t)NENG; i++) {
        int32_t y = 44 + i * 16;
        if (i == e) ui_rect(PX0, y - 3, 108, 14, UI_D);
        ui_text(PX0 + 4, y, i == e ? ">" : " ", 1, ui_acc);
        ui_text(PX0 + 16, y, ENGINES[i]->name, 1, i == e ? ui_acc : UI_M);
    }
    ui_text(140, 60, "LATCH", 1, UI_M);
    ui_rect(140, 74, 64, 24, trk[sel].latch ? ui_acc : UI_D);
    ui_text(140 + (trk[sel].latch ? 16 : 8), 78, trk[sel].latch ? "ON" : "OFF", 2, trk[sel].latch ? UI_K : UI_L);
}

static void pic_env(void)                       /* Env: the shape of the track's ATTACK, DECAY, SUSTAIN, RELEASE */
{
    const volatile int32_t *e = trk[sel].env;
    int32_t wa = 2 + clamp(e[0], 0, 127) * 18 / 127, wd = 2 + clamp(e[1], 0, 127) * 12 / 127;
    int32_t wr = 2 + clamp(e[3], 0, 127) * 14 / 127, ws = 53 - wa - wd - wr, ys = 28 - clamp(e[2], 0, 127) * 25 / 127;
    int32_t x, y, ev[55];
    for (x = 0; x <= 54; x++) {                 /* the level a column (28: 0, 3: full) */
        if (x <= wa) ev[x] = 28 - 25 * x / wa;
        else if (x <= wa + wd) ev[x] = 3 + (ys - 3) * (x - wa) / wd;
        else if (x <= wa + wd + ws) ev[x] = ys;
        else ev[x] = ys + (28 - ys) * (x - wa - wd - ws) / wr;
        if (ev[x] > 28) ev[x] = 28;
    }
    ui_text(PX0 + (wa / 2) * 4, 31, "A", 1, UI_M);
    ui_text(PX0 + (wa + wd / 2) * 4, 31, "D", 1, UI_M);
    ui_text(PX0 + (wa + wd + ws / 2) * 4, 31, "S", 1, UI_M);
    ui_text(PX0 + (wa + wd + ws + wr / 2) * 4, 31, "R", 1, UI_M);
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            int32_t n = x < 53 ? ev[x + 1] : ev[x], lo = ev[x] < n ? ev[x] : n, hi = ev[x] < n ? n : ev[x];
            uint16_t c = 0;
            if (y > ev[x] && y <= 28 && !((x + y) & 1)) c = UI_D;
            if (y >= lo && y <= hi) c = UI_L;
            if (y == 29 && !(x & 1)) c = UI_D;
            if (c) ui_cell(x, y, c);
        }
    ui_cell(wa - 1, 3, ui_acc), ui_cell(wa, 3, ui_acc), ui_cell(wa - 1, 4, ui_acc), ui_cell(wa, 4, ui_acc);
}

static void pic_mix(void)                       /* FxTrack: the four tracks' columns, LEVEL and the sends (MIXER 2: PAN) */
{
    int32_t t, m, j;
    ui_caption(page == PG_MIX1 ? "LEVEL DLY REV GRAIN" : "PAN", 0);
    for (t = 0; t < (int32_t)NT; t++) {
        int32_t x0 = PX0 + t * 54 + 4;
        char lab[3] = {'T', (char)('1' + t), 0};
        if (page == PG_MIX1) {
            volatile int32_t *const V[4] = {&trk[t].level, &trk[t].dly, &trk[t].rev, &trk[t].grn};
            for (m = 0; m < 4; m++) {
                int32_t n = (clamp(*V[m], 0, 127) * 8 + 63) / 127;
                for (j = 0; j < 8; j++)
                    ui_rect(x0 + m * 12, 44 + j * 12, 9, 10, 7 - j < n ? (m ? UI_M : UI_L) : UI_D);
            }
        } else {
            int32_t pp = (clamp(trk[t].pan, 0, 127) * 6 + 63) / 127;
            for (j = 0; j < 7; j++)
                ui_rect(x0 + j * 7, 80, 6, 20, j == pp ? UI_L : j == 3 ? UI_M : UI_D);
        }
        ui_text(x0 + 14, 145, lab, 1, t == (int32_t)sel ? ui_acc : UI_M);
    }
}

static void pic_tape(void)                      /* EditTape: the reels and the tape, its wobble WOW */
{
    static const int16_t SPK[2][3][2] = {{{183, 123}, {107, 141}, {130, 66}}, {{408, 154}, {358, 94}, {435, 81}}};
    static const int8_t RC[2][2] = {{14, 11}, {40, 11}};
    int32_t x, y, r, k, on = tf_wow | tf_sat | tf_tone | tf_hiss, amp = (clamp(tf_wow, 0, 127) * 3 + 63) / 127;
    ui_caption("OVER THE WHOLE MIX", on ? "ON" : "BYPASS");
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            uint16_t c = 0;
            for (r = 0; r < 2; r++) {
                int32_t dx = x - RC[r][0], dy = y - RC[r][1], d = dx * dx + dy * dy;
                if (d < 58) c = UI_D;
                if (d >= 58 && d < 88) c = UI_L;
                if (d < 4) c = UI_L;
                for (k = 0; k < 3; k++) {
                    int32_t sx = x * 10 - SPK[r][k][0], sy = y * 10 - SPK[r][k][1];
                    if (sx * sx + sy * sy < 256) c = UI_M;
                }
            }
            if (y == 21 && x >= 14 && x <= 40) c = UI_L;
            if (x >= 6 && x <= 48 && y == 26 + ((sine_i((uint32_t)x * 341782637u) * amp + 16384) >> 15)) c = UI_M;
            if (x >= 25 && x <= 29 && y >= 22 && y <= 24) c = ui_acc;
            if (c) ui_cell(x, y, c);
        }
}

static void pic_grain(void)                     /* EditGrain: the cloud, its grains scattered by SCATTER, DENSITY */
{
    static const uint8_t AMP[54] = {2, 5, 4, 6, 7, 5, 8, 7, 9, 10, 9, 12, 9, 12, 12, 12, 13, 10, 13, 11, 11, 11, 9, 10,
                                    7, 9, 7, 6, 7, 3, 6, 4, 4, 4, 2, 4, 2, 3, 3, 2, 3, 1, 3, 2, 2, 3, 1, 3, 1, 2, 2, 1,
                                    3, 1};
    int32_t x, y, i, n = 8 + clamp(ge_dens, 0, 127) * 30 / 127, sp = 2 + clamp(ge_scat, 0, 127) * 40 / 127;
    char r[12];
    put_s(put_u(r, (pow2_q16(clamp(ge_size, 0, 127) * 8) * 10u + 32768u) >> 16), " MS");
    ui_caption("BUFFER 1.25 S", ge_freeze ? "FREEZE" : r);
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            int32_t dy = y < 15 ? 15 - y : y - 15;
            if (dy <= AMP[x]) ui_cell(x, y, dy == AMP[x] ? UI_M : UI_D);
            if (x == 21) ui_cell(x, y, ui_acc);
        }
    ui_rnd_s = 11;
    for (i = 0; i < n; i++) {
        int32_t gx = 21 + (int32_t)ui_rnd((uint32_t)sp + 1u) - sp / 2, gy = (int32_t)ui_rnd(30);
        if (gx >= 0 && gx < 54) ui_cell(gx, gy, UI_L);
    }
    if (ge_freeze) {
        ui_rect(180, 29, 56, 12, ui_acc);
        ui_text(184, 31, "FREEZE", 1, UI_K);
    }
}

static void pic_delay(void)                     /* Space: the echoes, smaller each time (FEEDBACK), apart by TIME */
{
    static const int8_t SZ[7] = {24, 20, 17, 14, 11, 8, 5};
    int32_t n = 1 + clamp(dl_fb, 0, 127) * 6 / 127, gap = 2 + clamp(dl_time, 0, 127) * 8 / 127, w = 0, i, x;
    char r[12];
    ui_time(r, DL_MS[clamp(dl_time, 0, 127)] << 16);
    ui_caption("DELAY", r);
    for (i = 0; i < n; i++) w += SZ[i] + (i ? gap : 0);
    for (i = 0, x = 120 - w / 2; i < n; x += SZ[i] + gap, i++)
        ui_rect(x, 110 - SZ[i], SZ[i], SZ[i], i < 2 ? UI_L : i < 4 ? UI_M : UI_D);
}
static void pic_space(void)                     /* Space: the reverb's tail, as long as SIZE (INF: it never falls) */
{
    int32_t v = clamp(rv_size, 0, 127), i, j, tl = 2 + v * 14 / 127;
    ui_caption("REVERB", 0);
    for (i = 0; i < 37; i++) {
        int32_t h = v >= RV_INF ? 18 : (int32_t)((18u * pow2_q16(-(i * 192) / tl) + 32768u) >> 16);
        h += (i * 7) % 3 - 1;
        h = h < 1 ? 1 : h > 18 ? 18 : h;
        for (j = 0; j < h; j++)
            ui_rect(9 + i * 6, 156 - (h - j) * 4, 3, 3, j == 0 ? UI_L : j < h / 2 ? UI_M : UI_D);
    }
    if (v >= RV_INF) {
        ui_rect(180, 29, 56, 12, ui_acc);
        ui_text(184, 31, "FREEZE", 1, UI_K);
    }
}

static void pic_grid(void)                      /* Seq: the 64 steps of the track's pattern, 8 rows of 8 */
{
    static const char DOT[5][6] = {".XXX.", "XXXXX", "XXXXX", "XXXXX", ".XXX."}, BAR[5][6] = {".....", "XXXXX", "XXXXX",
                                                                                              "XXXXX", "....."};
    const seq_trk_t *S = &seq[sel];
    const seq_pat_t *P = &S->pat[clamp(S->cur, 0, SEQ_NPAT - 1)];
    int32_t n = seq_steps(S), cur = clamp(S->cursor, 0, SEQ_N - 1), pos = S->pos, i, j, x, y;
    uint8_t tie[SEQ_N];
    char st[12];
    for (i = 0; i < (int32_t)SEQ_N; i++) tie[i] = 0;
    for (i = 0; i < n; i++)                     /* a note longer than a step: its tie over the next ones */
        if (P->st[i].note)
            for (j = 1; j < clamp(P->st[i].len, 1, SEQ_N) && i + j < n; j++) tie[i + j] = 1;
    for (i = 0; i < (int32_t)SEQ_N; i++) {
        const seq_step_t *s = &P->st[i];
        int32_t dx = 12 + (i & 7) * 16, dy = 32 + (i >> 3) * 16, bar = !s->note && tie[i] && i < n;
        for (y = 0; y < 5; y++)
            for (x = 0; x < 5; x++) {
                uint16_t c;
                if ((bar ? BAR : DOT)[y][x] != 'X') continue;
                if (i == pos && seq_on) c = UI_W;           /* NOW */
                else if (i >= n) c = UI_D;                  /* past STEPS */
                else if (bar) c = UI_L;                     /* a tie */
                else if (!s->note) c = UI_M;                /* OFF */
                else if (s->chance < 100) c = (x + y) & 1 ? UI_L : UI_M;   /* CHANCE */
                else if (s->ratchet > 1) c = x == 2 ? UI_K : UI_L;        /* RATCHET */
                else c = s->chord ? ui_acc : UI_L;          /* NOTE (a chord step: CHORD) */
                ui_rect(dx + x * 2, dy + y * 2, 2, 2, c);
            }
        if (s->lmask && s->note && i < n) ui_rect(dx + 11, dy + 4, 2, 2, UI_W);   /* (a step with locks: a mark) */
        if (i == cur) {                         /* the cursor: a ring */
            ui_rect(dx - 2, dy - 2, 14, 1, UI_W), ui_rect(dx - 2, dy + 11, 14, 1, UI_W);
            ui_rect(dx - 2, dy - 2, 1, 14, UI_W), ui_rect(dx + 11, dy - 2, 1, 14, UI_W);
        }
    }
    ui_text(148, 32, "STEP", 1, UI_M);          /* the right panel: STEP, PLAYING, BPM, STEPS, CHORD (later), the legend */
    st[0] = (char)('0' + (cur + 1) / 10), st[1] = (char)('0' + (cur + 1) % 10), st[2] = 0;
    ui_text(148, 41, st, 2, UI_L);
    ui_text(148, 61, "PLAYING", 1, UI_M);
    if (pos >= 0 && seq_on) put_u(put_s(put_u(st, (uint32_t)pos + 1u), "/"), (uint32_t)n);
    else put_u(put_s(st, "-/"), (uint32_t)n);
    ui_text(148, 71, st, 1, UI_L);
    put_u(put_s(st, "BPM "), (uint32_t)clamp(seq_bpm, 20, 300));
    ui_text(148, 83, st, 1, UI_L);
    put_u(put_s(st, "STEPS "), (uint32_t)n);
    ui_text(148, 93, st, 1, UI_L);
    if (P->st[cur].note && P->st[cur].chord) {   /* the selected step's chord: a chord step's own, "CHORD Cm7" */
        chord_name_tv(P->st[cur].chord & 15, (uint32_t)P->st[cur].note, st);
        if (ui_len(st) <= 5) ui_text(196, 105, st, 1, ui_acc), ui_text(148, 105, "CHORD", 1, UI_M);
        else ui_text(148, 105, st, 1, ui_acc);
    } else if (!trk[sel].chord) ui_text(148, 105, "CHORD OFF", 1, UI_M);   /* (a note step: the track's chord mode) */
    else if (!P->st[cur].note) ui_text(148, 105, "CHORD -", 1, UI_M);
    else {
        chord_name(sel, (uint32_t)P->st[cur].note, st);
        if (ui_len(st) <= 5) ui_text(196, 105, st, 1, ui_acc), ui_text(148, 105, "CHORD", 1, UI_M);
        else ui_text(148, 105, st, 1, ui_acc);
    }
    {
        static const char *const LEG[4] = {"NOTE", "CHORD", "CHANCE", "NOW"};
        const uint16_t LC[5] = {UI_L, ui_acc, UI_M, UI_W, UI_D};
        for (i = 0; i < 5; i++) {
            ui_rect(148, 118 + i * 9, 5, 5, LC[i]);
            if (i < 4) ui_text(157, 117 + i * 9, LEG[i], 1, UI_M);
            else {
                put_u(put_s(st, "OFF >"), (uint32_t)n);
                ui_text(157, 117 + i * 9, st, 1, UI_M);
            }
        }
    }
}
static void pic_pattern(void)                   /* SeqSet: the patterns (A; B .. D: song mode, later), the settings */
{
    static const char *const ROW[4] = {"STEPS", "DIVISION", "SWING", "PLAY MODE"};
    const seq_trk_t *S = &seq[sel];
    int32_t i;
    char v[12];
    ui_caption("PATTERNS", 0);
    for (i = 0; i < 4; i++) {
        int32_t x = PX0 + i * 55;
        char nm[2] = {(char)('A' + i), 0};
        if (!i) {                               /* A: the one there is, playing or not */
            ui_rect(x, 40, 51, 32, ui_acc);
            ui_text(x + 18, 43, nm, 2, UI_K);
            if (seq_on) put_s(v, "PLAY");
            else put_s(put_u(v, (uint32_t)seq_steps(S)), "ST");
            ui_text(x + 26 - 4 * ui_len(v), 61, v, 1, UI_K);
        } else {
            ui_rect(x, 40, 51, 32, UI_D);
            ui_text(x + 18, 43, nm, 2, UI_M);
            ui_text(x + 6, 61, "EMPTY", 1, UI_M);
        }
    }
    for (i = 0; i < 4; i++) {
        int32_t y = 77 + i * 20;
        ui_rect(PX0, y, 216, 17, UI_D);
        ui_text(PX0 + 6, y + 5, ROW[i], 1, UI_L);
        if (i == 0) put_u(v, (uint32_t)seq_steps(S));
        else if (i == 1) put_s(v, SEQ_DIV_NAME[clamp(S->div, 0, 7)]);
        else if (i == 2) ui_3(v, ui_pc(S->swing));
        else put_s(v, SEQ_MODE_NAME[clamp(S->mode, 0, SEQ_NMODE - 1)]);
        ui_text(PX0 + 210 - 8 * ui_len(v), y + 5, v, 1, UI_L);
    }
}

static void pic_sel(void)                       /* Scale: the keys (the scale's light, the chord's in the accent), the
                                                 * chord of the note last played (none: ROOT's first) and its name */
{
    static const int8_t WI[12] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6};   /* (from F: a white key's place) */
    uint32_t base = KEY0 + (uint32_t)(12 * ui_oct), r, n, i, k, lit = 0;
    uint8_t ns[CH_MAX];
    char l[32], rg[12], *e;
    int32_t x;
    r = ch_last >= (int32_t)base && ch_last < (int32_t)base + 27 ? (uint32_t)ch_last :
        base + (uint32_t)(clamp(sc_root, 0, 11) + 12 - (int32_t)(base % 12u)) % 12u;
    n = chord_notes(sel, r, ns);
    for (i = 0; i < n; i++)
        if (ns[i] >= base && ns[i] < base + 27u) lit |= 1u << (ns[i] - base);
    e = put_s(put_s(l, SC_NOTE[clamp(sc_root, 0, 11)]), " ");
    put_s(e, SC_NAME[clamp(sc_scale, 0, NSCALE - 1)]);
    ui_note(put_s(ui_note(rg, base), "-"), base + 26u);   /* (the keys' range: "F3-G5") */
    ui_caption(l, rg);
    for (k = 0; k < 27u; k++)                   /* the white keys, then the black ones over them */
        if (WI[k % 12u] >= 0) {
            x = 12 + ((int32_t)(k / 12u) * 7 + WI[k % 12u]) * 27 / 2;
            ui_rect(x, 42, 12, 72, lit >> k & 1u ? ui_acc : sc_in((int32_t)(base + k)) ? UI_L : UI_M);
        }
    for (k = 0; k < 27u; k++)
        if (WI[k % 12u] < 0) {
            x = 12 + (((int32_t)(k / 12u) * 7 + WI[(k - 1u) % 12u] + 1) * 27 - 10) / 2;
            ui_rect(x, 42, 9, 43, UI_K);
            ui_rect(x + 1, 42, 7, 42, lit >> k & 1u ? ui_acc : sc_in((int32_t)(base + k)) ? UI_M : UI_D);
        }
    chord_name(sel, r, l);
    x = ui_text(12, 124, l, 2, trk[sel].chord ? ui_acc : UI_M) + 8;
    if (!trk[sel].chord) put_s(l, "OFF: HOLD SEL");
    else {
        for (i = 0, k = 1; i < n; i++) k &= (uint32_t)sc_in(ns[i]);   /* (all of them in the scale: IN KEY) */
        put_s(put_s(put_u(l, n), n == 1u ? " NOTE" : " NOTES"), k && sc_scale > 0 ? " IN KEY" : "");
    }
    if (x + 8 * ui_len(l) > 228) {              /* (a long name: the short of it) */
        if (trk[sel].chord) put_s(put_u(l, n), " NOTES");
        else put_s(l, "OFF");
    }
    ui_text(x, 128, l, 1, UI_M);
    ui_rect(12, 147, 6, 6, UI_L), ui_text(22, 146, "IN SCALE", 1, UI_M);
    ui_rect(98, 147, 6, 6, ui_acc), ui_text(108, 146, "CHORD", 1, UI_M);
}

static void pic_arp(void)                       /* Arp: what it holds, its steps (13 at most) as bars, their height
                                                 * the note, the step playing in the accent */
{
    const arp_t *A = &arp[sel];
    uint8_t p[ARP_PN];
    uint32_t oct = (uint32_t)clamp(trk[sel].arp_oct, 1, 4), n, i, k, ids = 0, un = 0, lo = 127, hi = 0, from = 0, cnt;
    int32_t now = A->last, chord = clamp(trk[sel].arp_mode, 0, NARPMODE - 1) == ARP_CHORD, x, y;
    char l[48], *e;
    n = arp_pattern(sel, p, oct);
    cnt = A->n < ARP_N ? A->n : ARP_N;
    for (i = 0; i < cnt; i++) {                 /* the keys it holds (one: its name; its chord's, chord mode on), the
                                                 * notes (each once) */
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
    ui_caption(l, 0);
    if (!trk[sel].arp || !n) {
        const char *m = trk[sel].arp ? "PLAY KEYS" : "HOLD ARP: ON";
        ui_text(120 - 4 * ui_len(m), 96, m, 1, UI_M);
        return;
    }
    if (chord) n = oct;                         /* (CHORD: a step an octave, all its notes) */
    for (i = 0; i < n; i++) {
        uint32_t v = chord ? i : p[i];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    if (now >= (int32_t)n) now = -1;
    if (n > 13u && now > 6) from = (uint32_t)now - 6u < n - 13u ? (uint32_t)now - 6u : n - 13u;
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54; x++) {
            uint32_t st = from + (uint32_t)x / 4u, v, lv, top;
            uint16_t c = 0;
            if (y == 29 && !(x & 1)) c = UI_D;
            if (st < n && st < from + 13u && x % 4 < 3) {
                v = chord ? st : p[st];
                lv = hi > lo ? (v - lo) * 7u / (hi - lo) : 3u;
                top = 26u - lv * 3u - 2u;
                if ((uint32_t)y >= top && (uint32_t)y <= top + 2u) c = (int32_t)st == now ? ui_acc : UI_L;
                else if ((uint32_t)y > top + 2u && y < 29 && (int32_t)st == now) c = UI_D;
                else if (chord && (uint32_t)y > top + 2u && y < 29) c = UI_M;   /* (a chord: a full column) */
            }
            if (c) ui_cell(x, y, c);
        }
}

static uint32_t lfo_dot(void) { return (uint32_t)(((uint64_t)lfo_ph[sel] * 54u) >> 32); }   /* the dot's column */
static int32_t lfo_row(uint32_t sh, uint32_t x)  /* the picture's SHAPE at column x (0..53, a cycle): its row */
{
    static const int16_t R[7] = {9000, -20000, 26000, -6000, 15000, -28000, 9000};   /* (RANDOM, DRIFT: an example) */
    int32_t v;
    if (sh == LS_RND) v = R[x / 9u];
    else if (sh == LS_DRIFT) v = lfo_shape(LS_DRIFT, (x % 9u) * 477218588u, R[x / 9u], R[x / 9u + 1u]);
    else v = lfo_shape(sh, x * 79536431u, 0, 0);   /* (2^32 / 54) */
    return 15 - v * 11 / 32767;
}
static void pic_lfo(void)                       /* Lfo: a cycle of SHAPE, the dot where the LFO is (at its rate), a
                                                 * cycle's time */
{
    uint32_t sh = (uint32_t)clamp(trk[sel].lfo_shape, 0, NLFOSHAPE - 1), dx = lfo_dot(), x;
    uint32_t ms = 3932160000u / lfo_hz_q16(trk[sel].lfo_rate);
    int32_t y, row[54];
    uint16_t lc = trk[sel].lfo_depth > 0 ? UI_L : UI_M;   /* (DEPTH 0: the shape dim) */
    char l[32];
    lfo_rate_text(put_s(l, ms >= 995u ? "1 CYCLE = " : ""), trk[sel].lfo_rate, " ");
    put_s(l + ui_len(l), " / FREE");
    ui_caption(l, 0);
    for (x = 0; x < 54u; x++) row[x] = lfo_row(sh, x);
    for (y = 0; y < 30; y++)
        for (x = 0; x < 54u; x++) {
            int32_t n = row[x < 53u ? x + 1u : x], lo = row[x] < n ? row[x] : n, hi = row[x] < n ? n : row[x];
            int32_t mx = 5 * (int32_t)x - 248, my = 5 * y - 20;
            uint16_t c = 0;
            if (y == 15 && x % 3u == 0u) c = UI_D;
            if (y >= lo && y <= hi) c = lc;
            if (((int32_t)x - 48) * ((int32_t)x - 48) + (y - 5) * (y - 5) <= 10 && mx * mx + my * my > 200) c = UI_L;
            if ((x == dx || x == dx + 1u) && y >= row[dx] - 1 && y <= row[dx]) c = ui_acc;   /* the dot */
            if (c) ui_cell((int32_t)x, y, c);
        }
}

static void song_cell(int32_t c, int32_t r, const char *t, uint16_t bg, uint16_t fg)   /* Song: a cell of the grid */
{
    int32_t x = 35 + c * 39, y = 31 + r * 21;
    if (bg) ui_rect(x, y, 36, 18, bg);
    ui_text(x + 18 - 4 * ui_len(t), y + 5, t, 1, fg);
}
static void pic_song(void)                      /* Song: five of the chain's steps (the one playing in the accent), a
                                                 * lane a track (its slot's letter; no notes in it: -), the REPEATS
                                                 * (playing: left / of) */
{
    int32_t cur = song_cur(), len = (int32_t)song_length(), from = clamp(cur - 2, 0, SONG_N - 5), c, r;
    char b[8];
    for (r = 0; r < 4; r++) {
        b[0] = 'T', b[1] = (char)('1' + r), b[2] = 0;
        ui_text(10, 31 + (r + 1) * 21 + 5, b, 1, UI_M);
    }
    ui_text(10, 31 + 5 * 21 + 5, "REP", 1, UI_M);
    for (c = 0; c < 5; c++) {
        int32_t i = from + c, on = i == cur, p = song_pat[i];
        put_u(b, (uint32_t)i + 1u);
        song_cell(c, 0, b, 0, on ? ui_acc : i <= len ? UI_M : UI_D);
        if (i > len) continue;
        if (i == len) { song_cell(c, 5, "END", 0, UI_M); continue; }
        for (r = 0; r < 4; r++) {
            int has = slot_has((uint32_t)p, (uint32_t)r);
            b[0] = has ? (char)('A' + p) : '-', b[1] = 0;
            song_cell(c, r + 1, b, !has ? UI_K : on ? ui_acc : UI_D, !has ? UI_M : on ? UI_K : UI_L);
        }
        if (i == song_at && song_rec != 2u) put_u(put_s(put_u(b, song_left), "/"), (uint32_t)clamp(song_rep[i], 1, 16));
        else put_u(put_s(b, "X"), (uint32_t)clamp(song_rep[i], 1, 16));
        song_cell(c, 5, b, 0, on ? ui_acc : UI_M);
    }
}

static void pic_perform(void)                   /* FxLayer: the eight tricks (FX held + the white keys), on in the
                                                 * accent (its mutes: GLO's) */
{
    static const char *const KEY[8] = {"F3", "G3", "A3", "B3", "C4", "D4", "E4", "F4"};
    static const char *const NM[8][2] = {{"FREEZE", ""}, {"REV", "THROW"}, {"ECHO", "THROW"}, {"TAPE", "STOP"},
                                         {"REVER-", "SE"}, {"FILTER", "DOWN"}, {"FILTER", "UP"}, {"STUT-", "TER"}};
    int32_t i;
    for (i = 0; i < 8; i++) {
        int32_t x = 12 + (i & 3) * 55, y = 31 + (i >> 2) * 42;
        int on = i ? (int)(pf_held >> (i - 1) & 1u) : (int)(ge_freeze & 1u);
        ui_rect(x, y, 51, 38, on ? ui_acc : UI_D);
        ui_text(x + 3, y + 3, KEY[i], 1, on ? UI_K : UI_M);
        ui_text(x + 2, y + 18, NM[i][0], 1, on ? UI_K : UI_L);
        ui_text(x + 2, y + 28, NM[i][1], 1, on ? UI_K : UI_L);
    }
}

static void ui_row(int32_t i, int sel_row, const char *l, const char *r, uint16_t lc)   /* Save: a row of the list */
{
    int32_t y = 31 + i * 21;
    ui_rect(PX0, y, 216, 18, sel_row ? UI_L : UI_D);
    ui_text(PX0 + 6, y + 5, l, 1, sel_row ? UI_K : lc);
    ui_text(PX0 + 210 - 8 * ui_len(r), y + 5, r, 1, sel_row ? UI_K : UI_M);
}
static void pic_sounds(void)                    /* Save: the sound slots (TYPE's list), the selected one light */
{
    int32_t list[SND_N], n = 0, i, at = -1, from;
    for (i = 0; i < (int32_t)SND_N; i++)
        if (sv_shown((uint32_t)i)) {
            if (i == sv_slot) at = n;
            list[n++] = i;
        }
    if (!n) { ui_text(120 - 4 * 9, 96, "NO SOUNDS", 1, UI_M); return; }
    from = clamp((at < 0 ? 0 : at) - 1, 0, n > 6 ? n - 6 : 0);
    for (i = 0; i < 6 && from + i < n; i++) {
        const uint8_t *r = snd_rec((uint32_t)list[from + i]);
        char l[24], *e = ui_3(l, (uint32_t)list[from + i] + 1u);
        *e++ = ' ';
        if (r) snd_name((uint32_t)list[from + i], e); else put_s(e, "---");
        ui_row(i, from + i == at, l, r ? ENGINES[r[3]]->name : "EMPTY", r ? UI_L : UI_M);
    }
}
static void pic_projects(void)                  /* Save: the slots A..D, used or not (the one in use in the accent) */
{
    int32_t i;
    for (i = 0; i < 4; i++) {
        char l[12];
        put_s(l, "SLOT A");
        l[5] = (char)('A' + i);
        ui_row(i, i == (pj_slot & 3), l, seq_recall == i ? "NEXT BAR" : slot_used((uint32_t)i) ? "USED" : "EMPTY",
               i == slot_cur ? ui_acc : slot_used((uint32_t)i) ? UI_L : UI_M);
    }
}

/* System (halved): COLOUR's four swatches (the chosen one ringed), then a row a setting; SYSTEM 2: MIDI LEARN's links,
 * the newest first */
static void sys_row(int32_t i, const char *l, const char *v)
{
    int32_t y = 74 + i * 17;
    ui_rect(PX0, y, 216, 15, UI_D);
    ui_text(PX0 + 6, y + 4, l, 1, UI_L);
    ui_text(PX0 + 210 - 8 * ui_len(v), y + 4, v, 1, UI_W);
}
static void pic_system(void)
{
    int32_t i;
    char v[24];
    ui_caption("COLOUR (MATCH YOUR FM-1)", 0);
    for (i = 0; i < 4; i++) {
        int32_t x = PX0 + i * 55;
        if (i == (int32_t)set.col) ui_rect(x - 4, 42, 58, 28, UI_W), ui_rect(x - 2, 44, 54, 24, UI_K);
        ui_rect(x, 46, 50, 20, UI_ACCENTS[i]);
        ui_text(x + 2, 55, COL_NAME[i], 1, UI_K);
    }
    sys_row(0, "MIDI CHANNEL", (mch_text(v, set.mch), v));
    tune_text(v, set.tune);
    sys_row(1, "TUNE", v);
    sys_row(2, "SCREEN OFF", SCR_NAME[set.scr % 5u]);
    put_s(put_u(v, set.nlk), set.nlk == 1u ? " LINK" : " LINKS");
    sys_row(3, "MIDI LEARN", v);
    sys_row(4, "SEQ MIDI OUT", set.out ? "ON" : "OFF");
}
static void sys2_cpu(void)                      /* SYSTEM 2's last row: CPU NOW, PEAK (OCT- + OCT+: PEAK to 0) */
{
    char l[16], r[16];
    put_s(put_x10(put_s(l, "CPU NOW "), pct10(cpu_now)), "%");
    put_s(put_x10(put_s(r, "PEAK "), pct10(cpu_peak)), "%");
    ui_rect(PX0, 144, 216, 15, UI_D);
    ui_text(PX0 + 6, 148, l, 1, UI_L);
    ui_text(PX0 + 210 - 8 * ui_len(r), 148, r, 1, UI_W);
}
static void pic_sys2(void)
{
    int32_t i, n = set.nlk;
    char l[8], v[24];
    put_s(put_u(v, (uint32_t)n), "/32");
    ui_caption("MIDI LEARN: LINKS", v);
    sys2_cpu();
    if (!n) { ui_text(120 - 4 * 8, 84, "NO LINKS", 1, UI_M); return; }
    for (i = 0; i < 5 && i < n; i++) {
        int32_t y = 42 + i * 17;
        const uint8_t *L = set.lk[n - 1 - i];
        ui_rect(PX0, y, 216, 15, UI_D);
        if (i == 4 && n > 5) {                  /* (more than five: how many more) */
            put_s(put_u(put_s(v, "+"), (uint32_t)(n - 4)), " MORE");
            ui_text(PX0 + 6, y + 4, v, 1, UI_M);
            break;
        }
        put_u(put_s(l, "CC "), L[0]);
        ui_text(PX0 + 6, y + 4, l, 1, UI_L);
        link_name(v, L);
        ui_text(PX0 + 210 - 8 * ui_len(v), y + 4, v, 1, UI_W);
    }
}

static void pic_notbuilt(void) { ui_text(120 - 4 * 13, 96, "NOT BUILT YET", 1, UI_M); }

static void ui_pic_draw(void)
{
    switch (page) {
    case PG_HOME: pic_home(); return;
    case PG_FMALGO: pic_fmalgo(); return;
    case PG_FMOP: pic_fmop(); return;
    case PG_SOUND:
        switch (sel_eng()) {
        case ENG_VA: pic_va(); return;
        case ENG_FM: pic_fm(); return;
        case ENG_SAMPLE: pic_sample(); return;
        case ENG_VOICE: pic_voice(); return;
        case ENG_WHEEL: pic_wheel(); return;
        case ENG_PHYS: pic_phys(); return;
        default: pic_phase(); return;
        }
    case PG_SOURCE: pic_source(); return;
    case PG_ENV: pic_env(); return;
    case PG_MIX1: case PG_MIX2: pic_mix(); return;
    case PG_TAPE: pic_tape(); return;
    case PG_GRAIN: pic_grain(); return;
    case PG_DELAY: pic_delay(); return;
    case PG_SPACE: pic_space(); return;
    case PG_GRID: pic_grid(); return;
    case PG_PATTERN: pic_pattern(); return;
    case PG_SCALE: case PG_CHORD: pic_sel(); return;
    case PG_ARP: pic_arp(); return;
    case PG_LFO: pic_lfo(); return;
    case PG_SONG: pic_song(); return;
    case PG_PERFORM: pic_perform(); return;
    case PG_SOUNDS: pic_sounds(); return;
    case PG_PROJECTS: pic_projects(); return;
    case PG_SYSTEM: pic_system(); return;
    case PG_SYS2: pic_sys2(); return;
    default: pic_notbuilt(); return;
    }
}

/* what the picture shows: a hash of everything it is drawn from (a change: redrawn) */
static uint32_t ui_pic_key(void)
{
    uint32_t h = 2166136261u, k, t;
#define UI_H(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    UI_H(page), UI_H(sel), UI_H(sel_eng()), UI_H(ge_freeze), UI_H(ui_acc);
    for (k = 0; k < 4u; k++) {
        volatile int32_t *p = knob_param(page, k);
        UI_H(p ? *p : -1);
    }
    for (t = 0; t < NT; t++)
        UI_H(trk[t].level), UI_H(trk[t].dly), UI_H(trk[t].rev), UI_H(trk[t].grn), UI_H(trk[t].pan), UI_H(trk[t].eng);
    UI_H(trk[sel].fm_alg), UI_H(trk[sel].fm_fb), UI_H(fm_op), UI_H(trk[sel].p[ENG_FM][0]);
    UI_H(trk[sel].latch), UI_H(trk[sel].chord), UI_H(trk[sel].ch_type), UI_H(trk[sel].ch_voice), UI_H(sc_root), UI_H(sc_scale);
    if (PAGES[page].btn == BTN_SEL) UI_H(ch_last), UI_H(ui_oct);
    UI_H(trk_quiet);
    if (page == PG_PERFORM) UI_H(pf_held), UI_H(ge_freeze);
    if (page == PG_SONG) {                      /* the chain, the step playing, the slots' notes */
        UI_H(song_at), UI_H(song_left), UI_H(song_rec), UI_H(song_ri);
        for (k = 0; k < SONG_N; k++) UI_H(song_pat[k]), UI_H(song_rep[k]);
        for (k = 0; k < 16u; k++) UI_H(slot_has(k >> 2, k & 3u));
    }
    if (page == PG_ARP) {                       /* the list, the step playing */
        UI_H(trk[sel].arp), UI_H(arp[sel].n), UI_H(arp[sel].last), UI_H(trk[sel].latch);
        for (k = 0; k < ARP_N; k++) UI_H(arp[sel].note[k]), UI_H(arp[sel].id[k]);
    }
    if (PAGES[page].btn == BTN_SAVE) {
        UI_H(sv_slot), UI_H(sv_type), UI_H(pj_slot), UI_H(slot_cur), UI_H(st_gen), UI_H(seq_recall);
        for (t = 0; t < 4u; t++) UI_H(slots[t][0]);
    }
    if (page == PG_SOUND && sel_eng() == ENG_SAMPLE) {
        k = sp_slot(trk[sel].p[ENG_SAMPLE]);
        UI_H(smp[k].state), UI_H(smp[k].ready), UI_H(smp[k].n);
    }
    if (page == PG_TAPE) UI_H(tf_wow), UI_H(tf_sat), UI_H(tf_tone), UI_H(tf_hiss);
    if (page == PG_SYSTEM || page == PG_SYS2) { /* the settings, the links */
        UI_H(set.col), UI_H(set.mch), UI_H(set.tune), UI_H(set.scr), UI_H(set.out), UI_H(set.nlk);
        for (k = 0; k < NLINK * 5u; k++) UI_H(set.lk[k / 5u][k % 5u]);
    }
    if (PAGES[page].btn == BTN_SEQ) {           /* the pattern, its settings, the cursor, the step playing */
        const seq_trk_t *S = &seq[sel];
        const seq_pat_t *P = &S->pat[clamp(S->cur, 0, SEQ_NPAT - 1)];
        UI_H(S->steps), UI_H(S->div), UI_H(S->swing), UI_H(S->mode), UI_H(S->cursor), UI_H(seq_on), UI_H(S->pos);
        UI_H(seq_bpm);
        for (k = 0; k < SEQ_N; k++)
            UI_H(P->st[k].note), UI_H(P->st[k].chance), UI_H(P->st[k].ratchet), UI_H(P->st[k].len),
                UI_H(P->st[k].chord), UI_H(P->st[k].lmask);
    }
#undef UI_H
    return h;
}

/* ---- the screen: the boot, then the page, only what changed */
/* Boot, UI_BOOT_MS at power-on (the project loaded before): the eye opens over the first second, the drips run down from
 * the bar over the three; only those two drawn again, a frame every UI_ANIM_MS */
static const int8_t DRIP[30] = {0, 3, 0, 0, 6, 0, 2, 0, 0, 0, 8, 0, 0, 1, 0, 0, 4, 0, 0, 0, 7, 0, 0, 2, 0, 0, 5, 0, 0, 0};
static uint32_t ui_boot_t, bt_k, bt_d;          /* from when; the lids, the drips' time (0 .. UI_BOOT_MS) shown */
#define BOOT_LINE "FM-1 / DEATHDREAM / V" APP_VER
static const char *bt_note;                     /* the boot screen's last line, if not BOOT_LINE ("INSTALLING SAMPLES") */
static int32_t bt_len(uint32_t x) { return DRIP[x] * (int32_t)bt_d / (int32_t)UI_BOOT_MS; }   /* a drip's cells now */
static void ui_boot_draw(void)                  /* Boot: the eye, GR!MM, the drips */
{
    int32_t x, y;
    ui_eye(80, 48, (int32_t)bt_k, 0, 0);
    ui_text(120 - 12 * 5, 100, APP_NAME, 3, UI_L);
    for (y = 0; y < 10; y++)
        for (x = 0; x < 30; x++)
            if (!y || y <= bt_len((uint32_t)x)) ui_rect(60 + x * 4, 128 + y * 4, 4, 4, y ? ui_acc : UI_M);
    ui_text(120 - 4 * ui_len(bt_note ? bt_note : BOOT_LINE), 210, bt_note ? bt_note : BOOT_LINE, 1, bt_note ? ui_acc : UI_M);
}
static void ui_boot(void)
{
    ui_boot_t = fm1_ms, bt_k = bt_d = 0;
    ui_region(0, 0, 240, 240, UI_K, ui_boot_draw);
}
static void ui_boot_frame(void)
{
    static uint32_t t;
    uint32_t e = fm1_ms - ui_boot_t, k = e >= 1000u ? 4u : e * 4u / 1000u, x, was = 0, now = 0;
    if (fm1_ms - t < UI_ANIM_MS) return;
    t = fm1_ms;
    if (k != bt_k) {
        bt_k = k;
        ui_region(80, 48, 80, 40, UI_K, ui_boot_draw);
    }
    for (x = 0; x < 30u; x++) was += (uint32_t)bt_len(x);
    bt_d = e;
    for (x = 0; x < 30u; x++) now += (uint32_t)bt_len(x);
    if (now != was) ui_region(60, 132, 120, 36, UI_K, ui_boot_draw);   /* (a drip a cell longer) */
}

static void ui_update(int32_t oct)
{
    static uint32_t shown_page = 0xFFFFFFFFu, pic_key, pic_t, stat_key, dot_shown, shown_acc;
    static int32_t shown_bat = -2;
    static char hb[12], hl[24], hr[12];
    uint32_t k, key, st;
    static uint32_t an_t;
    int all = page != shown_page || ui_acc != shown_acc;   /* (a new COLOUR: everything again) */
    if (fm1_ms - ui_boot_t < UI_BOOT_MS) {      /* (the boot screen still) */
        ui_boot_frame();
        return;
    }
    if (all) {
        if (shown_page == 0xFFFFFFFFu)
            lcd_fill(0, 0, 240, 240, UI_K);     /* (after the boot screen: all of it cleared once) */
        shown_page = page, shown_acc = ui_acc;
        for (k = 0; k < 4u; k++) ui_slab_shown[k].lab[0] = '\x01';
        pic_key = ~ui_pic_key();
    }
    ui_head_text();
    if (all || !same(hb, ui_hb) || !same(hl, ui_hl) || !same(hr, ui_hr) || ui_bat != shown_bat) {
        put_s(hb, ui_hb), put_s(hl, ui_hl), put_s(hr, ui_hr), shown_bat = ui_bat;
        ui_region(0, 0, 240, 24, UI_K, ui_head_draw);
    }
    ui_oct = oct;
    if (page == PG_HOME && fm1_ms - an_t >= UI_ANIM_MS) {   /* HOME: a frame of its moving picture (what moved drawn,
                                                 * unless all of it is about to be) */
        an_t = fm1_ms;
        ui_home_frame(!all);
    }
    key = ui_pic_key();
    if (key != pic_key && (all || fm1_ms - pic_t >= UI_PIC_MS)) {
        pic_key = key;
        pic_t = fm1_ms;
        stat_key = 0xFFFFFFFFu;
        dot_shown = page == PG_LFO ? lfo_dot() : 0xFFFFFFFFu;
        ui_region(0, 24, 240, 141, UI_K, ui_pic_draw);
    }
    if (page == PG_LFO && lfo_dot() != dot_shown) {   /* LFO: the dot moved: its two columns where it was, and where
                                                 * it is, drawn again */
        if (dot_shown < 54u) ui_region(PX0 + (int32_t)dot_shown * 4, PY0, 8, 120, UI_K, ui_pic_draw);
        dot_shown = lfo_dot();
        ui_region(PX0 + (int32_t)dot_shown * 4, PY0, 8, 120, UI_K, ui_pic_draw);
    }
    if (page == PG_SYS2) {                      /* SYSTEM 2's CPU row (every ~0.5 s) */
        st = pct10(cpu_now) << 16 ^ pct10(cpu_peak);
        if (st != stat_key) {
            stat_key = st;
            ui_region(PX0, 144, 216, 15, UI_K, ui_pic_draw);
        }
    }
    for (k = 0; k < 4u; k++) {
        ui_slab_make(k, &ui_sl[k]);
        if (!same(ui_sl[k].lab, ui_slab_shown[k].lab) || !same(ui_sl[k].val, ui_slab_shown[k].val) ||
            ui_sl[k].fill != ui_slab_shown[k].fill || ui_sl[k].acc != ui_slab_shown[k].acc) {
            ui_slab_shown[k] = ui_sl[k];
            ui_k = k;
            ui_region(3 + (int32_t)k * 59, 165, 56, 72, UI_K, ui_slab_draw);
        }
    }
}
