#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's GENERATE in host mode (tests/host.c, its "probe": gen, ph, srcs):
  - every note it plays is in the SCALE (C NATURAL MINOR), in each MODE, chord mode off and on
  - a track's pattern: playing; GENERATE on: its steps unheard (none of its sequenced notes on), the pattern itself
    unchanged; off: the pattern plays again, unchanged
  - a track's key switches it when let go, and not when another key came meanwhile; its key held + D#4: its MODE
    alone, D#4 alone: all four; F#4 G#4 its OCTAVE, A#4 its RANGE: no note outside the range (chords folded in)
  - the tempo: faster, its notes come faster, at once (stopped and playing; INT and INT+OUT)
  - four tracks generating STORM (PHYS, two choirs, PHASE), chord mode off and on: each its share of the six voices, no
    generated note takes another track's voice, no chord note within its share left out, every track heard; one
    track generating: to the byte as before (against the build given as the second argument, if one)
  - a SAMPLE track's notes: up to the whole loop (its LENGTH), past the MODE's lengths; another source: the MODE's
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
            pr[ms] = dict(gen=int(g[0]), mode=[int(x) for x in g[1].split(",")],
                          oc=[int(x) for x in ln.split(" oc ")[1].split()[0].split(",")],
                          rg=[int(x) for x in ln.split(" rg ")[1].split()[0].split(",")],
                          own=int(ln.split(" own ")[1].split()[0]),
                          gv=[int(x) for x in ln.split(" gv ")[1].split()[:2]],
                          left=[int(x) for x in ln.split(" gv ")[1].split()[2].split(",")],
                          ph=ln.split(" ph ")[1].split()[0].split(","), srcs=srcs)
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
              "the SCALE", notes and not out and all(p["mode"] == [mode] * 4 for p in pr.values()), f"out of it: {out}")

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

# the keys: a track's key switches it when let go; held with another key (a white one): not switched
K = {"F#3": 1, "G#3": 3, "A#3": 5, "C#4": 8, "D#4": 10, "F#4": 13, "G#4": 15, "A#4": 17, "G3": 2}
B = lambda *ks: sum(1 << K[k] for k in ks)      # noqa: E731
ev = [(3000, "down 2"), (3100, f"keys {B('F#3'):#x}"), (3200, "probe"), (3300, "keys 0"), (3400, "probe"),
      (3500, f"keys {B('G#3'):#x}"), (3550, f"keys {B('G#3', 'G3'):#x}"), (3600, f"keys {B('G#3'):#x}"), (3700, "keys 0"),
      (3800, "probe"), (3900, "up 2")]
pr = run("release", ev, 4000)
check("a track's key: switched when let go (not when pressed)", pr[3200]["gen"] == 0 and pr[3400]["gen"] == 1,
      f"{pr[3200]['gen']} {pr[3400]['gen']}")
check("a track's key held while another key is pressed: not switched", pr[3800]["gen"] == 1, f"{pr[3800]['gen']}")

# MODE, OCTAVE, RANGE: a track's key held: its own; none held: all four (the first one's next)
ev = [(3000, "down 2")]
t = 3100
for st in ((B("G#3"), B("G#3", "D#4"), B("G#3"), 0, "probe"),          # T2 MODE: PULSE
           (B("D#4"), 0, "probe"),                                       # all: T1's next: PULSE
           (B("A#3"), B("A#3", "G#4"), B("A#3"), B("A#3", "G#4"), B("A#3"), 0, "probe"),   # T3 OCTAVE +2
           (B("F#4"), 0, "probe"),                                       # all: T1's OCTAVE - 1: -1
           (B("C#4"), B("C#4", "A#4"), B("C#4"), 0, "probe"),            # T4 RANGE: 3
           (B("A#4"), 0, "probe")):                                      # all: T1's next RANGE: 3
    for m in st:
        ev.append((t, "probe" if m == "probe" else f"keys {m:#x}"))
        t += 60
ev.append((t + 50, "up 2"))
pr = run("settings", ev, t + 200)
ps = [pr[ms] for ms in sorted(pr)]
check("a track's key + D#4: its MODE alone (T2 PULSE)", ps[0]["mode"] == [0, 1, 0, 0] and ps[0]["gen"] == 0,
      f"{ps[0]['mode']} gen {ps[0]['gen']}")
