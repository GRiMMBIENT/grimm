#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""Host screenshots of MINIMAL at 2x in build/host/theme/: minimal_home_glow_on.png, minimal_home_glow_off.png, minimal_sys3_glow_on.png, minimal_sys3_glow_off.png, minimal_boot_<ms>.png (and a contact sheet of
the boot, minimal_boot.png), and every page (minimal_<page>.png, minimal_contact.png).
  python3 tests/minimal_shots.py [build/host/grimm]"""
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
src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
PG = [n.lower() for n in re.findall(r"PG_(\w+)", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S).group(1))]


def run(name, ev, shots, glow, end=None, extra_env=None):
    p = OUT / f"{name}.txt"
    t_end = (end or max(shots) + 300)
    p.write_text("".join(f"{a} {b}\n" for a, b in sorted(ev + [(t, "shot") for t in shots])) + f"{t_end} end\n")
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    env = dict(os.environ, GRIMM_THEME="2", GRIMM_GLOW="1" if glow else "0", **(extra_env or {}))
    subprocess.run([EXE, str(OUT / name), str(p)], capture_output=True, check=True, env=env)
    return {t: Image.open(OUT / f"{name}-{t}.ppm").convert("RGB") for t in shots}


def big(im):
    return im.resize((480, 480), Image.NEAREST)


# HOME, glow on and off
for glow, nm in ((1, "minimal_home_glow_on"), (0, "minimal_home_glow_off")):
    im = run(f"mh{glow}", [(BOOT, "keys 1"), (BOOT + 400, "keys 0")], [BOOT + 300], glow)[BOOT + 300]
    big(im).save(OUT / f"{nm}.png")
# SYSTEM 3 (HOME held x3: the host's page command is simpler)
for glow in (1, 0):
    im = run(f"ms{glow}", [(BOOT, f"page {PG.index('sys3')}")], [BOOT + 400], glow)[BOOT + 400]
    big(im).save(OUT / f"minimal_sys3_glow_{'on' if glow else 'off'}.png")
# the boot: frames at 0.3, 1.0, 1.6, 2.2, 3.0, 3.8, 4.3, 4.7, 5.2 s
ts = [300, 1000, 1600, 2200, 3000, 3800, 4300, 4700, 5200]
for glow in (1, 0):
    fr = run(f"mb{glow}", [], [t + 3 for t in ts], glow, end=5900)
    for t in ts:
        big(fr[t + 3]).save(OUT / f"minimal_boot{'_glow' if glow else ''}_{t}.png")
    sheet = Image.new("RGB", (3 * 244, 3 * 244), (60, 60, 60))
    for i, t in enumerate(ts):
        sheet.paste(fr[t + 3], ((i % 3) * 244 + 2, (i // 3) * 244 + 2))
    sheet.save(OUT / f"minimal_boot{'_glow' if glow else ''}.png")
# every page
ev, shots, t = [], {}, BOOT
base = [(i, n) for i, n in enumerate(PG) if n not in ("track",)]
for i, n in base:
    ev.append((t, f"page {i}"))
    shots[n] = t + 450
    t += 700
r = run("mall", ev, list(shots.values()), 1, end=t + 400)
imgs = {n: r[ts_] for n, ts_ in shots.items()}
for n, im in imgs.items():
    big(im).save(OUT / f"minimal_{n}.png")
cols = 6
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 244, rows * 252), (60, 60, 60))
dr = ImageDraw.Draw(sheet)
for i, (n, im) in enumerate(imgs.items()):
    x, y = (i % cols) * 244 + 2, (i // cols) * 252 + 12
    sheet.paste(im, (x, y))
    dr.text((x + 2, y - 11), n, fill=(230, 230, 230))
sheet.save(OUT / "minimal_contact.png")
print(f"MINIMAL: {len(imgs)} pages, HOME, SYSTEM 3, the boot -> {OUT}/minimal_*.png")
