#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 item 7 in host mode (tests/run_host.sh): chords stay whole when other tracks play.
T1 plays a 16-step pattern of 3-note chords (chord mode on, SCALE major); T2 .. T4 play single notes; all four share the six
voices. A chord's 2nd and further notes may take the quietest release tail (a fading note) of another track, never a note that
is sounding; the first note and plain key notes behave as before. So:
  - the 9 combinations of T1's and the others' RELEASE (short / as it is / long), three other tracks playing: 112 of 112 chord
    steps whole (7 loops of 16: the first loop starts cold and is left out), and the other tracks play all their notes (8 of 8)
  - the drift case (T2 with 11 steps, T3 with 13 steps, they drift against T1), three releases: every chord of every loop whole
  - stop / play again, 8 runs each, three release series: every chord whole, in every run
(the counts: how many notes sound in each 1/16 of a loop, from the probe's notes; the player's own scripts, tests/chordvoice_test.py)
  python3 tests/chordvoice_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/chordvoice")
OUT.mkdir(parents=True, exist_ok=True)
K = {"C": 7, "D": 9, "E": 11, "F": 12, "G": 14, "A": 16}             # the keys (bit k = F3 + k) of the C major scale's notes
SEQ16 = [("C",), ("E",), ("G",), ("D",), ("F",), ("A",), ("E",), ("C",), ("G",), ("D",), ("A",), ("F",), ("E",), ("C",), ("D",), ("G",)]
SEQS = [("A",), ("G",), ("F",), ("E",), ("D",), ("C",), ("G",), ("A",)]
SEQS2 = [("C",), ("C",), ("G",), ("G",), ("A",), ("A",), ("G",), ("F",)]
LOOP = 2000                                     # a loop of 16 steps at 120 BPM, in ms
REL = {-1: "short", 0: "as it is", 1: "long"}
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  chord voices: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end):
    """-> {ms: {track 1..4: the notes sounding}}"""
    p = OUT / f"{name}.txt"
    p.write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda q: q[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(p)], capture_output=True, text=True, check=True).stdout
    pr = {}
    for ln in out.splitlines():
        if not ln.startswith("probe "):
            continue
        notes = {t: set() for t in (1, 2, 3, 4)}
        for x in re.search(r" notes(.*?) fx mf", ln).group(1).split():
            t, n = x.split(":")
            notes[int(t)].add(int(n))
        pr[int(ln.split()[1])] = notes
    return pr


def tog(t):                                     # SEL held and let go: the selected track's chord mode
    return [(t, "down 3"), (t + 800, "up 3")]


def scale(t, root, sc):                         # SEL > SCALE: ROOT, SCALE
    return [(t, "btn 3"), (t + 50, f"enc 2 {root}"), (t + 100, f"enc 3 {sc}")]


def keys(*ks):
    return sum(1 << K[k] for k in ks)


def rec(t, seq):                                # the notes of seq into the selected track (SEQ page, REC, the cursor goes on)
    ev = [(t, "btn 11"), (t + 50, "btn 13")]
    t += 100
    for s in seq:
        ev += [(t, f"keys {keys(*s):#x}"), (t + 60, "keys 0")]
        t += 100
    return ev + [(t, "btn 13"), (t + 50, "btn 8")], t + 100


def env_rel(t, r):                              # ENV page, KNOB 4 = RELEASE: r in -1 (shortest), 0 (as it is), +1 (longest)
    return [(t, "btn 4"), (t + 50, f"enc 5 {127 * r}")] if r else []


def build(rel, other_rel, steps=None):          # T1: chord mode, 16 steps of chords; T2 .. T4 single notes; the selected track back to T1
    ev = scale(300, 0, 2)
    t = 1000
    for tr in range(4):
        if tr:
            ev += [(t, "enc 1 1")]
            t += 100
        ev += env_rel(t, rel if tr == 0 or other_rel is None else other_rel)
        t += 150
        if tr == 0:
            ev += tog(t)
            t += 900
        if steps and steps.get(tr):
            ev += [(t, "down 11"), (t + 20, f"enc 0 {steps[tr] - 16}"), (t + 40, "up 11")]
            t += 100
        r, t = rec(t, SEQ16 if tr == 0 else (SEQS, SEQS2, SEQS)[tr - 1])
        ev += r
    return ev + [(t, "enc 1 -3")], t + 400


