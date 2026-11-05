#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 THEMES, stage 1: the BRUTAL theme in host mode (tests/run_host.sh; EYE's own pixels: tests/theme_test.py).
  - SYSTEM 3's THEME (KNOB 3) is saved with the settings, and read before the boot screen, which then follows it
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

bf = film("boot", [], 6500, ENVB, fps=20)
b2, b4 = bf(2000), bf(4500)
check("the boot screen follows the saved THEME: BRUTAL's red and white, none of EYE's red; the wordmark in the BRUTAL face (white, 24 px); the eye as in EYE",
      count(b4, RED) > 100 and count(b4, EYE_RED) == 0 and count(b4, BW, (60, 100, 180, 126)) > 300 and count(b2, RED, (72, 40, 168, 92)) > 40,
      f"{count(b4, RED)} {count(b4, EYE_RED)} {count(b4, BW, (60, 100, 180, 126))} {count(b2, RED, (72, 40, 168, 92))}")

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

# ---- the boot screen: the eye opens and looks around as in EYE: its box, frame by frame (every 50 ms), the same pixels in the theme's colours; the red of the eye,
# the bats and the drips in the same places
ef = film("boot_eye", [], 6500, None, fps=20)
EYEPAL = rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")])[:6]


def roles(ppm, pal):
    w, d = px(ppm)
    r = {c: i for i, c in enumerate(pal)}
    return [r.get(tuple(d[3 * i:3 * i + 3]), 9) for i in range(w * 240)]


diff_eye, diff_red, nf = [], [], 0
for ms in range(0, 5600, 50):
    a, b = roles(ef(ms), EYEPAL), roles(bf(ms), PAL[:6])
    nf += 1
    if [a[y * 240 + x] for y in range(40, 92) for x in range(74, 166)] != [b[y * 240 + x] for y in range(40, 92) for x in range(74, 166)]:
        diff_eye.append(ms)
    if [i for i, v in enumerate(a) if v == 5] != [i for i, v in enumerate(b) if v == 5]:
        diff_red.append(ms)
check(f"the BRUTAL boot's eye is EYE's, frame by frame: the eye's box (74 .. 165, 40 .. 91) has the same pixels, in the theme's colours, in all {nf} frames (every 50 ms, 0 .. 5.55 s: "
      "it opens, trembles, looks around, blinks), and the red (the eye's iris, the bats' eyes, the drips) is in the same places",
      not diff_eye and not diff_red, f"eye differs {diff_eye[:6]}, red differs {diff_red[:6]}")

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
