#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's GENERATE in host mode (tests/host.c, its "probe": gen, ph, srcs):
  - every note it plays is in the SCALE (C NATURAL MINOR), in each MODE, chord mode off and on
  - a track's pattern: playing; GENERATE on: its steps unheard (none of its sequenced notes on), the pattern itself
    unchanged; off: the pattern plays again, unchanged
    python3 tests/gen_test.py build/host/grimm"""
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/gentest")
OUT.mkdir(parents=True, exist_ok=True)
SRC_SEQ, SRC_GEN = 1, 3
MINOR = {0, 2, 3, 5, 7, 8, 10}                  # (C NATURAL MINOR: SC_MASK 0x5AD)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  gen: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end):
    (OUT / f"{name}.txt").write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda x: x[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(OUT / f"{name}.txt")], capture_output=True, text=True).stdout
    pr = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            ms = int(ln.split()[1])
            g = ln.split(" gen ")[1].split()
            srcs = [tuple(int(x) for x in s.split(":")) for s in ln.split(" srcs")[1].split(" /srcs")[0].split()]
            pr[ms] = dict(gen=int(g[0]), mode=int(g[1]), ph=ln.split(" ph ")[1].split()[0].split(","), srcs=srcs)
    return pr


def fx_key(t, bit):                             # FX held, a key tapped, FX let go
    return [(t, "down 2"), (t + 20, f"keys {1 << bit:#x}"), (t + 40, "keys 0"), (t + 60, "up 2")]


SCALE = [(200, "btn 3"), (250, "enc 3 1")]       # (SEL: SCALE; SCALE: NATURAL MINOR, ROOT C)
CHORD = [(300, "down 3"), (1000, "up 3")]        # (SEL held: chord mode on; its CHORD TYPE: SCALE, in key)
for chord in (0, 1):
    for mode in range(3):
        ev = SCALE + (CHORD if chord else []) + fx_key(1200, 1)
        for i in range(mode):
            ev += fx_key(1400 + 100 * i, 10)     # (D#4: the next MODE)
        ev += [(2000 + 40 * i, "probe") for i in range(500)]
        pr = run(f"scale{chord}{mode}", ev, 22200)
        notes = [n for p in pr.values() for s, t, n in p["srcs"] if s == SRC_GEN]
        out = sorted({n for n in notes if n % 12 not in MINOR})
        check(f"MODE {['DRIFT', 'PULSE', 'STORM'][mode]}{', chord mode' if chord else ''}: {len(notes)} notes seen, all in "
              "the SCALE", notes and not out and all(p["mode"] == mode for p in pr.values()), f"out of it: {out}")

# the pattern: four steps recorded on T1 (stopped, REC), PLAY; GENERATE on T1 at 4 s, off at 12 s
ev = [(200, "btn 13"), (300, "keys 0x80"), (400, "keys 0"), (500, "keys 0x800"), (600, "keys 0"), (700, "keys 0x4000"),
      (800, "keys 0"), (900, "keys 0x1000"), (1000, "keys 0"), (1100, "btn 13"), (1500, "btn 12")]
ev += fx_key(4000, 1) + fx_key(12000, 1)
ev += [(1600 + 50 * i, "probe") for i in range(400)]
pr = run("pattern", ev, 21800)
ph0 = pr[1600]["ph"]
seq_before = [ms for ms, p in pr.items() if ms < 4000 and any(s == SRC_SEQ and t == 1 for s, t, n in p["srcs"])]
during = [p for ms, p in pr.items() if 4300 <= ms < 12000]
seq_during = [ms for ms, p in pr.items() if 4300 <= ms < 12000 and any(s == SRC_SEQ and t == 1 for s, t, n in p["srcs"])]
gen_during = [ms for ms, p in pr.items() if 4300 <= ms < 12000 and any(s == SRC_GEN and t == 1 for s, t, n in p["srcs"])]
seq_after = [ms for ms, p in pr.items() if ms >= 12300 and any(s == SRC_SEQ and t == 1 for s, t, n in p["srcs"])]
gen_after = [ms for ms, p in pr.items() if ms >= 12300 and any(s == SRC_GEN for s, t, n in p["srcs"])]
check("T1's pattern plays before GENERATE (its notes on)", len(seq_before) > 5, f"{len(seq_before)}")
check("GENERATE on T1: none of its pattern's notes on, generated ones instead", not seq_during and len(gen_during) > 5,
      f"pattern notes at {seq_during[:5]}, generated {len(gen_during)}")
check("GENERATE never writes into the pattern: the same all along (all four tracks)",
      all(p["ph"] == ph0 for p in pr.values()), "")
check("GENERATE off: the pattern plays again, nothing generated", len(seq_after) > 5 and not gen_after,
      f"pattern notes {len(seq_after)}, generated at {gen_after[:5]}")
print("gen test: passed" if not bad else "gen test: FAILED")
sys.exit(1 if bad else 0)
