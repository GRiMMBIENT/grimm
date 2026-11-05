#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 item 5 in host mode (tests/run_host.sh): the voices running out are shown.
  - a note no voice can be found for (6 voices held, a 7th key): the header says VOICES FULL ~1.5 s
  - a note that takes another track's voice (T1 holds all 6, T2 plays): NOTE TAKEN
  - not every note: at most one message in 1.5 s (a drop inside it says nothing new), the next one after it says again
  - SYSTEM 2's VOICES row ("VOICES 4/6"): the voices in use: held, latched and releasing ones; drawn (the screen's pixels)
  python3 tests/voices_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/voices")
OUT.mkdir(parents=True, exist_ok=True)
SEL, OCT_UP, HOME = 3, 1, 8
ALGO = 1                                        # the encoder: the track
K6, K7, K8 = 63, 127, 191                       # keys held: F3 .. A3 (6), + the 7th, + the 8th
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  voices: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def script(name, ev, end):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return path


def run(name, ev, end):
    """-> {ms: (voices in use, notes dropped, notes taken, the VOICES row's text, the header's message)}"""
    path = script(name, ev, end)
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True).stdout
    res = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            m = re.search(r" hd (\S+) vo (\d+) (\d+) (\d+) (\S+)", ln)
            res[int(ln.split()[1])] = (int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5), m.group(1))
    return res


def probes(*ts):
    return [(t, "probe") for t in ts]


# ---- VOICES FULL: six keys held, the 7th has no voice
p = run("full", [(6100, f"keys {K6}"), (6400, "probe"), (6500, f"keys {K7}"), (6800, "probe"), (6900, f"keys {K6}"), (9000, "probe"),
                 (9100, "keys 0")], 9300)
check("six keys: six voices in use, nothing said; the 7th key: dropped (1), the header says VOICES FULL, six still in use",
      p[6400][:3] == (6, 0, 0) and p[6400][4] == "-" and p[6800][:3] == (6, 1, 0) and p[6800][4] == "VOICES_FULL", str(p))
check("VOICES FULL goes after ~1.5 s (not still there at 3 s)", p[9000][4] == "-", str(p[9000]))

# ---- NOTE TAKEN: T1 holds all six, T2 plays: it takes one of T1's
p = run("taken", [(6100, f"keys {K6}"), (6400, "probe"), (6500, f"enc {ALGO} 1"), (6600, f"keys {K7}"), (6900, "probe"),
                  (9000, "probe"), (9100, "keys 0")], 9300)
check("T1 holds six; T2's key takes a voice of T1's: NOTE TAKEN (1), none dropped, six in use", p[6400][:3] == (6, 0, 0) and
      p[6900][:3] == (6, 0, 1) and p[6900][4] == "NOTE_TAKEN", str(p))
check("NOTE TAKEN goes after ~1.5 s", p[9000][4] == "-", str(p[9000]))

# ---- no spam: the 7th key struck at 6100, 6600, 7100 (inside 1.5 s of the first), 8000 (after it)
ev = [(6000, f"keys {K6}")]
for t in (6100, 6600, 7100, 8000):
    ev += [(t, f"keys {K7}"), (t + 100, f"keys {K6}")]
ev += probes(6300, 6700, 7300, 7700, 8200, 9700) + [(9800, "keys 0")]
p = run("spam", ev, 10000)
check("three drops inside 1.5 s: the message once (it is gone by 7.7 s, 1.5 s after the first, not renewed), all counted",
      p[6300][1] == 1 and p[6300][4] == "VOICES_FULL" and p[6700][1] == 2 and p[6700][4] == "VOICES_FULL" and p[7300][1] == 3 and
      p[7700][4] == "-", str({t: p[t] for t in (6300, 6700, 7300, 7700)}))
check("a drop after it says again (8.0 s), and goes again", p[8200][1] == 4 and p[8200][4] == "VOICES_FULL" and p[9700][4] == "-",
      str({t: p[t] for t in (8200, 9700)}))

# ---- the readout: held, latched, releasing
ev = [(6100, "keys 1"), (6400, "probe"),        # one held
      (6500, "keys 7"), (6800, "probe"),        # three held
      (6900, "keys 0"), (6950, "probe"),        # released: still sounding (a release)
      (12000, "probe")]                         # gone
p = run("count", ev, 12100)
check("held: VOICES 1/6, then 3/6; just released: still 3/6 (the releases sound); later 0/6",
      [p[6400][0], p[6400][3]] == [1, "VOICES_1/6"] and [p[6800][0], p[6800][3]] == [3, "VOICES_3/6"] and p[6950][0] == 3 and
      p[12000][:1] == (0,) and p[12000][3] == "VOICES_0/6", str(p))
ev = [(6000, f"down {SEL}"), (6050, f"btn {OCT_UP}"), (6150, f"up {SEL}"),   # LATCH on (SEL + OCT+)
      (6300, "keys 7"), (6400, "keys 0"), (6500, "probe"),   # three keys tapped: latched, they stay
      (12000, "probe")]
p = run("latch", ev, 12100)
check("latched notes stay in use after the keys are let go (3/6, still 3/6 much later)",
      p[6500][0] == 3 and p[12000][0] == 3 and p[12000][3] == "VOICES_3/6", str(p))

# ---- the screen: SYSTEM 2 (HOME held, then HOME), the VOICES row's pixels
ev = [(6100, f"down {HOME}"), (6800, f"up {HOME}"), (7000, f"btn {HOME}"),   # SYSTEM, then SYSTEM 2
      (8000, f"keys {K6}")]                     # six voices: the row changes
path = script("pix", ev, 9800)
subprocess.run([EXE, str(OUT / "pix_f"), str(path), "4"], capture_output=True, check=True)


def row_px(ms):                                 # the VOICES row's light pixels, its text, and the row's bytes (rows 110 .. 125)
    f = open(OUT / f"pix_f-{ms // 250:05d}.ppm", "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", f)
    w, d = int(m.group(1)), f[m.end():]
    n = 0
    for y in range(112, 123):
        for x in range(12, 100):
            r, g, b = d[3 * (y * w + x):3 * (y * w + x) + 3]
            n += r > 150 and g > 150 and b > 150
    return n, d[3 * (110 * w):3 * (126 * w)]


a, b = row_px(7750), row_px(9250)
check("SYSTEM 2 draws the VOICES row (light pixels at 0 voices and at 6), and redraws when the count changes",
      a[0] > 30 and b[0] > 30 and a[1] != b[1], f"{a[0]} {b[0]} same={a[1] == b[1]}")

sys.exit(1 if bad else 0)
