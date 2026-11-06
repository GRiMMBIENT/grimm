#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 THEMES, stage 1: the BRUTAL theme in host mode (tests/run_host.sh; EYE's own pixels: tests/theme_test.py).
  - SYSTEM 3's THEME (KNOB 3) is saved with the settings, and read before the boot screen, which then follows it: BRUTAL's is a concrete door (slabs slam shut,
    the eye opens in the seam, GR!MM is stamped, the door slides open on HOME), its eye EYE's frame for frame; nothing is left of the door after it
  - BRUTAL's HOME: the header (the held button's name a black chip, the grey title bar, the red tag), the four tracks as tiles, the knob blocks with
    their 3 px value bars, the selected block (the knob turned last); the messages over the whole header; the chips CHD, CUT, MUTE
  - every page in BRUTAL's frame (EYE's pictures inside it for now): the margins and gaps black, the blocks in place, none of EYE's red
  - the polygon and line drawing, by the size of what it draws
  python3 tests/brutal_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/brutal")
OUT.mkdir(parents=True, exist_ok=True)
BOOT = 6000
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  brutal: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def film(name, ev, end, env=None, fps=4):
    """-> frame(ms): the PPM of the frame at ms (the next one: a frame is drawn after its time)"""
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    subprocess.run([EXE, str(OUT / name), str(path), str(fps)], capture_output=True, check=True, env=dict(os.environ, **(env or {})))
    return lambda ms: (OUT / f"{name}-{ms * fps // 1000 + 1:05d}.ppm").read_bytes()


def px(ppm):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", ppm)
    return int(m.group(1)), ppm[m.end():]


def pixel(ppm, x, y):
    w, d = px(ppm)
    return tuple(d[3 * (y * w + x):3 * (y * w + x) + 3])


def count(ppm, col, box=None):
    w, d = px(ppm)
    x0, y0, x1, y1 = box or (0, 0, w, 240)
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) == col)


def npage():
    src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
    m = re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S)
    return len(re.findall(r"PG_\w+", m.group(1)))


