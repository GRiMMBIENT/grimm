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
HL = ({halo(*c) for c in MP[:6]} | {halo(*MN[n]) for n in MN}) - {BK, DK, MD, LT, WH, AMB, RULE, TRACK, DIM, FILL}
gh = film("glowhome", [], 7000, EM)(6500)
nh = film("noglowhome", [], 7000, EM0)(6500)
check(f"GLOW: with it the text has a halo (the colour a third as bright, 1 px round every pixel of it: {count(gh, HL)} pixels of it on HOME), the rule too; without it none ({count(nh, HL)})",
      count(gh, HL) > 300 and count(nh, HL) == 0 and P(gh, 100, 26) == halo(*MN["RULE"]) and P(gh, 100, 28) == halo(*MN["RULE"]) and P(nh, 100, 26) == BK, f"{count(gh, HL)} {count(nh, HL)}")
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

# ---- every page in MINIMAL's frame
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
check(f"none of EYE's pixel font is drawn on any of the {N} pages (the header, the pictures, the knob row: Terminus); the pages' pictures' texts go through it ({sum(1 for p in pt if p[2])} pages)",
      not any(p[1] for p in pt) and sum(1 for p in pt if p[2]) >= 20, str([p for p in pt if p[1]]))
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
        if 0 <= ir < 4 and 0 <= ic < 6 and IRIS[tiny][ir][ic] in "aw":
            for i in range(3):
                for j in range(3):
                    out[(x0 + c * 5 + 1 + i, y0 + r * 5 + 1 + j)] = AMB if IRIS[tiny][ir][ic] == "a" else WH
    return out


ME = list(range(2, 64))                                          # (the boot's frames, 55 ms apart: 0.1 s .. 3.5 s: shut, a flutter, startled wide open, looking round, the blink)
es = shots("bye", [55 * m + 3 for m in ME], 4000, {"GRIMM_BOOT_PLAIN": "1"})   # (EYE's intro without its stars and bats: the eye alone)
bs = shots("byb", [55 * m + 3 for m in ME], 6000, EM0)
bad_px, no_pose, poses, outside = [], [], set(), 0
for m in ME:
    ie, ib = es(55 * m + 3), bs(55 * m + 3)
    pose = match_eye(ie)
    if pose is None:
        no_pose.append(m)
        continue
    poses.add(pose)
    w, d = px(ib)
    got = {(x, y): tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) for y in range(38, 96) for x in range(70, 172) if tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) != BK}
    if got != mini_eye(pose):
        bad_px.append(m)
        if len(bad_px) == 1:
            ex = mini_eye(pose)
            print("   first difference (frame", m, pose, "): drawn not expected", sorted(set(got) - set(ex))[:8], "expected not drawn", sorted(set(ex) - set(got))[:8], len(got), len(ex))
    outside += sum(1 for y in range(240) for x in range(240) if not (70 <= x < 172 and 38 <= y < 96) and tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) != BK)
check(f"the boot's eye is EYE's, frame for frame: {len(ME)} frames (0.1 .. 3.5 s: shut, a flutter, startled wide open, looking round, the blink; {len(poses)} different poses, each found in EYE's frame "
      "cell by cell): MINIMAL's frame is exactly the 1 px outline round that shape and the iris (amber, white, black) in the same cells, nothing else",
      not no_pose and not bad_px and len(poses) >= 8, f"EYE's pose unknown at {no_pose[:6]}; differs at {bad_px[:8]}; {len(poses)} poses")
check("no bats, no stars, no drips: nothing on the screen but the eye until GR!MM comes (3.6 s)", outside == 0, str(outside))
BT = [3000, 3700, 3900, 4100, 4300, 4500, 4700, 5100, 5500]
bt = shots("bt", [t + 3 for t in BT], 6300, EM0)
f30, f37, f39, f41, f43, f45, f47, f51, f55 = (bt(t + 3) for t in BT)
def mix565(c, a):                               # mn_mix(c, a, 8) as a pixel
    return ((c[0] >> 3) * a // 8 * 255 // 31, (c[1] >> 2) * a // 8 * 255 // 63, (c[2] >> 3) * a // 8 * 255 // 31)


wm = lambda im: count(im, {mix565(MP[3], a) for a in range(1, 9)}, (52, 98, 188, 126))
lin = lambda im: count(im, FILL, (50, 134, 190, 135))
ver = lambda im: count(im, {mix565(MN["DIM"], a) for a in range(1, 9)}, (0, 144, 240, 160))
check("GR!MM fades in (3.6 .. 4.2 s: none at 3.0 s, dim at 3.9 s, full light by 4.3 s), in the middle of the screen, Terminus Bold 2x",
      wm(f30) == 0 and 0 < wm(f39) and count(f39, LT, (52, 98, 188, 126)) == 0 and count(f43, LT, (52, 98, 188, 126)) > 150,
      f"{wm(f30)} {wm(f39)} {count(f43, LT, (52, 98, 188, 126))}")
xs = [x for x in range(240) if P(f51, x, 134) == FILL]
check("a thin line draws out from the middle under it (4.2 .. 4.8 s): none at 4.1 s, 1 px, growing, as wide as the word (128 px) when done (4.8 s); the version line fades in after it (4.4 .. 5.0 s) and is there at 5.1 s",
      lin(f41) == 0 and 0 < lin(f45) < lin(f47) < 128 and len(xs) == 128 and xs[0] == 56 and xs[-1] == 183 and ver(f51) > 40 and ver(f43) == 0, f"{lin(f41)} {lin(f45)} {lin(f47)} {len(xs)} {ver(f51)} {ver(f43)}")
hh = shots("bh", [5500 + 3, 5700 + 3, 5900 + 3, 7000 + 3], 7500, EM0)
b55, b57, b59, b70 = hh(5503), hh(5703), hh(5903), hh(7003)
check("the boot is as long as the others' (5.6 s): at 5.5 s it is still on (the eye, GR!MM, the line); then a clean cut to HOME: at 5.7 s the screen is HOME and nothing of the boot is left on it (the same as 7 s)",
      count(b55, AMB, (70, 38, 172, 96)) > 20 and P(b55, 100, 27) == BK and b57 == b70 and b59 == b70 and P(b57, 100, 27) == RULE, f"{b57 == b70} {b59 == b70}")
bgl = shots("bgl", [3000 + 3, 5100 + 3], 5500, dict(EM, GRIMM_BOOT_PLAIN="1"))
check("with GLOW the boot has its halo too (the eye's outline, the word, the line), as the text and lines do; without it none", count(bgl(3003), HL) > 80 and count(bt(3003), HL) == 0 and count(bgl(5103), HL) > 200,
      f"{count(bgl(3003), HL)} {count(bgl(5103), HL)}")

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

sys.exit(1 if bad else 0)