check("D#4 alone: all four's MODE together (PULSE)", ps[1]["mode"] == [1, 1, 1, 1], f"{ps[1]['mode']}")
check("a track's key + G#4 twice: its OCTAVE alone (T3 +2)", ps[2]["oc"] == [0, 0, 2, 0], f"{ps[2]['oc']}")
check("F#4 alone: all four's OCTAVE (T1's down: -1)", ps[3]["oc"] == [-1, -1, -1, -1], f"{ps[3]['oc']}")
check("a track's key + A#4: its RANGE alone (T4 3 OCT); A#4 alone: all four (T1's next: 3)",
      ps[4]["rg"] == [2, 2, 2, 3] and ps[5]["rg"] == [3, 3, 3, 3] and all(p["gen"] == 0 for p in ps),
      f"{ps[4]['rg']} {ps[5]['rg']} gen {[p['gen'] for p in ps]}")

# the range: T1 OCTAVE -1, RANGE 1 (C2 .. B2), T2 OCTAVE +1, RANGE 3 (C4 .. B6), both STORM, chord mode on T1 (its
# chord's notes folded into the range); C minor: every note in its range and in the scale
ev = SCALE + CHORD
t = 1200
for st in ((B("F#3"), B("F#3", "F#4"), B("F#3"), B("F#3", "A#4"), B("F#3"), B("F#3", "A#4"), B("F#3"),
            B("F#3", "D#4"), B("F#3"), B("F#3", "D#4"), B("F#3"), 0, B("F#3"), 0),
           (B("G#3"), B("G#3", "G#4"), B("G#3"), B("G#3", "A#4"), B("G#3"), B("G#3", "D#4"), B("G#3"),
            B("G#3", "D#4"), B("G#3"), 0, B("G#3"), 0)):
    for m in st:
        ev.append((t, f"keys {m:#x}"))
        t += 60
ev = [(1150, "down 2")] + ev + [(t + 50, "up 2")]
ev += [(t + 500 + 40 * i, "probe") for i in range(500)]
pr = run("range", ev, t + 500 + 40 * 500 + 200)
p0 = pr[min(pr)]
n1 = [n for p in pr.values() for s, tr, n in p["srcs"] if s == SRC_GEN and tr == 1]
n2 = [n for p in pr.values() for s, tr, n in p["srcs"] if s == SRC_GEN and tr == 2]
check("the settings for the range test (T1 -1 / 1 OCT, T2 +1 / 3 OCT, both STORM, both on)",
      p0["oc"][:2] == [-1, 1] and p0["rg"][:2] == [1, 3] and p0["mode"][:2] == [2, 2] and p0["gen"] & 3 == 3,
      f"{p0}")
check(f"T1 (chords, folded): {len(n1)} notes seen, all in C2 .. B2 and in the scale",
      n1 and all(36 <= n <= 47 and n % 12 in MINOR for n in n1), f"{sorted(set(n1))}")
check(f"T2: {len(n2)} notes seen, all in C4 .. B6 and in the scale, over more than two octaves",
      n2 and all(60 <= n <= 95 and n % 12 in MINOR for n in n2) and max(n2) - min(n2) > 24, f"{sorted(set(n2))}")

# the tempo: PULSE, T1, 60 BPM, then 180 BPM (SELECT on HOME): its notes three times as often, from then on;
# stopped and playing, CLOCK INT and INT+OUT
for clock, play in ((0, 0), (0, 1), (1, 0), (1, 1)):
    ev = [(500, "down 8"), (1300, "up 8"), (1400, "btn 8"), (1500, f"enc 5 {clock}"), (1600, "btn 6"), (1700, "btn 8"),
          (1800, "enc 0 -60")] + fx_key(2000, 1) + fx_key(2100, 10) + ([(2300, "btn 12")] if play else [])
    ev += [(14000, "enc 0 120")]
    ev += [(3000 + 10 * i, "probe") for i in range(2300)]
    pr = run(f"tempo{clock}{play}", ev, 26200)
    starts, prev = [], None
    for ms in sorted(pr):
        g = tuple(sorted(n for s, tr, n in pr[ms]["srcs"] if s == SRC_GEN))
        if g and g != prev:
            starts.append(ms)
        prev = g
    a = len([x for x in starts if 3000 <= x < 14000]) / 11.0
    b = len([x for x in starts if 14000 <= x < 25000]) / 11.0
    check(f"the tempo 60 -> 180 BPM ({'INT+OUT' if clock else 'INT'}, {'playing' if play else 'stopped'}): "
          f"its notes {a:.1f} -> {b:.1f} a second", pr[4000]["own"] == 60 and pr[15000]["own"] == 180 and b > 2.2 * a,
          f"tempo {pr[4000]['own']} -> {pr[15000]['own']}")