def c888(r, g, b):                              # a colour as host mode writes a pixel (RGB565 to 8 bits a channel)
    return ((r >> 3) * 255 // 31, (g >> 2) * 255 // 63, (b >> 3) * 255 // 31)


def rgbs(text):
    return [c888(*(int(x, 16) for x in m)) for m in re.findall(r"RGB\(0x(..), 0x(..), 0x(..)\)", text)]


src = Path("firmware/src/brutal.c").read_text(encoding="utf-8")
ui_src = Path("firmware/src/ui.c").read_text(encoding="utf-8")
PAL = rgbs(src[src.index("UI_PAL_BRUTAL = "):src.index("/* ---- the font")])    # black, dark, mid, light, white, 4 accents, 3 meter colours
BK, BD, BM, BL, BW = PAL[:5]
RED = PAL[5]
EYE_RED = rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")])[5]
BR = {n: rgbs(m)[0] for n, m in re.findall(r"#define (BR_\w+) (RGB\(0x.., 0x.., 0x..\))", src)}
BAR, C1, C2, C3, TRACK, SELBG = (BR[n] for n in ("BR_BAR", "BR_C1", "BR_C2", "BR_C3", "BR_TRACK", "BR_SELBG"))
N = npage()

# ---- THEME: saved with the settings, read before the boot screen
SET, SETE = OUT / "set_brutal.bin", OUT / "set_eye.bin"
for f in (SET, SETE):
    f.unlink(missing_ok=True)
SYS3 = [(6100, "down 8"), (6900, "up 8"), (7000, "btn 8"), (7100, "btn 8")]    # HOME held: SYSTEM; HOME: SYSTEM 2, SYSTEM 3
film("mkset", SYS3 + [(7300, "enc 4 1")], 11000, {"GRIMM_SETTINGS_OUT": str(SET)})
film("mkeye", SYS3 + [(7300, "enc 4 1"), (7600, "enc 4 -1")], 11000, {"GRIMM_SETTINGS_OUT": str(SETE)})
sb, se = SET.read_bytes(), SETE.read_bytes()
check("THEME (SYSTEM 3, KNOB 3) turned to BRUTAL is saved with the settings (170 B, the last byte's bit 4); turned back to EYE: bit 4 clear",
      len(sb) == 170 and sb[-1] & 0x30 == 0x10 and se[-1] & 0x30 == 0, f"{sb[-3:].hex()} {se[-3:].hex()}")
ENVB = {"GRIMM_SETTINGS_IN": str(SET)}

# ---- HOME: the header, the tiles, the knob blocks and their bars
hf = film("home", [(6300, "enc 4 1")], 8200, ENVB)
h1, h2 = hf(6000), hf(7500)
P = pixel
check("HOME: the held button's name a black chip at the left of the grey title bar, the red tag at the right; black margins round the header",
      P(h1, 10, 12) == BK and P(h1, 120, 6) == BAR and P(h1, 200, 5) == RED and P(h1, 1, 1) == BK and P(h1, 238, 20) == BK and P(h1, 120, 30) == BK)
check("HOME: the four tracks as tiles: T1 (selected) black with a red frame, T2 / T4 the darker concrete, T3 the lighter; each with its level bar",
      P(h1, 60, 60) == BK and P(h1, 3, 60) == RED and P(h1, 150, 50) == C2 and P(h1, 60, 130) == C1 and P(h1, 200, 150) == C2 and
      P(h1, 20, 98) == BW and P(h1, 105, 98) == SELBG and P(h1, 135, 98)[0] < 60 and P(h1, 220, 98) == TRACK,
      str([P(h1, 60, 60), P(h1, 3, 60), P(h1, 150, 50), P(h1, 60, 130), P(h1, 200, 150), P(h1, 20, 98), P(h1, 105, 98), P(h1, 135, 98), P(h1, 220, 98)]))
check("HOME: the four knob blocks (VOLUME, CUTOFF, RES, FX) concrete, 42 px tall at the bottom, each with a 3 px value bar (black as far as the knob, a lighter track beyond)",
      all(P(h1, 6 + 59 * k, 200) == C3 for k in range(4)) and P(h1, 6, 193) == BK and P(h1, 6, 238) == BK and P(h1, 20, 232) == BK and
      P(h1, 50, 232) == TRACK and P(h1, 20, 235) == C3,
      str([P(h1, 6, 193), P(h1, 6, 238), P(h1, 20, 232), P(h1, 50, 232), P(h1, 20, 235)]))
check("a knob turned (KNOB 3): its block is the selected one: black, a red frame, the bar red; the others stay concrete",
      P(h2, 125, 215) == BK and P(h2, 121, 215) == RED and P(h2, 130, 232) in (RED, SELBG) and P(h2, 6, 200) == C3 and P(h2, 65, 200) == C3 and P(h2, 183, 200) == C3,
      str([P(h2, 125, 215), P(h2, 121, 215), P(h2, 130, 232), P(h2, 6, 200), P(h2, 65, 200), P(h2, 183, 200)]))

# ---- messages and chips
ev = [(6100, "down 3"), (6800, "up 3"),                       # T1: chord on: CHORD ON
      (7200, "enc 1 1"), (7300, "down 3"), (8000, "up 3"),    # T2: chord on
      (8400, "enc 3 -127"),                                   # T2's filter closed: CUT
      (9000, "down 7"), (9050, "keys 1"), (9100, "keys 0"), (9200, "up 7")]   # GLO + F3: T1 muted
cf = film("chips", ev, 12000, ENVB)
m1, m2, c3, c4 = cf(6900), cf(8100), cf(10800), cf(11500)
check("a message (CHORD ON) is the whole header, red, its text white; after its 1.5 s the bar is back",
      P(m1, 100, 8) == RED and P(m1, 230, 8) == RED and P(m1, 5, 5) == RED and P(c4, 100, 6) == BAR and P(c4, 200, 5) == RED and count(m1, BW, (3, 3, 237, 29)) > 40,
      f"{P(m1, 100, 8)} {P(c4, 100, 6)} {count(m1, BW, (3, 3, 237, 29))}")
check("HOME's chips: CHD where a track's chord mode is on (T1 on a concrete tile: black; T2, the selected tile: white), CUT red under it on T2, MUTE red on T1",
      P(m2, 10, 66) == BK and P(m2, 129, 66) == BW and P(c3, 129, 66) == BW and P(c3, 129, 78) == RED and P(c4, 83, 66) == RED,
      str([P(m2, 10, 66), P(m2, 129, 66), P(c3, 129, 66), P(c3, 129, 78), P(c4, 83, 66)]))

# ---- every page
ev = [(BOOT + 500 * n, f"page {n}") for n in range(N)]
pf = film("pages", ev, BOOT + 500 * N + 300, ENVB)
bad_pages = []
for n in range(N):
    im = pf(BOOT + 500 * n + 400)
    ok = P(im, 120, 6) in (BAR, RED) and P(im, 1, 100) == BK and P(im, 1, 1) == BK and count(im, EYE_RED) == 0
    if n != 8:                                                 # (MIXER 1: the tiles are the whole page)
        ok = ok and P(im, 120, 30) == BK and P(im, 120, 31) == BK and P(im, 120, 238) == BK and P(im, 120, 239) == BK and P(im, 6, 198) in (C3, BD, BK)
    if not ok:
        bad_pages.append(n)
check(f"all {N} pages in BRUTAL's frame: the grey header and red tag, black margins and gaps (the header, the picture and the blocks never overlap), the knob blocks, "
      "no EYE red anywhere", not bad_pages, str(bad_pages))

# ---- polygon and lines: the size of what is drawn
pm = film("prim", [(6100, "prim"), (6200, "page 1")], 7500, ENVB)(7000)
w_, d_ = px(pm)
on = lambda x, y: tuple(d_[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) == BW
tri = count(pm, BW, (0, 95, 100, 155))
hor_rows = [y for y in range(30, 50) if on(150, y)]
hor_cols = [x for x in range(90, 215) if on(x, 39)]
sla = count(pm, BW, (145, 115, 215, 165))
check("polygon and line drawing: a triangle of 1500 px (within 1 %), a 5 px line (rows 37 .. 41, columns 97 .. 201: square ends), the V's corner sharp (a miter, "
      "3 sqrt 2 over its point), a 4 px slant of 304 px (within 4 %)",
      abs(tri - 1500) <= 15 and hor_rows == list(range(37, 42)) and hor_cols[0] == 97 and hor_cols[-1] == 201 and on(140, 56) and not on(140, 55) and abs(sla - 304) <= 12,
      f"tri {tri} rows {hor_rows} cols {hor_cols[:1]} {hor_cols[-1:]} tip {on(140, 56)} {on(140, 55)} slant {sla}")

# ---- the fonts: Archivo Black only big, Terminus Bold for all the small text
fsrc = Path("firmware/gen/brutal_font.h").read_text(encoding="utf-8")
sizes = sorted(int(n) for n in re.findall(r"static const br_font_t BRF(\d+) =", fsrc))
small_use = re.findall(r"BRF(?:8|10|12|15)\b", src + ui_src)
check("fonts: Archivo Black has only its big sizes (16 and 24 px) and nothing uses a smaller one; the small text (chip, tag label, block labels, small values, messages, "
      "the boot line) is Terminus Bold (6x12, 8x14, 8x16)",
      sizes == [16, 24] and not small_use and all(f"BRT{n} = " in fsrc for n in ("12", "14", "16")) and src.count("&BRT") >= 12 and "&BRT12" in ui_src,
      f"{sizes} {small_use}")
w_, d_ = px(h1)
rows = lambda x0, x1, y0, y1: [y for y in range(y0, y1) if any(tuple(d_[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) == BK for x in range(x0, x1))]
lab = rows(8, 55, 196, 215)                                    # (VOLUME: the label's ink, Terminus 8x14 bold: 10 rows from y + 5)
check("a block's label is Terminus Bold 8x14: its capitals 10 rows tall, from 5 px under the block's top (200 .. 209)", lab[0] == 200 and lab[-1] == 209 and len(lab) == 10, str(lab))

# ---- the boot screen: BRUTAL's concrete door (EYE's intro is theme_test's). Screenshots at known times: "T shot" (a frame is drawn every 55 ms: the shot 3 ms after one)
def shots(name, ts, end, env=None, extra=()):
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {c}\n" for t, c in sorted(list(extra) + [(t, "shot") for t in ts])) + f"{end} end\n")
    subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, check=True, env=dict(os.environ, **(env or {})))
    return lambda t: (OUT / f"{name}-{t}.ppm").read_bytes()


DOORT = [403, 773, 833, 1503, 2503, 4003, 4953, 5393, 5503, 5703]
dr = shots("door", DOORT, 6500, ENVB)
d04, d08, dsh, d15, d25, d40, d50, d54, d55, d57 = (dr(t) for t in DOORT)
check("the door, 0 .. 0.8 s: two concrete slabs (the mockup's picture ground: faint lines) slide in from the top and the bottom, faster and faster (0.4 s: 27 rows each; "
      "0.77 s: 110), black between; none of EYE's colours",
      P(d04, 120, 5) == C1 and P(d04, 120, 10) == C2 and P(d04, 120, 60) == BK and P(d04, 120, 236) == C1 and P(d04, 120, 180) == BK and P(d08, 120, 50) in (C1, C2) and
      P(d08, 120, 115) == BK and P(d08, 120, 95) == C1 and count(d04, EYE_RED) == 0,
      str([P(d04, 120, 5), P(d04, 120, 10), P(d04, 120, 60), P(d04, 120, 236), P(d08, 120, 115)]))
check("each slab's closing edge is a heavy one: a 6 px black band (0.4 s: rows 21 .. 26 of the top slab; 0.77 s: about 105 .. 110) with the darker concrete's 2 px line just inside it (19 .. 20; about 103 .. 104)",
      P(d04, 120, 20) == BM and P(d04, 120, 23) == BK and P(d04, 120, 18) == C1 and P(d08, 120, 103) == BM and P(d08, 120, 107) == BK and P(d08, 120, 100) == C1,
      str([P(d04, 120, 20), P(d04, 120, 23), P(d08, 120, 103), P(d08, 120, 107)]))
check("the slam: the screen shakes 2 px (the door 2 px down: black rows at the top, the seam at 121 .. 122); then still: a 2 px red seam at 119 .. 120, concrete on both sides",
      P(dsh, 20, 0) == BK and P(dsh, 20, 1) == BK and P(dsh, 20, 121) == RED and P(dsh, 20, 122) == RED and P(d15, 20, 119) == RED and P(d15, 20, 120) == RED and
      P(d15, 20, 100) == C1 and P(d15, 20, 140) == C1, str([P(dsh, 20, 0), P(dsh, 20, 121), P(d15, 20, 119), P(d15, 20, 100)]))
check("shut: the two black bands are one solid seam (rows 114 .. 125) with the thin red line in the middle (119 .. 120), the darker line each side of it (112 .. 113, 126 .. 127), and the "
      "slabs' outer edges, a 2 px dark line, at the screen's top and bottom (0 .. 1, 238 .. 239); the shake moves it all (2 .. 3)",
      all(P(d15, 20, y) == BK for y in (114, 116, 118, 121, 123, 125)) and P(d15, 20, 112) == BM and P(d15, 20, 113) == BM and P(d15, 20, 126) == BM and P(d15, 20, 127) == BM and
      P(d15, 20, 111) == C1 and P(d15, 20, 128) == C1 and P(d15, 20, 0) == BM and P(d15, 20, 1) == BM and P(d15, 20, 238) == BM and P(d15, 20, 239) == BM and P(dsh, 20, 2) == BM,
      str([P(d15, 20, y) for y in (111, 112, 114, 119, 126, 128, 0, 239)]))
check("the eye in the seam (2.5 s: open, a red iris, the seam either side of it); 4.0 s: GR!MM stamped in black (Archivo Black 24 px), the version line under it (Terminus Bold); "
      "no bats, no stars, no drips (nothing but concrete round them)",
      count(d25, RED, (80, 100, 160, 140)) > 10 and P(d25, 20, 119) == RED and P(d25, 200, 120) == RED and count(d25, BK, (60, 150, 180, 172)) == 0 and
      count(d40, BK, (60, 150, 180, 172)) > 150 and count(d40, BK, (40, 184, 200, 196)) > 80 and count(d40, RED, (0, 130, 240, 240)) == 0 and
      count(d40, BK, (0, 0, 240, 90)) == 0 and count(d40, BD, (0, 0, 240, 240)) == 0 and count(d40, BM, (0, 150, 240, 236)) == 0,
      str([count(d25, RED, (80, 100, 160, 140)), count(d40, BK, (60, 150, 180, 172)), count(d40, BK, (40, 184, 200, 196))]))
check("4.6 .. 5.6 s: the door slides open, top up, bottom down, HOME already behind it (5.0 s: the gap shows the tiles' black and red; 5.4 s: T1's black tile; the stamp goes down with the "
      "bottom slab); at 5.7 s the header is there: the intro lasted 5.6 s",
      P(d50, 60, 111) == BK and P(d50, 60, 108) == RED and P(d50, 100, 5) in (C1, C2) and P(d54, 60, 60) == BK and P(d54, 100, 5) in (C1, C2) and P(d55, 100, 5) in (C1, C2) and
      P(d57, 120, 6) == BAR and P(d57, 200, 5) == RED,
      str([P(d50, 60, 111), P(d50, 60, 108), P(d54, 60, 60), P(d55, 100, 5), P(d57, 120, 6)]))
# nothing left of the door after it opened: the screen at 5.7 s is the same as HOME drawn from clear (THEME switched off and on, then HOME again), and the margins are black
rf = shots("doorref", [9003], 10000, ENVB, [(7000, "theme 0"), (7500, "theme 1"), (8000, "page 1"), (8400, "page 7")])(9003)
same = d57 == rf
margins = all(P(d57, x, y) == BK for x in (0, 1, 2, 120, 237, 238, 239) for y in (29, 30, 31, 193, 194, 237, 238, 239)) and \
    all(P(d57, x, y) == BK for x in (0, 1, 2, 238, 239) for y in range(0, 240, 7))
check("the door leaves nothing behind: the screen 0.1 s after it (5.7 s) is pixel for pixel HOME drawn from clear (THEME off and on again, then HOME), the margins and gaps black",
      same and margins, f"same {same}, margins {margins}")

# only what moves is drawn: the pixels sent to the panel between two moments of the door (a probe's lcdpx), a frame every 55 ms
path = OUT / "doorpx.txt"
path.write_text("".join(f"{t} probe\n" for t in (1100, 3300, 3700, 4300, 4500, 4700, 5600)) + "6500 end\n")
out = subprocess.run([EXE, str(OUT / "doorpx"), str(path)], capture_output=True, text=True, check=True, env=dict(os.environ, **ENVB)).stdout
lcd = {int(ln.split()[1]): int(re.search(r" lcdpx (\d+)", ln).group(1)) for ln in out.splitlines() if ln.startswith("slabvals ")}
fr_eye, fr_stamp, fr_open = (lcd[3300] - lcd[1100]) / 40, (lcd[4300] - lcd[3700]) / 11, (lcd[5600] - lcd[4700]) / 16
check("only what moves is drawn: the eye's phase sends the eye's box a frame at most (108 x 62 = 6696 px with its outline; the whole screen is 57600), the stamp 3.6 s a few small regions, the door "
      "opening two strips and the stamp; no frame the whole screen but the slam's shake", fr_eye <= 6696 and fr_stamp <= 6696 and fr_open <= 25000,
      f"px a frame: eye {fr_eye:.0f}, stamp {fr_stamp:.0f}, opening {fr_open:.0f}")

# the eye: the same frames as EYE's, in the seam: EYE's eye box (74 .. 165, 40 .. 91) at m x 55 ms is BRUTAL's (74 .. 165, 92 .. 143) at 825 + m x 55 ms (the eye's clock starts 15 frames
# in), pixel for pixel in the eye's own colours (mid grey, white, the iris: the lid's light edge is concrete, as the door is, and the seam's two rows left out)
EYEPAL = rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")])[:6]
ME = list(range(3, 63))                                          # (after the shake: 3 frames; to 3.4 s: it opens, trembles, looks, blinks)
es = shots("doore", [55 * m + 3 for m in ME], 4000, {"GRIMM_BOOT_PLAIN": "1"})   # (EYE's intro without its stars and bats: the eye alone)
bs = shots("doorb", [825 + 55 * m + 3 for m in ME], 6000, ENVB)


def eye_rows(ppm, pal, y0):
    """-> {row: (the first x, the last x)} of the eye in EYE's frame (the pixels that are not its black ground), rows from y0"""
    w, d = px(ppm)
    out = {}
    for y in range(52):
        xs = [x for x in range(74, 166) if tuple(d[3 * ((y0 + y) * w + x):3 * ((y0 + y) * w + x) + 3]) != pal[0]]
        if xs:
            out[y] = (min(xs), max(xs))
    return out


def role_at(d, w, pal, x, y):
    c = tuple(d[3 * (y * w + x):3 * (y * w + x) + 3])
    return pal.index(c) if c in pal else 9


bad_px, bad_ring, poses = [], [], set()
for m in ME:
    ea, ba = es(55 * m + 3), bs(825 + 55 * m + 3)
    rows = eye_rows(ea, EYEPAL, 40)
    w, de = px(ea)
    _, db = px(ba)
    foot = {(x, y) for y, (x0, x1) in rows.items() for x in range(x0, x1 + 1)}
    poses.add(frozenset(foot))
    if any(role_at(de, w, EYEPAL, x, 40 + y) != role_at(db, w, PAL[:6], x, 92 + y) for x, y in foot if y not in (27, 28)):
        bad_px.append(m)
    ring = {(x + dx, y + dy) for x, y in foot for dx in range(-2, 3) for dy in range(-2, 3)} - foot
    if any(role_at(db, w, PAL[:6], x, 92 + y) != 0 for x, y in ring if y not in (27, 28) and 0 <= y < 52 and 0 <= x < 240):
        bad_ring.append(m)
check(f"the eye in the door is EYE's, frame for frame: {len(ME)} frames (3.4 s of it: shut, a flutter, startled wide open, looking round, the blink): every pixel of its shape (the mid grey, "
      f"the lids, white, red, the pupil) is EYE's own in every one ({len(poses)} different shapes); the two seam rows left out",
      not bad_px and len(poses) >= 8, f"differs at frames {bad_px[:8]}, {len(poses)} shapes")
check("and a 2 px black outline round every pixel of the eye's edge, following its shape in all those frames (the blink included): the ring round it is black, none of it concrete",
      not bad_ring, f"ring not black at frames {bad_ring[:8]}")
check("the outline on concrete, to look at (2.5 s, the eye open): black 2 px over its top (rows 98 .. 99), 2 px left of its second row (x 88 .. 89), concrete beyond that",
      P(d25, 120, 98) == BK and P(d25, 120, 99) == BK and P(d25, 120, 97) == C1 and P(d25, 88, 107) == BK and P(d25, 89, 107) == BK and P(d25, 87, 107) == C1,
      str([P(d25, 120, 98), P(d25, 120, 97), P(d25, 88, 107), P(d25, 87, 107)]))

# ---- the theme switch: the whole panel cleared and drawn again: no pixel of the other theme left, on any page, both ways (the switch with the page shown, as the editor makes it)
EYEC = set(PAL_E := rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")]))
BRUC = set(PAL) | {c for c in (BAR, C1, C2, C3, TRACK, SELBG, c888(0xe0, 0x70, 0x5f))}


def ramp(a, b):
    return {c888(a[0] + (b[0] - a[0]) * v // 127, a[1] + (b[1] - a[1]) * v // 127, a[2] + (b[2] - a[2]) * v // 127) for v in range(128)}


EYEC |= ramp((0x5e, 0x5b, 0x54), (0xe8, 0xe4, 0xd8))          # (MIXER 1's LEVEL ramp, each theme's)
BRUC |= ramp((0x6e, 0x6b, 0x66), (0xf2, 0xef, 0xe8))
EYE_ONLY, BRU_ONLY = EYEC - BRUC, BRUC - EYEC


def left(ppm, only):
    w, d = px(ppm)
    return sum(1 for i in range(w * 240) if tuple(d[3 * i:3 * i + 3]) in only)


ev = []
for n in range(N):
    ev += [(BOOT + 1500 * n, f"page {n}"), (BOOT + 1500 * n + 500, "theme 1"), (BOOT + 1500 * n + 1000, "theme 0")]
sf = film("switch", ev, BOOT + 1500 * N + 300)
leftovers = []
for n in range(N):
    t = BOOT + 1500 * n
    a, b = left(sf(t + 900), EYE_ONLY), left(sf(t + 1400), BRU_ONLY)
    if a or b:
        leftovers.append((n, a, b))
check(f"THEME switched with each of the {N} pages shown (EYE to BRUTAL, back to EYE): the whole panel is cleared and drawn again: not one pixel of the other theme "
      "is left (checked against the colours only that theme has, MIXER 1's ramp too)", not leftovers, f"(page, EYE pixels in BRUTAL, BRUTAL pixels in EYE): {leftovers[:6]}")

sys.exit(1 if bad else 0)