def analyse(pr, trk, t0, nloops):               # per loop, per 1/16 of it: the most notes of track trk sounding
    ms = sorted(m for m in pr if m >= t0)
    res = []
    for L in range(nloops):
        a = t0 + L * LOOP
        res.append([max([len(pr[m][trk]) for m in ms if a + st * LOOP // 16 <= m < a + (st + 1) * LOOP // 16] or [0]) for st in range(16)])
    return res


def probes(t0, ms):
    return [(t0 + 25 * i, "probe") for i in range(int(ms / 25))]


def grid_cell(r1, ro):
    ev, p0 = build(r1, ro)
    nl = 8
    pr = run(f"grid_{r1}_{ro}", ev + [(p0, "btn 12")] + probes(p0, nl * LOOP + 500), p0 + nl * LOOP + 600)
    c = Counter(x for r in analyse(pr, 1, p0 + 20, nl)[1:] for x in r)
    oth = [sum(1 for r in analyse(pr, tr, p0 + 20, nl)[1:] for x in r if x > 0) / 7.0 for tr in (2, 3, 4)]
    return (r1, ro), c, oth


def drift_case(rel):
    ev, p0 = build(rel, None, steps={1: 11, 2: 13})
    nl = 20
    pr = run(f"drift_{rel}", ev + [(p0, "btn 12")] + probes(p0, nl * LOOP + 500), p0 + nl * LOOP + 600)
    return rel, analyse(pr, 1, p0 + 20, nl)


def series(rel, ro):                            # stop / play again, 8 runs: the chord size in each
    ev, p0 = build(rel, ro)
    starts = [p0 + i * 9000 for i in range(8)]
    for s in starts:
        ev += [(s, "btn 12"), (s + 6000, "btn 12")] + probes(s + 100, 5800)
    pr = run(f"series_{rel}_{ro}", ev, starts[-1] + 9000)
    res = []
    for s in starts:
        c = Counter(x for r in analyse(pr, 1, s + 2100, 2) for x in r)
        res.append(c)
    return (rel, ro), res


with ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 2))) as ex:
    f_grid = [ex.submit(grid_cell, r1, ro) for r1 in (-1, 0, 1) for ro in (-1, 0, 1)]
    f_drift = [ex.submit(drift_case, rel) for rel in (-1, 0, 1)]
    f_series = [ex.submit(series, rel, ro) for rel, ro in ((0, 0), (1, 0), (-1, 1))]
    grid = [f.result() for f in f_grid]
    drift = [f.result() for f in f_drift]
    ser = [f.result() for f in f_series]

whole = sum(c.get(3, 0) for _, c, _ in grid)
check("9 release combinations (T1 short / as it is / long, the others the same), three other tracks playing: 112 of 112 chord steps whole "
      "in each (9 x 112 = %d of 1008)" % whole,
      all(c.get(3, 0) == 112 for _, c, _ in grid), "; ".join(f"T1 {REL[k[0]]} / others {REL[k[1]]}: {dict(c)}" for k, c, _ in grid if c.get(3, 0) != 112))
check("... and the other tracks still play all their notes (8 of 8 a loop, T2 .. T4, in all 9)",
      all(abs(o - 8.0) < 0.01 for _, _, oth in grid for o in oth), "; ".join(f"T1 {REL[k[0]]} / others {REL[k[1]]}: {oth}" for k, _, oth in grid
                                                                              if any(abs(o - 8.0) >= 0.01 for o in oth)))
def whole_row(row):                             # a loop (16 windows): every chord 3 notes
    return Counter(row) == Counter({3: 16})


check("the drift case (T2 11 steps, T3 13 steps), three releases: every chord of 19 loops whole (16 of 16 a loop; the first loop starts cold, the "
      "recorded notes still fading, and is left out)",
      all(all(whole_row(r) for r in rows[1:]) for _, rows in drift),
      "; ".join(f"{REL[r]}: {[dict(Counter(x)) for x in rows]}" for r, rows in drift if not all(whole_row(x) for x in rows[1:])))
check("3 stop / play series (8 runs each): every chord whole, in every run (no chord of 1 or 2 notes)",
      all(not c.get(1) and not c.get(2) and c.get(3, 0) >= 31 for _, runs_ in ser for c in runs_),
      "; ".join(f"T1 {REL[k[0]]} / others {REL[k[1]]}: {[dict(c) for c in runs_]}" for k, runs_ in ser
                if any(c.get(1) or c.get(2) or c.get(3, 0) < 31 for c in runs_)))
sys.exit(1 if bad else 0)
