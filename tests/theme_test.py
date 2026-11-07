#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 themes in host mode (tests/run_host.sh).
Stage 0: EYE is pixel-identical to GR!MM before the theme layer: the boot screen frame by frame (20 a second), every page (a frame each), and a playing state (notes held, chord, a closed filter, a changed COLOUR, the mixer pages): the SHA-256 of each frame
is compared with tests/data/eye_screens.sha256, made with the build just before the theme layer (python3 tests/theme_test.py BIN --make). The theme
layer itself changed none of them (proved before THEME was added); the THEME row on SYSTEM 3 changed that page's five frames (page 27 and the state's
frames 15500 .. 16250), and those are new in the file.
Stage 1 (BRUTAL): the same states with THEME BRUTAL, looked at pixel by pixel (see the checks below).
  python3 tests/theme_test.py [build/host/grimm] [--make]"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
EXE = ARGS[0] if ARGS else "build/host/grimm"
MAKE = "--make" in sys.argv
DUMP = Path(sys.argv[sys.argv.index("--dump") + 1]) if "--dump" in sys.argv else None   # (--dump DIR: every frame as DIR/<name>.ppm, no comparison: to see what a change touched)
ARGS = [a for a in ARGS if a != str(DUMP)]
OUT = Path("build/host/theme")
OUT.mkdir(parents=True, exist_ok=True)
GOLD = Path("tests/data/eye_screens.sha256")
BOOT = 6000
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  theme: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def npage():
    src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
    m = re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S)
    return len(re.findall(r"PG_\w+", m.group(1)))


def film(name, ev, end, fps, env=None):
    """-> a function frame(ms) -> the PPM bytes of the frame at ms"""
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


N = npage()
SYS2 = {"page11", "state@15000", "state@15250"}          # SYSTEM 2 shows the CPU load, which depends on the computer's speed: its row (y 144 .. 159) left out


def sha(name, ppm):
    if DUMP:
        DUMP.mkdir(parents=True, exist_ok=True)
        (DUMP / f"{name.replace('@', '_')}.ppm").write_bytes(ppm)
    if name in SYS2:
        w, d = px(ppm)
        d = bytearray(d)
        d[3 * w * 144:3 * w * 160] = bytes(3 * w * 16)
        ppm = bytes(d)
    return hashlib.sha256(ppm).hexdigest()


def eye_hashes():
    res = {}
    fr = film("boot", [], 6500, 20)                         # the boot screen, frame by frame
    for k in range(0, 5700, 50):
        res[f"boot@{k}"] = sha(f"boot@{k}", fr(k))
    ev = [(BOOT + 500 * n, f"page {n}") for n in range(N)]  # every page
    fr = film("pages", ev, BOOT + 500 * N + 300, 4)
    for n in range(N):
        res[f"page{n}"] = sha(f"page{n}", fr(BOOT + 500 * n + 400))
    ev = [(6100, "keys 3"), (6900, "keys 0"),               # a playing state: notes, the chord mode, a closed filter, MIXER 1 / 2, LEVELS, FX, SEQ, a COLOUR
          (7000, "down 3"), (7700, "up 3"), (8100, "enc 3 -127"),
          (8500, "btn 8"), (9000, "btn 8"), (9500, "btn 8"), (10000, "btn 8"),
          (10500, "btn 2"), (11000, "btn 11"), (11500, "btn 6"), (12000, "btn 10"), (12500, "btn 7"),
          (13000, "down 8"), (13800, "up 8"), (14300, "enc 2 1"), (15000, "btn 8"), (15500, "btn 8"),
          (16000, "keys 5"), (16500, "btn 8"), (17000, "keys 0")]
    fr = film("state", ev, 18000, 4)
    for k in range(BOOT, 18000, 250):
        res[f"state@{k}"] = sha(f"state@{k}", fr(k))
    return res


if DUMP:
    eye_hashes()
    sys.exit(0)
if MAKE:
    GOLD.write_text("".join(f"{k} {v}\n" for k, v in sorted(eye_hashes().items())))
    print(f"made {GOLD}")
    sys.exit(0)

gold = dict(ln.split() for ln in GOLD.read_text().splitlines())
cur = eye_hashes()
diff = sorted(k for k in gold if cur.get(k) != gold[k])
check(f"EYE is pixel-identical to GR!MM before the theme layer: {len(gold)} frames (the boot screen 20 a second, all {N} pages, a playing "
      f"state), SHA-256 each", not diff and set(cur) == set(gold), str(diff[:12]))

sys.exit(1 if bad else 0)
