#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""BRUTAL is pixel-identical through the refactors that share its picture code with MINIMAL (tests/run_host.sh): every page (and each source's SOUND page, FM ALGO's 32 algorithms,
PHASE's LINE 2 and PHASE ENV, a playing state, the boot's frames) with THEME BRUTAL, SHA-256 each, compared with tests/data/brutal_screens.sha256 (made with the build just before the
refactor: python3 tests/brutal_hash_test.py BIN --make).
  python3 tests/brutal_hash_test.py [build/host/grimm] [--make]"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
EXE = ARGS[0] if ARGS else "build/host/grimm"
MAKE = "--make" in sys.argv
OUT = Path("build/host/brutalhash")
OUT.mkdir(parents=True, exist_ok=True)
GOLD = Path("tests/data/brutal_screens.sha256")
BOOT = 6000
src = Path("firmware/src/grimm.c").read_text(encoding="utf-8")
PG = [n.lower() for n in re.findall(r"PG_(\w+)", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", src, re.S).group(1))]


def digest(label, ppm):
    """SHA-256 of a frame; SYSTEM 2's CPU figures move by themselves: its rows 116 .. 189 are left out"""
    if label.startswith("page_sys2"):
        m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", ppm)
        w, o = int(m.group(1)), m.end()
        ppm = bytearray(ppm)
        ppm[o + 3 * w * 116:o + 3 * w * 190] = bytes(3 * w * 74)
        ppm = bytes(ppm)
    return hashlib.sha256(ppm).hexdigest()


def run(name, ev, labels):
    """-> {label: sha256 of the frame shot at its time}: events [(ms, command)], labels {label: ms of its shot}"""
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    path = OUT / f"{name}.txt"
    allev = ev + [(t, "shot") for t in labels.values()]
    end = max(t for t, _ in allev) + 300
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(allev, key=lambda e: e[0])) + f"{end} end\n")
    subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, check=True, env=dict(os.environ, GRIMM_THEME="1"))
    return {k: digest(k, (OUT / f"{name}-{t}.ppm").read_bytes()) for k, t in labels.items()}


got = {}
# every page, the source VA (a page's own state: its knobs as they come up)
ev, lab, t = [], {}, BOOT
for i, n in enumerate(PG):
    ev.append((t, f"page {i}"))
    lab[f"page_{n}"] = t + 450
    t += 700
got.update(run("pages", ev, lab))
# SOUND for each source, and PHASE's LINE 2 and PHASE ENV, FM's ALGO and OP
ev, lab, t = [], {}, BOOT
for e in range(7):
    ev += [(t, f"page {PG.index('source')}"), (t + 100, "enc 2 1" if e else "enc 2 -9"), (t + 200, f"page {PG.index('sound')}")]
    lab[f"sound_src{e}"] = t + 650
    t += 900
    if e == 1:
        for n in ("fmalgo", "fmop"):
            ev.append((t, f"page {PG.index(n)}"))
            lab[f"{n}_src1"] = t + 450
            t += 700
    if e == 6:
        for n in ("pdline", "pdenv"):
            ev.append((t, f"page {PG.index(n)}"))
            lab[f"{n}_src6"] = t + 450
            t += 700
got.update(run("sources", ev, lab))
# FM ALGO: the 32 algorithms
ev = [(BOOT, f"page {PG.index('source')}"), (BOOT + 50, "enc 2 1"), (BOOT + 100, f"page {PG.index('fmalgo')}"), (BOOT + 150, "enc 2 -40")]
lab = {}
for k in range(32):
    t = BOOT + 600 + k * 400
    lab[f"algo_{k + 1:02d}"] = t
    if k:
        ev.append((t - 300, "enc 2 1"))
got.update(run("algos", ev, lab))
# knobs turned and notes held: states of the pages that move
states = {
    "mix1_sel": [(BOOT, f"page {PG.index('mix1')}"), (BOOT + 100, "enc 0 1"), (BOOT + 200, "enc 2 60"), (BOOT + 300, "keys 1")],
    "levels_keys": [(BOOT, f"page {PG.index('levels')}"), (BOOT + 100, "keys 1")],
    "home_keys": [(BOOT, "keys 1")],
    "lfo_shape": [(BOOT, f"page {PG.index('lfo')}"), (BOOT + 100, "enc 5 3"), (BOOT + 200, "enc 2 25")],
    "master_hp": [(BOOT, f"page {PG.index('master')}"), (BOOT + 100, "enc 2 -50"), (BOOT + 200, "enc 3 80"), (BOOT + 300, "enc 4 1")],
    "drive": [(BOOT, f"page {PG.index('drive')}"), (BOOT + 100, "enc 2 70"), (BOOT + 200, "enc 3 2")],
    "env": [(BOOT, f"page {PG.index('env')}"), (BOOT + 100, "enc 2 -60"), (BOOT + 200, "enc 4 -60"), (BOOT + 300, "enc 3 -30"), (BOOT + 400, "enc 5 -50")],
    "grain": [(BOOT, f"page {PG.index('grain')}"), (BOOT + 100, "enc 3 60"), (BOOT + 200, "enc 4 90"), (BOOT + 300, "enc 2 30")],
    "delay": [(BOOT, f"page {PG.index('delay')}"), (BOOT + 100, "enc 3 60"), (BOOT + 200, "enc 2 -50")],
    "space": [(BOOT, f"page {PG.index('space')}"), (BOOT + 100, "enc 2 -90")],
    "tape": [(BOOT, f"page {PG.index('tape')}"), (BOOT + 100, "enc 2 100")],
    "sounds": [(BOOT, f"page {PG.index('sounds')}"), (BOOT + 100, "enc 2 2")],
    "song": [(BOOT, f"page {PG.index('song')}"), (BOOT + 100, "enc 5 1")],
    "pattern": [(BOOT, f"page {PG.index('pattern')}"), (BOOT + 100, "enc 2 -4")],
    "perform": [(BOOT, f"page {PG.index('perform')}"), (BOOT + 100, "down 2"), (BOOT + 150, "keys 1"), (BOOT + 200, "keys 0"), (BOOT + 250, "up 2")],
    "scale": [(BOOT, f"page {PG.index('scale')}"), (BOOT + 100, "enc 3 2")],
    "grid": [(BOOT, f"page {PG.index('grid')}"), (BOOT + 100, "enc 2 5"), (BOOT + 200, "enc 3 -30"), (BOOT + 300, "enc 4 1"), (BOOT + 400, "enc 0 1"), (BOOT + 500, "enc 2 7")],
}
for k, ev in states.items():
    got.update(run("st_" + k, ev, {f"state_{k}": ev[-1][0] + 500}))
# the boot's frames (BRUTAL's door)
lab = {f"boot_{t}": t + 3 for t in range(0, 5900, 110)}
got.update(run("boot", [], lab))

if MAKE:
    GOLD.write_text("".join(f"{k} {v}\n" for k, v in sorted(got.items())), encoding="utf-8")
    print(f"{len(got)} frames -> {GOLD}")
    sys.exit(0)
want = dict(line.split() for line in GOLD.read_text(encoding="utf-8").splitlines())
diff = sorted(k for k in set(got) | set(want) if got.get(k) != want.get(k))
print(f"  {'ok' if not diff else 'FAIL':4s}  brutal hash: BRUTAL is pixel-identical to the build before the refactor: {len(got)} frames (every page, each source's SOUND page, FM ALGO's 32 algorithms, "
      f"PHASE's LINE 2 and PHASE ENV, {len(states)} states that move, the boot's frames), SHA-256 each" + (f" ({diff[:6]})" if diff else ""))
sys.exit(1 if diff else 0)
