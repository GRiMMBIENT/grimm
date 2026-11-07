#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""What a change touched, pixel by pixel: the same scenes of EYE's golden test (tests/theme_test.py: the boot, every page, a playing state) and of BRUTAL's (tests/brutal_hash_test.py) with an
older host binary and the newer one, every frame compared: which frames differ and the box of the differing pixels in each (made for "the intro's version line is V3.0", V3.0 THEMES)
  python3 tests/boot_line_diff.py OLD_BIN NEW_BIN"""
import re
import subprocess
import sys
from pathlib import Path

old, new = sys.argv[1], sys.argv[2]
OUT = Path("build/host/linediff")


def frames(exe, which, d):
    subprocess.run([sys.executable, f"tests/{which}.py", exe, "--dump", str(d)], capture_output=True, check=True)
    return {p.stem: p.read_bytes() for p in d.glob("*.ppm")}


def px(ppm):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", ppm)
    return int(m.group(1)), ppm[m.end():]


MASK = {"EYE": ({"page11", "state_15000", "state_15250"}, 144, 160), "BRUTAL": ({"page_sys2"}, 116, 190)}     # (SYSTEM 2's CPU figures move by themselves: their rows are left out, as in the hash tests)


def masked(ppm, k, label):
    names, y0, y1 = MASK[label]
    if k not in names:
        return ppm
    w, d = px(ppm)
    d = bytearray(d)
    d[3 * w * y0:3 * w * y1] = bytes(3 * w * (y1 - y0))
    return ppm[:len(ppm) - len(d)] + bytes(d)


def box(a, b):
    w, da = px(a)
    _, db = px(b)
    xs, ys = [], []
    for i in range(0, len(da), 3):
        if da[i:i + 3] != db[i:i + 3]:
            xs.append((i // 3) % w)
            ys.append((i // 3) // w)
    return (min(xs), min(ys), max(xs), max(ys), len(xs)) if xs else None


bad = 0
for which, label in (("theme_test", "EYE"), ("brutal_hash_test", "BRUTAL")):
    fo, fn = frames(old, which, OUT / f"{which}_old"), frames(new, which, OUT / f"{which}_new")
    assert set(fo) == set(fn), (which, set(fo) ^ set(fn))
    diff = {k: box(masked(fo[k], k, label), masked(fn[k], k, label)) for k in sorted(fo) if masked(fo[k], k, label) != masked(fn[k], k, label)}
    boxes = {}
    for k, b in diff.items():
        kind = "boot" if k.startswith("boot") else k.split("_")[0] if k.startswith("page") else "state"
        boxes.setdefault((kind, b[:4] if kind != "boot" else None), []).append(k)
    print(f"{label}: {len(fo)} frames, {len(diff)} differ")
    allx = [b for b in diff.values()]
    for (kind, bx), ks in sorted(boxes.items(), key=lambda e: str(e[0])):
        if kind == "boot":
            bs = [diff[k] for k in ks]
            bx = (min(b[0] for b in bs), min(b[1] for b in bs), max(b[2] for b in bs), max(b[3] for b in bs))
        print(f"   {kind}: {len(ks)} frames ({min(ks, key=lambda k: int(re.sub(r'\D', '', k) or 0))} .. {max(ks, key=lambda k: int(re.sub(r'\D', '', k) or 0))}), the pixels differing in box x {bx[0]}..{bx[2]}, y {bx[1]}..{bx[3]}")
    if diff:
        bb = (min(b[0] for b in allx), min(b[1] for b in allx), max(b[2] for b in allx), max(b[3] for b in allx))
        print(f"   all the differing pixels lie in x {bb[0]}..{bb[2]}, y {bb[1]}..{bb[3]}")
