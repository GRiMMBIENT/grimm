#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 THEMES: the MINIMAL theme, stage 1, in host mode (tests/run_host.sh; EYE's own pixels: tests/theme_test.py, BRUTAL's: tests/brutal_test.py).
  - SYSTEM 3's THEME (KNOB 3) has a third value, MINIMAL, and GLOW (KNOB 4, there only in MINIMAL) is saved with the settings (bits 5 and 6 of the same byte)
  - the header (a 1 px rule under it, the tag), the knob row (rules, dividers, the selected knob amber with an underline), HOME's four rows
  - GLOW: a dimmer halo colour 1 px round the text and the lines, none with GLOW off, redrawn whole when it is switched
  - the boot: EYE's eye, frame for frame (its shape, its iris) as a 1 px outline with the iris amber, white and black; no bats, stars or drips; GR!MM fades in, a line draws under it, the
    version line fades in, then a clean cut to HOME
  - every page in MINIMAL's frame, none of EYE's pixel font drawn, the theme switched on every page between all three themes leaves nothing of the one left
  python3 tests/minimal_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/minimal")
OUT.mkdir(parents=True, exist_ok=True)
BOOT = 6000
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  minimal: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, env=None, fps=4):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return subprocess.run([EXE, str(OUT / name), str(path), str(fps)], capture_output=True, text=True, check=True, env=dict(os.environ, **(env or {}))).stdout


def film(name, ev, end, env=None, fps=4):
    """-> frame(ms): the PPM of the frame at ms (the next one: a frame is drawn after its time)"""
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    run(name, ev, end, env, fps)
    return lambda ms: (OUT / f"{name}-{ms * fps // 1000 + 1:05d}.ppm").read_bytes()


def shots(name, ts, end, env=None, extra=()):
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    run(name, sorted(list(extra) + [(t, "shot") for t in ts]), end, env)
    return lambda t: (OUT / f"{name}-{t}.ppm").read_bytes()


def px(ppm):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", ppm)
    return int(m.group(1)), ppm[m.end():]


def P(ppm, x, y):
    w, d = px(ppm)
    return tuple(d[3 * (y * w + x):3 * (y * w + x) + 3])


def count(ppm, col, box=None):
    w, d = px(ppm)
    x0, y0, x1, y1 = box or (0, 0, w, 240)
    cols = col if isinstance(col, (set, frozenset)) else {col}
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) in cols)


