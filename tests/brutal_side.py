#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""MIXER 1 and SEQ GRID, EYE and BRUTAL side by side at 2x in build/host/theme/side_<page>.png (the same state in both: the host's GRIMM_THEME)
  python3 tests/brutal_side.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/theme")
OUT.mkdir(parents=True, exist_ok=True)
src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
PG = [n.lower() for n in re.findall(r"PG_(\w+)", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S).group(1))]
STATES = {
    "mix1": [(6000, f"page {PG.index('mix1')}"), (6100, "enc 0 1"), (6200, "enc 2 60"), (6300, "keys 1")],
    "mix1_fx0": [(6000, f"page {PG.index('home')}"), (6100, "enc 5 -127"), (6300, f"page {PG.index('mix1')}"), (6400, "enc 0 2")],
    "grid": [(6000, f"page {PG.index('grid')}"), (6100, "enc 2 5"), (6200, "enc 3 -30"), (6300, "enc 4 1"), (6400, "enc 0 1"), (6500, "enc 2 7")],
}
for name, ev in STATES.items():
    t = ev[-1][0] + 400
    ims = []
    for th in ("0", "1"):
        p = OUT / f"side_{name}_{th}.txt"
        p.write_text("".join(f"{a} {b}\n" for a, b in ev) + f"{t} shot\n{t + 300} end\n")
        for f in OUT.glob(f"side_{name}_{th}-*.ppm"):
            f.unlink()
        subprocess.run([EXE, str(OUT / f"side_{name}_{th}"), str(p)], capture_output=True, check=True, env=dict(os.environ, GRIMM_THEME=th))
        ims.append(Image.open(OUT / f"side_{name}_{th}-{t}.ppm").convert("RGB").resize((480, 480), Image.NEAREST))
    sheet = Image.new("RGB", (968, 480), (60, 60, 60))
    sheet.paste(ims[0], (0, 0))
    sheet.paste(ims[1], (488, 0))
    sheet.save(OUT / f"side_{name}.png")
    print(f"EYE | BRUTAL -> {OUT}/side_{name}.png")