# four tracks generating STORM: T1 BOWED BELL (PHYS), T2 GREGORIAN (choir, chord mode on), T3 WRAITH (PHASE), T4 CATHEDRAL
# CHOIR (choir, chord mode on); chord mode then all off, or all on (SEL held, let go, on the tracks to switch)
OLD = sys.argv[2] if len(sys.argv) > 2 else None
LOAD = [(300, "enc 6 17"), (400, "enc 1 1"), (500, "enc 6 36"), (600, "enc 1 1"), (700, "enc 6 25"), (800, "enc 1 1"),
        (900, "enc 6 38"), (1000, "enc 1 -3")]


def chords(on):                                 # (T1, T3 off as loaded: on; T2, T4 on as loaded: off)
    a, b = (0, 2) if on else (1, 3)
    return [(1100, f"enc 1 {a}"), (1200, "down 3"), (2000, "up 3"), (2100, f"enc 1 {b - a}"), (2200, "down 3"),
            (3000, "up 3"), (3100, f"enc 1 {-b}")]


ALL4 = [(3500, "down 2")] + sum(([(3600 + 100 * i, f"keys {1 << k:#x}"), (3650 + 100 * i, "keys 0")]
                                 for i, k in enumerate((1, 3, 5, 8))), [])
ALL4 += [(4100, "keys 0x400"), (4150, "keys 0"), (4200, "keys 0x400"), (4250, "keys 0"), (4400, "up 2")]   # (ALL STORM)
for on in (0, 1):
    pr = run(f"share{on}", LOAD + chords(on) + ALL4 + [(5000 + 50 * i, "probe") for i in range(500)], 30100)
    last = pr[max(pr)]
    heard = [sum(1 for p in pr.values() for s, tr, n in p["srcs"] if s == SRC_GEN and tr == t) for t in (1, 2, 3, 4)]
    most = [max(sum(1 for s, tr, n in p["srcs"] if s == SRC_GEN and tr == t) for p in pr.values()) for t in (1, 2, 3, 4)]
    check(f"4 tracks STORM, chord mode {'on' if on else 'off'}: all four generating, every track heard ({heard})",
          last["gen"] == 15 and last["mode"] == [2, 2, 2, 2] and min(heard) > 20, f"gen {last['gen']} {last['mode']}")
    check(f"4 tracks STORM, chord mode {'on' if on else 'off'}: no generated note took another track's voice, no chord "
          f"note within its share left out", last["gv"] == [0, 0], f"took, left out: {last['gv']}")
    if on:
        check(f"chord mode on: chords within each track's share (6 voices / 4: 2, 2, 1, 1; most at once {most})",
              most[0] == 2 and most[1] == 2 and most[2] == 1 and most[3] == 1)
if OLD:
    keep = BIN
    for b, nm in ((BIN, "one-new"), (OLD, "one-old")):
        BIN = b
        run(nm, SCALE + CHORD + fx_key(1200, 1) + fx_key(1400, 10) + fx_key(1500, 10), 20000)
    BIN = keep
    check("one track generating (STORM, chord mode on): to the byte as before",
          (OUT / "one-new.wav").read_bytes() == (OUT / "one-old.wav").read_bytes())

# a SAMPLE track's notes: up to its loop (URCHIN, 6.64 s: ~1275 ticks at 120 BPM), past STORM's longest (48 ticks); a VA
# track's: STORM's
SAMPLE = [(300, "btn 6"), (400, "btn 6"), (500, "enc 2 2"), (600, "btn 8")]
for src, ev in (("SAMPLE", SAMPLE), ("VA", [])):
    pr = run(f"len{src}", ev + fx_key(1200, 1) + fx_key(1400, 10) + fx_key(1500, 10) +
             [(3000 + 50 * i, "probe") for i in range(600)], 33100)
    mx = max(p["left"][0] for p in pr.values())
    if src == "SAMPLE":
        check(f"STORM on a SAMPLE track: notes up to the whole loop (longest seen {mx} ticks; STORM's 48, the loop 1275)",
              200 < mx <= 1275)
    else:
        check(f"STORM on a VA track: the MODE's lengths (longest seen {mx} ticks, at most 48)", 0 < mx <= 48)
print("gen test: passed" if not bad else "gen test: FAILED")
sys.exit(1 if bad else 0)