def c888(r, g, b):                              # a colour as host mode writes a pixel (RGB565 to 8 bits a channel)
    return ((r >> 3) * 255 // 31, (g >> 2) * 255 // 63, (b >> 3) * 255 // 31)


def c565(r, g, b):
    return (r >> 3, g >> 2, b >> 3)


def halo(r, g, b):                              # minimal.c's mn_mix(c, 1, 3), as a pixel
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 // 3) * 255 // 31, (g6 // 3) * 255 // 63, (b5 // 3) * 255 // 31)


def rgbs(text):
    return [tuple(int(x, 16) for x in m) for m in re.findall(r"RGB\(0x(..), 0x(..), 0x(..)\)", text)]


msrc = Path("firmware/src/minimal.c").read_text(encoding="utf-8")
ui_src = Path("firmware/src/ui.c").read_text(encoding="utf-8")
bsrc = Path("firmware/src/brutal.c").read_text(encoding="utf-8")
src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
PGN = [n.lower() for n in re.findall(r"PG_(\w+)", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S).group(1))]
N = len(PGN)
MP = rgbs(msrc[msrc.index("UI_PAL_MINIMAL = "):msrc.index("static uint16_t mn_mix")])
MN = {n: tuple(int(x, 16) for x in m) for n, *m in re.findall(r"#define MN_(\w+) RGB\(0x(..), 0x(..), 0x(..)\)", msrc)}
BK, DK, MD, LT, WH = (c888(*c) for c in MP[:5])
AMB = c888(*MP[5])
RULE, TRACK, DIM, FILL = (c888(*MN[n]) for n in ("RULE", "TRACK", "DIM", "FILL"))
EP = [c888(*c) for c in rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")])[:6]]   # EYE's k, d, m, l, w, accent

# ---- THEME: MINIMAL is the third value; GLOW is the fourth knob, there only in MINIMAL; both saved with the settings
SYS3 = [(6100, "down 8"), (6900, "up 8"), (7000, "btn 8"), (7100, "btn 8")]    # HOME held: SYSTEM; HOME: SYSTEM 2, SYSTEM 3
for nm, ev in (("sm", [(7300, "enc 4 2"), (7600, "enc 5 1")]), ("sm0", [(7300, "enc 4 2"), (7600, "enc 5 1"), (7900, "enc 5 -1")]), ("sb", [(7300, "enc 4 1"), (7600, "enc 5 1")])):
    (OUT / f"{nm}.bin").unlink(missing_ok=True)
    run(nm, SYS3 + ev, 11000, {"GRIMM_SETTINGS_OUT": str(OUT / f"{nm}.bin")})
sm, sm0, sb = ((OUT / f"{n}.bin").read_bytes() for n in ("sm", "sm0", "sb"))
check("THEME turned to MINIMAL (SYSTEM 3, KNOB 3: the third value) and GLOW turned on (KNOB 4) are saved with the settings (170 B, the last byte's bits 4 .. 6: MINIMAL is 2, GLOW 1); "
      "GLOW turned off again: bit 6 clear; in BRUTAL KNOB 4 does nothing (the setting stays off)",
      len(sm) == 170 and sm[-1] & 0x70 == 0x60 and sm0[-1] & 0x70 == 0x20 and sb[-1] & 0x70 == 0x10, f"{sm[-1]:02x} {sm0[-1]:02x} {sb[-1]:02x}")
EM = {"GRIMM_SETTINGS_IN": str(OUT / "sm.bin")}                  # MINIMAL, GLOW on
EM0 = {"GRIMM_SETTINGS_IN": str(OUT / "sm0.bin")}               # MINIMAL, GLOW off
check("the saved MINIMAL with GLOW is read back at power-on: the boot screen and HOME follow it (no EYE pixel: the screen has none of EYE's palette)",
      count(film("rb", [], 7000, EM)(6500), set(EP[1:]) - {BK, DK, MD, LT, WH, AMB}) == 0)

# ---- HOME: the header, the rows, the knob row
hf = film("home", [(6300, "enc 4 60")], 8200, EM0)
h1, h2 = hf(6000), hf(7500)
check("HOME: the header has a 1 px rule under it (row 27, the rule's grey) and nothing else below the text rows; the button's name dim, the title light, the tag at the right (its label dim, its value light)",
      all(P(h1, x, 27) == RULE for x in (2, 100, 237)) and P(h1, 100, 26) == BK and P(h1, 100, 28) == BK and count(h1, LT, (0, 0, 240, 27)) > 80 and count(h1, DIM, (0, 0, 240, 27)) > 20,
      f"{P(h1, 100, 27)} {count(h1, LT, (0, 0, 240, 27))} {count(h1, DIM, (0, 0, 240, 27))}")
check("HOME: four rows (T1 .. T4, 40 px apart); the selected track (T1) amber: its name, its level bar's fill (79 %: 83 px of 106, the rest the track's dark grey); the others' bar fill grey; the sources dim",
      count(h1, AMB, (3, 36, 30, 52)) > 20 and P(h1, 150, 42) == AMB and P(h1, 195, 42) == TRACK and P(h1, 150, 82) == FILL and P(h1, 150, 122) == FILL and count(h1, DIM, (26, 36, 46, 52)) > 8,
      f"{count(h1, AMB, (3, 36, 30, 52))} {P(h1, 150, 42)} {P(h1, 195, 42)} {P(h1, 150, 82)}")
check("HOME: under each row its pattern's first 16 steps as small bars (an empty step the dark one: a 5 x 4 bar each 7 px)",
      all(P(h1, 102 + 7 * i, 57 + 40 * t) == DK for i in range(16) for t in range(4)), str([P(h1, 102, 57), P(h1, 214, 57)]))
check("HOME's knob row: a rule above (row 195) and a divider between the knobs (x 61, 120, 179: not after the last), each with its label dim, its value light, a 2 px value bar (the fill: grey, "
      "its track dark; VOLUME 79 %)",
      all(P(h1, x, 195) == RULE for x in (10, 100, 200)) and all(P(h1, 3 + 59 * k + 58, 215) == RULE for k in range(3)) and P(h1, 238, 215) == BK and P(h1, 10, 195 + 42 - 9) == FILL and
      P(h1, 50, 195 + 42 - 9) == TRACK and count(h1, DIM, (3, 196, 61, 215)) > 8 and count(h1, LT, (3, 196, 61, 240)) > 30, f"{P(h1, 10, 228)} {P(h1, 50, 228)}")
check("a knob turned (KNOB 3): its block is the selected one: its label and value amber, its bar amber, a 2 px amber underline (rows 235 .. 236); the others stay grey and light",
      count(h2, AMB, (121, 196, 178, 225)) > 25 and P(h2, 135, 228) == AMB and all(P(h2, 125 + i, 235) == AMB and P(h2, 125 + i, 236) == AMB for i in (0, 20, 40)) and
      count(h2, AMB, (3, 196, 61, 240)) == 0 and count(h2, AMB, (180, 196, 240, 240)) == 0, f"{count(h2, AMB, (121, 196, 178, 225))} {P(h2, 130, 228)} {P(h2, 130, 235)}")

# ---- messages
cf = film("msg", [(6100, "down 3"), (6800, "up 3")], 9000, EM0)
m1, m2 = cf(6900), cf(8800)
check("a message (CHORD ON) is the whole header's text, bold, amber (the rule stays); after its 1.5 s the title and the tag are back",
      count(m1, AMB, (0, 0, 240, 27)) > 60 and count(m1, LT, (0, 0, 240, 27)) == 0 and P(m1, 100, 27) == RULE and count(m2, AMB, (0, 0, 240, 27)) == 0 and count(m2, LT, (0, 0, 240, 27)) > 80,
      f"{count(m1, AMB, (0, 0, 240, 27))} {count(m1, LT, (0, 0, 240, 27))} {count(m2, LT, (0, 0, 240, 27))}")

# ---- GLOW: a halo, a third of the colour, 1 px round the text and the lines; none without it; switched on a page: redrawn whole
def t565(c):                                    # a colour (r, g, b) as the display has it: 5, 6, 5 bits
    return (c[0] >> 3, c[1] >> 2, c[2] >> 3)


def p888(t):
    return (t[0] * 255 // 31, t[1] * 255 // 63, t[2] * 255 // 31)


def lum5(t):                                    # minimal.c's mn_lum
    return (t[0] * 8 + t[1] * 4 + t[2] * 8) // 3


def mixt(t, a, n):                              # minimal.c's mn_mix
    return (t[0] * a // n, t[1] * a // n, t[2] * a // n)


MID5 = t565(MP[2])
INKS = [t565(c) for c in MP] + [t565(MN[n]) for n in MN if n != "SEL"]      # (the selected tile's fill is no ink)
INKS += [mixt(t565(c), a, 8) for c in (MP[3], MN["DIM"], MP[5], MN["FILL"]) for a in range(1, 9)]                # (the words fading in, the version line)
INKS += [tuple(x + y for x, y in zip(mixt(t565(MN["TRACK"]), 4 - n, 4), mixt(t565(MP[3]), n, 4))) for n in range(1, 5)]   # (the dots' ripple)
LEGIT = {p888(t) for t in INKS}
BRIGHT = [t for t in INKS if lum5(t) >= lum5(MID5)]
HLB = {p888(mixt(t, 1, 3)) for t in BRIGHT} - LEGIT              # (the halos of the ink as bright as the mid grey or brighter)
HLD = {p888(mixt(t, 1, 3)) for t in INKS if lum5(t) < lum5(MID5)} - LEGIT   # (what a dark ink's would be: none may be there)
HL = HLB
gh = film("glowhome", [], 7000, EM)(6500)
nh = film("noglowhome", [], 7000, EM0)(6500)
check(f"GLOW: with it the light text has a halo (the colour a third as bright, 1 px round every pixel of it: {count(gh, HL)} pixels of it on HOME); without it none ({count(nh, HL)}); the rule, "
      "dark, has none", count(gh, HL) > 300 and count(nh, HL) == 0 and P(gh, 100, 26) == BK and P(gh, 100, 28) == BK and P(gh, 100, 27) == RULE, f"{count(gh, HL)} {count(nh, HL)}")
ev, ts = [], []
for n in range(N):
    ev += [(BOOT + 1200 * n, f"page {n}"), (BOOT + 1200 * n + 400, "glow 0"), (BOOT + 1200 * n + 800, "glow 1")]
    ts += [BOOT + 1200 * n + d for d in (300, 700, 1100)]
gf = shots("glowswitch", ts, BOOT + 1200 * N + 300, EM, ev)
left = []
for n in range(N):
    t = BOOT + 1200 * n
    a, b, c = gf(t + 300), gf(t + 700), gf(t + 1100)
    if not count(a, HL) or count(b, HL) or not count(c, HL):
        left.append((PGN[n], count(a, HL), count(b, HL), count(c, HL)))
check(f"GLOW switched off and on with each of the {N} pages shown: the halo is gone from the whole page at once (none of its colours left), and back at once",
      not [x for x in left if x[0] not in ("track",)], f"(page, with, off, on again): {left[:6]}")


# the rule: with GLOW on, every pixel that differs from the same page with it off is a halo of a light ink (a third of a colour as bright as the mid grey or brighter) on a dark pixel: no halo round any
# ink darker than the mid grey (black text on an amber bar, dark segments, dark rules), on every page
ev = [(BOOT + 700 * n, f"page {n}") for n in range(N)]
ts = [BOOT + 700 * n + 450 for n in range(N)]
ga = shots("glowa", ts, BOOT + 700 * N + 300, EM, ev)
gb = shots("glowb", ts, BOOT + 700 * N + 300, EM0, ev)
wrong, seen = [], 0
for n in range(N):
    w, da = px(ga(ts[n]))
    _, db = px(gb(ts[n]))
    for k in range(w * 240):
        a, b = tuple(da[3 * k:3 * k + 3]), tuple(db[3 * k:3 * k + 3])
        if a != b:
            if (PGN[n] == "sys2" and 116 <= k // w < 190) or (PGN[n] == "sys3" and k // w >= 195 and k % w >= 177):   # (the CPU's figures move by themselves; GLOW's own knob reads ON, OFF)
                continue
            seen += 1
            if a not in HLB:                    # (a halo of a light ink, over what it touches: a dark pixel, or a grey line it passes)
                wrong.append((PGN[n], k % w, k // w, a, b))
check(f"GLOW on every page: it changes {seen} pixels in all, every one a halo of a light ink (a third of a colour at least as bright as the mid grey); none round a dark or black ink "
      "(black text on an amber bar, dark segments, dark rules)", not [x for x in wrong if x[0] != "track"] and seen > 2000, str(wrong[:5]))

# ---- every page in MINIMAL's frame
psrc = Path("firmware/src/brutalp.c").read_text(encoding="utf-8")
SHARED = {n.lower() for n in re.findall(r"case PG_(\w+):", psrc[psrc.index("static int bp_min_page"):psrc.index("static int mn_native")])}
STAGE = {"source", "perform", "mix2", "levels", "sounds", "projects", "pattern", "song", "sys3",                                         # (stage 2a)
         "env", "lfo", "pdline", "pdenv", "tape", "grain", "delay", "space", "master", "drive", "sound"}                                 # (stage 2b: SOUND for VA, PHASE)
check("the pages MINIMAL draws with BRUTAL's shared picture code (brutalp.c's bp_min_page) are the ones this test knows: " + " ".join(sorted(SHARED)), SHARED == STAGE, f"{sorted(SHARED ^ STAGE)}")
pf = film("pages", [(BOOT + 500 * n, f"page {n}") for n in range(N)], BOOT + 500 * N + 300, EM0)
bad_pages = []
for n in range(N):
    im = pf(BOOT + 500 * n + 400)
    ok = P(im, 100, 27) == RULE and P(im, 1, 100) == BK and P(im, 238, 100) == BK and P(im, 100, 28) == BK and (PGN[n] == "mix1" or P(im, 10, 195) == RULE) and count(im, set(EP[1:]) - {BK, DK, MD, LT, WH, AMB}) == 0
    if not ok:
        bad_pages.append(PGN[n])
check(f"all {N} pages in MINIMAL's frame: the header with its rule, the knob row with its rule, black margins, none of EYE's colours", not bad_pages, str(bad_pages))
ev, t = [], BOOT
for n in range(N):
    ev += [(t, "probe"), (t + 100, f"page {n}"), (t + 500, "probe")]
    t += 700
out = run("ptext", ev, t + 100, EM0)
v = [(int(re.search(r" ptext (\d+)", ln).group(1)), int(re.search(r" btext (\d+)", ln).group(1))) for ln in out.splitlines() if ln.startswith("slabvals ")]
pt = [(PGN[n], v[2 * n + 1][0] - v[2 * n][0], v[2 * n + 1][1] - v[2 * n][1]) for n in range(N)]
check(f"none of EYE's pixel font is drawn on any of the {N} pages (the header, the pictures, the knob row: Terminus); the pages' pictures' texts go through it ({sum(1 for p in pt if p[2])} pages: the ones still EYE's)",
      not any(p[1] for p in pt) and sum(1 for p in pt if p[2]) >= N - 2 - len(SHARED) - 3, str([p for p in pt if p[1]]))
fsrc = Path("firmware/gen/minimal_font.h").read_text(encoding="utf-8")
check("fonts: only Terminus (the regular 6x12 and 8x14: minimal_font.h; the bold ones: brutal_font.h); the theme uses no Archivo Black and no other face",
      "BRT12R" in fsrc and "BRT14R" in fsrc and "BRF" not in msrc and "br_text(" not in msrc, "")
sk = film("sys3m", [(6000, f"page {PGN.index('sys3')}")], 7000, EM0)(6500)
check("SYSTEM 3: GLOW is the fourth knob block (its label GLOW and its setting ON/OFF)", count(sk, LT, (180, 196, 240, 240)) > 20 and count(sk, DIM, (180, 196, 240, 215)) > 8)

# ---- the boot: EYE's eye frame for frame (outline, iris), no bats, stars or drips; GR!MM, its line, the version line; the cut to HOME
EYE_ROWS = ["....llllllll....", "..llmmmmmmmmll..", ".lmmmmaaaammmml.", "lmmmmaawkaammmml", "lmmmmaakkaammmml", ".lmmmmaaaammmml.", "..llmmmmmmmmll..", "....llllllll...."]
IRIS = ([".aaaa.", "aawkaa", "aakkaa", ".aaaa."], [".aaaa.", "aawaaa", "aaakaa", ".aaaa."])
ROLE = {"l": EP[3], "m": EP[2], "a": EP[5], "w": EP[4], "k": EP[0]}


def eye_cells(k, dx, dy, tiny):
    """-> {(row, col): char}: the cells of EYE's eye (ui_eye): its lids k, its iris at dx, dy"""
    out = {}
    for r in range(8):
        d = 3 - r if r < 4 else r - 4
        for c in range(16):
            ch = EYE_ROWS[r][c]
            if ch == "." or d > k:
                continue
            if d == k and k < 4:
                ch = "l"
            elif ch != "l":
                ir, ic = r - 2 - dy, c - 5 - dx
                ch = IRIS[tiny][ir][ic] if 0 <= ir < 4 and 0 <= ic < 6 and IRIS[tiny][ir][ic] != "." else "m"
            out[(r, c)] = ch
    return out


POSES = [(k, dx, dy, tiny, jx, jy) for k in (0, 1, 2, 4) for dx in (-2, -1, 0, 2) for dy in (-1, 0) for tiny in (0, 1) for jx in (-2, 0, 2) for jy in (-4, -2, 0)]


def match_eye(im):
    """-> the pose (k, dx, dy, tiny, jx, jy) whose EYE sprite is exactly this frame's eye (cell centres), or None"""
    for k, dx, dy, tiny, jx, jy in POSES:
        cells = eye_cells(k, dx, dy, tiny)
        x0, y0 = 80 + jx, 48 + jy
        if all(P(im, x0 + c * 5 + i, y0 + r * 5 + j) == (ROLE[cells[(r, c)]] if (r, c) in cells else EP[0]) for r in range(8) for c in range(16) for i, j in ((0, 0), (4, 0), (0, 4), (4, 4))):
            return k, dx, dy, tiny, jx, jy
    return None


def mini_eye(pose):
    """-> {(x, y): colour} of what MINIMAL draws for the pose: 1 px outline round the shape (UI_L), the iris in the cells' middle 3 x 3 (amber, white; the pupil is black)"""
    k, dx, dy, tiny, jx, jy = pose
    cells = eye_cells(k, dx, dy, tiny)
    x0, y0 = 80 + jx, 48 + jy
    out = {}
    for (r, c), ch in cells.items():
        ux, uy = x0 + c * 5, y0 + r * 5
        if (r - 1, c) not in cells:
            out.update({(ux + i, uy): LT for i in range(5)})
        if (r + 1, c) not in cells:
            out.update({(ux + i, uy + 4): LT for i in range(5)})
        if (r, c - 1) not in cells:
            out.update({(ux, uy + i): LT for i in range(5)})
        if (r, c + 1) not in cells:
            out.update({(ux + 4, uy + i): LT for i in range(5)})
    for (r, c), ch in cells.items():
        d = 3 - r if r < 4 else r - 4
        if EYE_ROWS[r][c] in ".l" or (d == k and k < 4):
            continue
        ir, ic = r - 2 - dy, c - 5 - dx
        if 0 <= ir < 4 and 0 <= ic < 6 and IRIS[tiny][ir][ic] in "awk":
            for i in range(3):
                for j in range(3):
                    out[(x0 + c * 5 + 1 + i, y0 + r * 5 + 1 + j)] = AMB if IRIS[tiny][ir][ic] == "a" else WH if IRIS[tiny][ir][ic] == "w" else BK
    return out


MB_EYE = 1650                                                    # (the thin eye's clock starts: 30 frames of 55 ms in: frame n of the boot is frame n - 30 of EYE's)
DOTS = [(4 + 8 * i, 4 + 8 * j) for j in range(30) for i in range(30)]
RING = lambda x, y: min((x - 4) // 8, 29 - (x - 4) // 8, (y - 4) // 8, 29 - (y - 4) // 8)
ME = list(range(2, 56))                                          # (EYE's frames, 55 ms apart: 0.1 s .. 3.5 s: shut, a flutter, startled wide open, looking round, the blink)
es = shots("bye", [55 * m + 3 for m in ME], 4000, {"GRIMM_BOOT_PLAIN": "1"})   # (EYE's intro without its stars and bats: the eye alone)
bs = shots("byb", [MB_EYE + 55 * m + 3 for m in ME], 5500, EM0)
EX0, EY0, EX1, EY1 = 70, 38, 172, 96                             # (the eye's region)
dots_in_box = {(x + a, y + b): TRACK for x, y in DOTS for a in (0, 1) for b in (0, 1) if EX0 <= x + a < EX1 and EY0 <= y + b < EY1}
bad_px, no_pose, poses = [], [], set()
for m in ME:
    ie, ib = es(55 * m + 3), bs(MB_EYE + 55 * m + 3)
    pose = match_eye(ie)
    if pose is None:
        no_pose.append(m)
        continue
    poses.add(pose)
    w, d = px(ib)
    got = {(x, y): tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) for y in range(EY0, EY1) for x in range(EX0, EX1) if tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) != BK}
    exp = dict(dots_in_box)
    exp.update(mini_eye(pose))
    exp = {k: v for k, v in exp.items() if v != BK}                  # (the pupil, black, covers the dots under it)
    if got != exp:
        bad_px.append(m)
        if len(bad_px) == 1:
            print("   first difference (frame", m, pose, "): drawn not expected", sorted(set(got) - set(exp))[:8], "expected not drawn", sorted(set(exp) - set(got))[:8],
                  [(k, got[k], exp[k]) for k in sorted(set(got) & set(exp)) if got[k] != exp[k]][:4])
check(f"the boot's eye is EYE's, frame for frame: {len(ME)} frames (EYE's 0.1 .. 3.5 s: shut, a flutter, startled wide open, looking round, the blink; {len(poses)} different poses, each found in EYE's frame "
      "cell by cell), 1.65 s later in the dot field (30 frames of the 55 ms grid): MINIMAL's eye region is exactly the faint dots and the 1 px outline round that shape with the iris (amber, white, black) "
      "in the same cells", not no_pose and not bad_px and len(poses) >= 8, f"EYE's pose unknown at {no_pose[:6]}; differs at {bad_px[:8]}; {len(poses)} poses")
# the dots
dt = shots("dots", [53, 153, 303, 603, 803, 1203, 1403, 1598, 2503, 3003, 4003, 4303], 4400, EM0)
d53, d153, d303, d603, d803, d1203, d1403, d1598, d2503, d3003, d4003, d4303 = (dt(t) for t in (53, 153, 303, 603, 803, 1203, 1403, 1598, 2503, 3003, 4003, 4303))


def lit_dots(im, box=(0, 0, 240, 240)):
    w, d = px(im)
    return [(x, y) for x, y in DOTS if box[0] <= x < box[2] and box[1] <= y < box[3] and tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) != BK]


check("the dot field fades in from the middle out: at 0.05 s a few dots, the middle first (the dot at the centre is there, a corner one not), at 0.15 s more, at 0.3 s more, all 900 at 0.6 s",
      0 <= len(lit_dots(d53)) < len(lit_dots(d153)) < len(lit_dots(d303)) < 900 and (124, 124) in lit_dots(d153) and (4, 4) not in lit_dots(d153) and len(lit_dots(d603)) == 900,
      f"{[len(lit_dots(x)) for x in (d53, d153, d303, d603)]}")
w_, dd = px(d603)
cols = {(x, y): tuple(dd[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) for x, y in DOTS}
ripple = [(x, y) for (x, y), c in cols.items() if c != TRACK]
dist = lambda x, y: ((x + 1 - 120) ** 2 + (y + 1 - 120) ** 2) ** 0.5
check(f"a ripple runs out from the centre, fast (240 px a second: at 0.6 s its front is about 70 px out: {len(ripple)} dots brightened, all in a ring 45 .. 78 px out, the brightest at its outer edge); "
      "the dots inside it are back to the faint grey", 40 <= len(ripple) <= 200 and all(45 <= dist(x, y) <= 78 for x, y in ripple) and max(dist(x, y) for x, y in ripple) >= 62 and
      all(cols[(x, y)] == TRACK for x, y in DOTS if dist(x, y) < 45), f"{len(ripple)} {sorted(set(round(dist(x, y)) for x, y in ripple))[:3]}")
w_, dd = px(d1203)
check("the ripple has crossed the whole field by 1.2 s (about 0.8 s after it left the centre): not a dot is brightened by it any more (all faint, or the eye's outline dots)",
      all(tuple(dd[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) in (TRACK, LT, AMB) for x, y in DOTS), "")
# the dots' own clock: a frame every 33 ms (the ripple moves in steps of 8 px: in 110 ms three or four steps, not two)
rc = shots("rclock", list(range(600, 711, 11)), 800, EM0)
nimg = len({rc(t) for t in range(600, 711, 11)})
check(f"the dots run on their own clock, 33 ms a frame (30 a second): in 110 ms of the ripple {nimg} different frames (3 or 4; EYE's 55 ms grid would give 2)", nimg in (3, 4), str(nimg))
def form(x, y):                                 # minimal.c's mb_form: the dot against the open eye (at 80, 48): 1 on its outline, 2 on its iris
    if x + 1 < 80 or y + 1 < 48:
        return 0
    c, r = (x + 1 - 80) // 5, (y + 1 - 48) // 5
    if c > 15 or r > 7 or EYE_ROWS[r][c] == ".":
        return 0
    ir, ic = r - 2, c - 5
    ch = IRIS[0][ir][ic] if 0 <= ir < 4 and 0 <= ic < 6 else "."
    if ch in "aw":
        return 2
    if ch == "k":
        return 0
    return 1 if r in (0, 7) or c in (0, 15) or any(EYE_ROWS[r + a][c + b] == "." for a, b in ((-1, 0), (1, 0), (0, -1), (0, 1))) else 0


OUTLINE = [(x, y) for x, y in DOTS if form(x, y) == 1]
nl = lambda im: sum(1 for x, y in OUTLINE if P(im, x, y) == LT)
check(f"the eye forms out of the dots: the dots along the open eye's outline ({len(OUTLINE)} of them) brighten one by one (light grey: {nl(d1203)} at 1.2 s, {nl(d1403)} at 1.4 s), all of them by 1.6 s "
      f"({nl(d1598)}), and the iris's amber ({sum(1 for x, y in DOTS if P(d1598, x, y) == AMB)} dots) with them",
      0 < nl(d1203) < nl(d1403) < nl(d1598) == len(OUTLINE) and sum(1 for x, y in DOTS if P(d1598, x, y) == AMB) >= 6, f"{nl(d1203)} {nl(d1403)} {nl(d1598)} {len(OUTLINE)}")
w_, dd = px(d2503)
rest = {(x, y) for y in range(240) for x in range(240) if tuple(dd[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) != BK and not (EX0 <= x < EX1 and EY0 <= y < EY1)}
want = {(x + a, y + b) for x, y in DOTS for a in (0, 1) for b in (0, 1) if not (EX0 <= x + a < EX1 and EY0 <= y + b < EY1)}
check("2.5 s: nothing on the screen outside the eye but the faint dots (2 px, every 8 px, the faint grey): no ripple, no bats, no stars, no drips", rest == want and all(P(d2503, x, y) == TRACK for x, y in DOTS if y > 100), f"{len(rest ^ want)}")


def mix565(c, a):                               # mn_mix(c, a, 8) as a pixel
    return ((c[0] >> 3) * a // 8 * 255 // 31, (c[1] >> 2) * a // 8 * 255 // 63, (c[2] >> 3) * a // 8 * 255 // 31)


BT = [3000, 3500, 3700, 3900, 4300, 4500, 5100]
bt = shots("bt", [t + 3 for t in BT], 6300, EM0)
f30, f35, f37, f39, f43, f45, f51 = (bt(t + 3) for t in BT)
wm = lambda im: count(im, {mix565(MP[3], a) for a in range(1, 9)}, (52, 98, 188, 126))
lin = lambda im: count(im, FILL, (50, 134, 190, 135))
ver = lambda im: count(im, {mix565(MN["DIM"], a) for a in range(1, 9)}, (0, 144, 240, 160))
check("GR!MM fades in under the eye (3.2 .. 3.8 s: none at 3.0 s, dim at 3.5 s, full light by 4.3 s), Terminus Bold 2x, the dots under the words gone (the field is there at 3.0 s, not in the words' box at 4.3 s)",
      wm(f30) == 0 and 0 < wm(f35) and count(f35, LT, (52, 98, 188, 126)) == 0 and count(f43, LT, (52, 98, 188, 126)) > 150 and count(f30, TRACK, (24, 96, 216, 160)) > 100 and
      count(f43, TRACK, (24, 96, 216, 160)) == 0, f"{wm(f30)} {wm(f35)} {count(f43, LT, (52, 98, 188, 126))} {count(f30, TRACK, (24, 96, 216, 160))} {count(f43, TRACK, (24, 96, 216, 160))}")
xs = [x for x in range(240) if P(f51, x, 134) == FILL]
check("a thin line draws out from the middle under it (3.7 .. 4.2 s): none at 3.7 s, 1 px, growing (3.9 s), as wide as the word (128 px) when done; the version line fades in after it (3.9 .. 4.5 s): none "
      "at 3.5 s, there at 5.1 s", lin(f37) == 0 and 0 < lin(f39) < 128 and lin(f43) == 128 and len(xs) == 128 and xs[0] == 56 and xs[-1] == 183 and ver(f35) == 0 and ver(f51) > 12 and ver(f39) < ver(f45),
      f"{lin(f37)} {lin(f39)} {lin(f43)} {len(xs)} {ver(f51)}")
vx = [x for y in range(144, 160) for x in range(240) if P(f51, x, y) != BK and P(f51, x, y) not in {TRACK}]
check("the intro's version line is just V3.0 (APP_VER without its -dXXXX stamp: 'V' and the digits up to the dash), Terminus Regular, in the middle: its ink between x 100 and 140", 12 < len(vx) and min(vx) >= 100 and max(vx) <= 140, f"{min(vx)} {max(vx)}")
sysh = film("syshead", [(6000, f"page {PGN.index('sys3')}"), (6400, f"page {PGN.index('system')}"), (6800, f"page {PGN.index('sys2')}")], 7400, EM0)
hx = [[x for y in range(5, 25) for x in range(150, 240) if P(im, x, y) == LT] for im in (sysh(6300), sysh(6700), sysh(7200))]
check("the SYSTEM pages' header in MINIMAL has the build's whole stamp (VER 3.0-dev here: the tag's value, 8 px a character, ends at 236 and starts at 180) so the installed build can be told; the intro shows only V3.0",
      all(h and 178 <= min(h) <= 184 and max(h) >= 230 for h in hx), str([(min(h), max(h)) for h in hx if h]))
out = run("bworst", [], 5800, EM0)
ms = [ln for ln in out.splitlines() if ln.startswith("mbstat ")]
wp, wr = (int(re.search(r": (\d+) px in (\d+) regions", ms[-1]).group(1)), int(re.search(r": (\d+) px in (\d+) regions", ms[-1]).group(2))) if ms else (0, 0)
spi = wp * 16 / 12e6 * 1000                                        # (ms: 16 bits a pixel at 12 MHz, the panel's SPI: lcd.c LCD_BAUD 4)
ovh = wr * 0.060                                                   # (ms: each region's window and commands, 60 us apiece: pessimistic)
check(f"the worst frame of the dots (the fade-in at 0.36 s: {wp} px in {wr} regions, {wr} runs of dots): {spi:.1f} ms of pixels at the panel's 12 MHz SPI, {ovh:.1f} ms of window set-up "
      f"(60 us a region, pessimistic): {spi + ovh:.1f} ms of a 33 ms frame", ms and spi + ovh < 20.0, f"{spi + ovh:.1f} ms")
# the end: the eye shuts, the dots fade out from the edges inward, then a clean cut to HOME
sh = shots("bend", [4953, 5253, 5353, 5503, 5703, 5903, 7003], 7500, EM0)
e49, e52, e54, e55, e57, e59, e70 = (sh(t) for t in (4953, 5253, 5353, 5503, 5703, 5903, 7003))
shut = mini_eye((0, 0, 0, 0, 0, 0))
w_, dd = px(e49)
got = {(x, y): tuple(dd[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) for y in range(EY0, EY1) for x in range(EX0, EX1) if tuple(dd[3 * (y * w_ + x):3 * (y * w_ + x) + 3]) != BK}
exp = dict(dots_in_box)
exp.update(shut)
exp = {k: v for k, v in exp.items() if v != BK}
check("4.8 .. 4.9 s the eye shuts (the thin outline: a flat bar, no iris), GR!MM and the version line stay", got == exp and count(e49, LT, (52, 98, 188, 126)) > 150 and ver(e49) > 12, f"{len(set(got) ^ set(exp))}")
free = lambda x, y: not (24 <= x < 216 and 96 <= y < 160)             # (the words' box: no dots)
gone = lambda im, rings: all(P(im, x, y) == BK for x, y in DOTS if RING(x, y) in rings and free(x, y))
there = lambda im, rings: sum(1 for x, y in DOTS if RING(x, y) in rings and free(x, y) and P(im, x, y) != BK)
check("the dots fade out from the edges inward, a ring at a time (4.9 .. 5.5 s): at 5.25 s the outer five rings are gone and the inner ones (9 on) all there; at 5.35 s the outer eight are gone, the ring 11 still there",
      gone(e52, range(0, 5)) and there(e52, range(9, 15)) == sum(1 for x, y in DOTS if RING(x, y) in range(9, 15) and free(x, y)) and gone(e54, range(0, 9)) and there(e54, range(11, 12)) > 0,
      f"{there(e52, range(9, 15))}")
check("the boot is as long as the others' (5.6 s): at 5.5 s it is still on (the words, the line, the version line); then a clean cut to HOME: at 5.7 s the screen is HOME and nothing of the boot is left on it "
      "(the same as at 7 s)", count(e55, LT, (52, 98, 188, 126)) > 150 and P(e55, 100, 27) == BK and e57 == e70 and e59 == e70 and P(e57, 100, 27) == RULE, f"{e57 == e70} {e59 == e70}")
# only what changes is drawn: while nothing moves (the eye open, the words there, 4.72 .. 4.79 s) not one pixel is sent to the display
out = run("bidle", [(4740, "probe"), (4790, "probe")], 5000, EM0)
lp = [int(re.search(r" lcdpx (\d+)", ln).group(1)) for ln in out.splitlines() if ln.startswith("slabvals ")]
check(f"only what changes is drawn: from 4.74 s to 4.79 s, nothing moving, not one pixel is sent to the display ({lp[1] - lp[0]} px); a frame of the ripple (0.8 s) sends only the dots that changed",
      len(lp) == 2 and lp[1] == lp[0], str(lp))
out = run("brip", [(790, "probe"), (850, "probe")], 1000, EM0)
lp = [int(re.search(r" lcdpx (\d+)", ln).group(1)) for ln in out.splitlines() if ln.startswith("slabvals ")]
check(f"a frame or two of the ripple (0.79 .. 0.85 s) sends {lp[1] - lp[0]} px: the dots that changed, in runs, not the screen (57,600)", 0 < lp[1] - lp[0] < 20000, str(lp))
# GLOW: the lit dots and the eye glow, the faint ones never (the rule above, on the boot's frames: what it changes is a halo of a light ink on a dark pixel)
T_G = [803, 1403, 1598, 2503, 4303, 4953]
bg1 = shots("bgl1", T_G, 5200, EM)
bg0 = shots("bgl0", T_G, 5200, EM0)
seen, wrong = 0, []
for t in T_G:
    w, da = px(bg1(t))
    _, db = px(bg0(t))
    for k in range(w * 240):
        a, b = tuple(da[3 * k:3 * k + 3]), tuple(db[3 * k:3 * k + 3])
        if a != b:
            seen += 1
            if a not in HLB:
                wrong.append((t, k % w, k // w, a, b))
check(f"the boot with GLOW: the lit dots (the ripple, the eye's outline and iris), the thin eye, the words and the line have a halo ({seen} pixels in these {len(T_G)} frames), every one a halo of a light ink on a dark "
      "pixel; the faint dots (dark) have none", not wrong and seen > 400, str(wrong[:4]))

# ---- the shared pages in MINIMAL's parameters: dark tiles, the selected one a lighter dark with a 2 px amber underline (not BRUTAL's black with a red frame); light ink; the live meter in MINIMAL's colours
SEL = c888(*MN["SEL"])
sk = film("srcm", [(6000, f"page {PGN.index('source')}")], 7000, EM0)(6500)
check("SOURCE: the seven sources as dark tiles, the track's (VA) the selected one: MINIMAL's lighter dark (#2c2f33) with a 2 px amber underline (rows 81 .. 82), its name amber; the others dark (#222528), "
      "their names light", P(sk, 10, 50) == SEL and P(sk, 10, 81) == AMB and P(sk, 10, 82) == AMB and P(sk, 10, 83) == BK and P(sk, 70, 50) == DK and P(sk, 70, 81) == DK and
      count(sk, AMB, (3, 32, 60, 80)) > 30 and count(sk, LT, (62, 32, 120, 80)) > 30, f"{P(sk, 10, 50)} {P(sk, 10, 81)} {P(sk, 70, 50)}")
lk = film("levm", [(6000, f"page {PGN.index('levels')}"), (6300, "keys 1")], 8000, EM0)
l1, l2 = lk(6200), lk(7500)
MET = {LT, AMB, c888(*rgbs(msrc[msrc.index("UI_PAL_MINIMAL"):msrc.index("static uint16_t mn_mix")])[7])}   # (the live meter: light, amber, red)
check("LEVELS: a column a track on a dark tile, the LEVEL set a light bar of 8 segments and the sound now (T1 playing) the live meter beside it in MINIMAL's colours (light, amber, red), dark when silent",
      count(l1, LT, (36, 82, 56, 182)) == 0 and count(l2, MET, (36, 82, 56, 182)) > 30 and count(l1, LT, (8, 82, 28, 182)) > 80, f"{count(l2, MET, (36, 82, 56, 182))} {count(l1, LT, (8, 82, 28, 182))}")

# ---- the curve pages in MINIMAL's parameters: thin lines (2 px; 1 px for the grey ones), thin stems, a hollow reel, a small dot, the cutoff a 1 px line
cv = shots("curvem", [6400 + 450, 7100 + 450, 7800 + 450, 8500 + 450], 9300, EM0,
           [(6400, f"page {PGN.index('delay')}"), (7100, f"page {PGN.index('master')}"), (7800, f"page {PGN.index('tape')}"), (8500, f"page {PGN.index('lfo')}")])
dl, ma, tp, lf = (cv(t + 450) for t in (6400, 7100, 7800, 8500))
w_, dd = px(dl)
row = [tuple(dd[3 * (172 * w_ + x):3 * (172 * w_ + x) + 3]) != BK for x in range(3, 237)]
runs = [len(list(g)) for k, g in __import__("itertools").groupby(row) if k]
check(f"DELAY: the echoes are thin stems (2 px wide, a small cap on each, the first echo's amber): {len(runs)} stems at 4 .. 7, each 2 px", 3 <= len(runs) <= 7 and all(r == 2 for r in runs), str(runs))
check("MASTER: the cutoff is a 1 px amber line (the filter open: at the right), the curve a 2 px light line", count(ma, AMB, (3, 121, 237, 122)) == 1 and sum(1 for y in range(60, 190) if P(ma, 100, y) == LT) == 2, f"{count(ma, AMB, (3, 121, 237, 122))}")
check("TAPE: the reels are hollow (a 2 px frame in the mid grey, a small light hub: black inside), no posts", P(tp, 3 + 35 + 10, 32 + 20 + 30) == BK and P(tp, 3 + 35, 32 + 20 + 30) == MD and P(tp, 3 + 35 + 1, 32 + 20 + 30) == MD and
      P(tp, 3 + 62 + 2, 32 + 47 + 2) == LT and P(tp, 3 + 65 + 1, 32 + 85) == BK, f"{P(tp, 48, 82)} {P(tp, 38, 82)}")
check("LFO: the dot is a 6 x 6 amber square (36 px) on the curve", count(lf, AMB, (3, 34, 237, 192)) == 36, str(count(lf, AMB, (3, 34, 237, 192))))

# ---- the theme switch: the whole panel cleared and drawn again, no pixel of the theme left, on any page, between all three themes
EYEC = set(c888(*c) for c in rgbs(ui_src[ui_src.index("UI_PAL_EYE = "):ui_src.index("static const ui_pal_t *ui_pal")]))
BRUC = set(c888(*c) for c in rgbs(bsrc[bsrc.index("UI_PAL_BRUTAL = "):bsrc.index("/* ---- the font")])) | {c888(*c) for c in (
    tuple(int(x, 16) for x in m) for m in re.findall(r"#define BR_\w+ RGB\(0x(..), 0x(..), 0x(..)\)", bsrc))}
BRUC |= {c888(0xe0, 0x70, 0x5f)}
MINC = set(c888(*c) for c in MP) | {c888(*MN[n]) for n in MN} | HL


def ramp(a, b):
    return {c888(*(a[i] + (b[i] - a[i]) * v // 127 for i in range(3))) for v in range(128)}


EYEC |= ramp((0x5e, 0x5b, 0x54), (0xe8, 0xe4, 0xd8))
BRUC |= ramp((0x6e, 0x6b, 0x66), (0xf2, 0xef, 0xe8))
T = {0: EYEC, 1: BRUC, 2: MINC}
CHAIN = [1, 2, 0, 2, 1, 0]                                       # the themes the page is in after each "theme N": from EYE
ev, ts = [], []
for n in range(N):
    t = BOOT + 3600 * n
    ev.append((t, f"page {n}"))
    ev += [(t + 400 + 500 * i, f"theme {b}") for i, b in enumerate(CHAIN)]
    ts += [t + 400 + 500 * i + 350 for i in range(len(CHAIN))]
sf = shots("switch", ts, BOOT + 3600 * N + 300, {"GRIMM_GLOW": "1"}, ev)
prev = [0] + CHAIN[:-1]
leftovers = []
for n in range(N):
    t = BOOT + 3600 * n
    for i, (a, b) in enumerate(zip(prev, CHAIN)):
        w, d = px(sf(t + 400 + 500 * i + 350))
        only = T[a] - T[b]
        got = sum(1 for j in range(w * 240) if tuple(d[3 * j:3 * j + 3]) in only)
        if got:
            leftovers.append((PGN[n], a, b, got))
check(f"THEME switched with each of the {N} pages shown, EYE > BRUTAL > MINIMAL > EYE > MINIMAL > BRUTAL > EYE (every ordered pair of the three): the whole panel is cleared and drawn again: not one "
      "pixel of the theme left is on it (checked against the colours only it has, GLOW's halos too)", not [x for x in leftovers if x[0] != "track"], f"(page, from, to, pixels): {leftovers[:6]}")

BRU_ONLY = BRUC - MINC - EYEC
bru_left = [(PGN[n], count(pf(BOOT + 500 * n + 400), BRU_ONLY)) for n in range(N)]
check("not a pixel of BRUTAL's own colours (the concretes, the black and the red) on any of the pages in MINIMAL", not [b for b in bru_left if b[1]], str([b for b in bru_left if b[1]][:6]))

sys.exit(1 if bad else 0)
