#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""Host screenshots of BRUTAL: every page at 2x in build/host/theme/brutal_<page>.png (the pictures that depend on the track's source: once for each source,
brutal_sound_va.png, brutal_sound_fm.png ..., FM ALGO / FM OP for FM, LINE 2 / PHASE ENV for PHASE), and one contact sheet of all of them, brutal_contact.png.
  python3 tests/brutal_shots.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/theme")
OUT.mkdir(parents=True, exist_ok=True)
BOOT = 6000
SRC = ["va", "fm", "sample", "voice", "wheel", "phys", "phase"]
src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
PG = [n.lower() for n in re.findall(r"PG_(\w+)", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S).group(1))]
ENVB = {}


def make_settings():
    sys3 = [(6100, "down 8"), (6900, "up 8"), (7000, "btn 8"), (7100, "btn 8"), (7300, "enc 4 1")]
    p = OUT / "bs_mk.txt"
    p.write_text("".join(f"{t} {s}\n" for t, s in sys3) + "11000 end\n")
    subprocess.run([EXE, str(OUT / "bs_mk"), str(p)], capture_output=True, check=True, env=dict(os.environ, GRIMM_SETTINGS_OUT=str(OUT / "bs_set.bin")))
    return {"GRIMM_SETTINGS_IN": str(OUT / "bs_set.bin")}


def run(name, steps, env):
    """steps: [(page number, label)]; the page shown for 0.5 s, a shot after it; -> {label: PIL image}"""
    ev = [(BOOT + 100, "page 1"), (BOOT + 200, "enc 2 %d" % steps[0])] if False else []
    return None


def shots(env):
    imgs = {}
    # engine-independent pages: a run with the source VA; the source-dependent ones once for each source
    def one(name, eng, pages):
        ev, t = [], BOOT
        if eng:
            ev += [(t, "page 1"), (t + 100, f"enc 2 {eng}")]
            t += 400
        shot_t = {}
        for n, label in pages:
            ev += [(t, f"page {n}")]
            shot_t[label] = t + 450
            ev += [(t + 450, "shot")]
            t += 700
        p = OUT / f"{name}.txt"
        p.write_text("".join(f"{a} {b}\n" for a, b in sorted(ev, key=lambda e: e[0])) + f"{t + 200} end\n")
        for f in OUT.glob(f"{name}-*.ppm"):
            f.unlink()
        subprocess.run([EXE, str(OUT / name), str(p)], capture_output=True, check=True, env=dict(os.environ, **env))
        for label, ms in shot_t.items():
            imgs[label] = Image.open(OUT / f"{name}-{ms}.ppm")
    base = [(i, n) for i, n in enumerate(PG) if n not in ("sound", "track", "fmalgo", "fmop", "pdline", "pdenv")]
    one("bs_base", 0, base)
    for e, nm in enumerate(SRC):
        extra = []
        if nm == "fm":
            extra = [(PG.index("fmalgo"), "fmalgo"), (PG.index("fmop"), "fmop")]
        if nm == "phase":
            extra = [(PG.index("pdline"), "pdline"), (PG.index("pdenv"), "pdenv")]
        one(f"bs_{nm}", e, [(PG.index("sound"), f"sound_{nm}")] + extra)
    return imgs


env = make_settings()
imgs = shots(env)
order = [n for n in PG if n in imgs] + [k for k in imgs if k not in PG]
for k, im in imgs.items():
    im.resize((480, 480), Image.NEAREST).save(OUT / f"brutal_{k}.png")
cols = 6
rows = (len(order) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 244, rows * 252), (60, 60, 60))
dr = ImageDraw.Draw(sheet)
for i, k in enumerate(order):
    x, y = (i % cols) * 244 + 2, (i // cols) * 252 + 12
    sheet.paste(imgs[k], (x, y))
    dr.text((x + 2, y - 11), k, fill=(230, 230, 230))
sheet.save(OUT / "brutal_contact.png")
print(f"{len(imgs)} pages -> {OUT}/brutal_<page>.png, brutal_contact.png")
