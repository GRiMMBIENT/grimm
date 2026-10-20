#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's effects in host mode (tests/run_host.sh): the track filter's TYPE (LP, HP, BP) and POS (PRE, POST: the
sends unfiltered), MASTER (a filter at PRE FX, POST REVERB, END; open: as without it, to the byte), DRIVE (its four
TYPEs, its limiter; AMOUNT 0: as without it, to the byte), and their saves (a V1.0 project: LP PRE, MASTER open, DRIVE
off; V2's: as set). A held saw chord on T1 through each; the sound measured: its level (RMS), its highs (the RMS of
its slope) and its lows (the RMS of a 64-frame average). Prints a line a check, exit 1 on a failure.
  python3 tests/fx_test.py [build/host/grimm]"""
import array
import os
import subprocess
import sys
import wave
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/fx")
OUT.mkdir(parents=True, exist_ok=True)
CHORD = 0x880                                   # (two keys, an octave apart: T1's VA saw)
bad = 0


def run(name, ev, end=4200, env=None):
    ev = [(1000, f"keys {CHORD:#x}")] + ev + [(end - 100, "probe")]
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=env).stdout
    w = wave.open(str(OUT / f"{name}.wav"))
    a = array.array("h", w.readframes(w.getnframes()))
    probe = [ln for ln in out.splitlines() if ln.startswith("probe ")][-1]
    return a, probe, (OUT / f"{name}.wav").read_bytes()


def measure(a, t0=2500, t1=4000):               # the left channel from t0 to t1 ms: level, highs, lows, peak
    x = a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000:2]
    n = len(x)
    rms = (sum(v * v for v in x) / n) ** .5
    hi = (sum((x[i] - x[i - 1]) ** 2 for i in range(1, n)) / n) ** .5
    lo = (sum((sum(x[i:i + 64]) / 64) ** 2 for i in range(0, n - 64, 64)) / (n // 64)) ** .5
    pk = max(abs(v) for v in a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000])
    return rms, hi, lo, pk


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  fx: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


MIX2 = [(300, "btn 8"), (350, "btn 8")]         # HOME: MIXER 1, MIXER 2
HOME = [(600, "btn 6"), (650, "btn 8")]         # (EDIT, then HOME: HOME)
FX = lambda k: [(700 + 50 * i, "btn 2") for i in range(k)]   # FX tapped k times: TAPE .. DRIVE (6)

base, _, base_b = run("base", [])
rb, hb, lb, pb = measure(base)
# the track filter's TYPE (the chord: C4 E4, 262 and 330 Hz): LP at ~1 kHz (HOME's CUTOFF 127 -> 72), HP and BP at
# ~710 Hz (64)
lp, _, _ = run("lp", HOME + [(800, "enc 3 -55")])
hp, _, _ = run("hp", MIX2 + [(400, "enc 3 1")] + HOME + [(800, "enc 3 -63")])
# (BP with RESONANCE 80: at 0 a band-pass's Q is 0.5, wide and mild)
bp, _, _ = run("bp", MIX2 + [(400, "enc 3 2")] + HOME + [(800, "enc 3 -63"), (850, "enc 4 80")])
r, h, lo_, _ = measure(lp)
check("track LP: the highs down, the lows kept", h < hb * 0.6 and lo_ > lb * 0.8, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
r, h, lo_, _ = measure(hp)
check("track HP: the lows down, the highs kept", lo_ < lb * 0.5 and h > hb * 0.6, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
r, h, lo_, _ = measure(bp)
check("track BP (RESONANCE 80): both ends down", lo_ < lb * 0.7 and h < hb * 0.8, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
# POS: LP closed (CUTOFF 15), the REV send full, the sound let go at 3000: the reverb's tail measured
SEND = [(300, "btn 8"), (320, "enc 4 127")]     # (MIXER 1: REV SEND 0 -> 127)
pre, _, _ = run("pre", SEND + [(350, "btn 8"), (400, "enc 4 0")] + HOME + [(800, "enc 3 -112"), (3000, "keys 0")], 5000)
post, pp, _ = run("post", SEND + [(350, "btn 8"), (400, "enc 4 1")] + HOME + [(800, "enc 3 -112"), (3000, "keys 0")], 5000)
_, h_pre, _, _ = measure(pre, 3300, 4800)
_, h_post, _, _ = measure(post, 3300, 4800)
check("track POS POST: the reverb hears it unfiltered (its tail brighter than PRE's)", h_post > h_pre * 1.5,
      f"tail highs POST {h_post:.0f}, PRE {h_pre:.0f}")
# MASTER: open (as set out): the same bytes; LP at 30, at each POSITION: darker
mo, _, mo_b = run("m_open", FX(5) + [(1200, "enc 5 -1")])   # (POSITION END -> POST REVERB, still open)
check("MASTER open: the sound as without it, to the byte", mo_b == base_b)
for pos, steps in (("PRE FX", -2), ("POST REVERB", -1), ("END", 0)):
    m, _, _ = run(f"m_{steps}", FX(5) + [(1200, "enc 2 -97"), (1300, f"enc 5 {steps}")])
    r, h, lo_, _ = measure(m)
    check(f"MASTER LP at {pos}: the highs down", h < hb * 0.5, f"hi {h:.0f}/{hb:.0f}")
mh, _, _ = run("m_hp", FX(5) + [(1200, "enc 4 1"), (1300, "enc 2 -40")])   # (HP at 87)
r, h, lo_, _ = measure(mh)
check("MASTER HP: the lows down", lo_ < lb * 0.5, f"lo {lo_:.0f}/{lb:.0f}")
# DRIVE: AMOUNT 0 (TYPE turned, still off): the same bytes; each TYPE its own sound; a loud chord at full: limited
d0, _, d0_b = run("d_off", FX(6) + [(1200, "enc 3 2"), (1300, "enc 4 -30")])
check("DRIVE off (AMOUNT 0): the sound as without it, to the byte", d0_b == base_b)
outs = []
for ty in range(4):
    d, _, db = run(f"d_{ty}", FX(6) + [(1200, "enc 2 100"), (1300, f"enc 3 {ty}")])
    outs.append(db)
    r, h, lo_, pk = measure(d)
    check(f"DRIVE {['SOFT', 'FUZZ', 'CLIP', 'FOLD'][ty]}: driven (louder, changed), within the limiter",
          r > rb * 1.2 and db != base_b and pk <= 30200, f"rms {r:.0f}/{rb:.0f} peak {pk}")
check("DRIVE: the four TYPEs sound different", len(set(outs)) == 4)
loud, _, _ = run("d_loud", [(1000, "keys 0x1f0ff")] + FX(6) + [(1200, "enc 2 127"), (1300, "enc 4 -127")])
_, _, _, pk = measure(loud)
check("DRIVE full on a big chord, TONE closed: the limiter holds it (no clipping)", pk <= 30200, f"peak {pk}")
# saves: V2's as set (autosaved, loaded again), V1.0's project: the defaults
SAVE = MIX2 + [(400, "enc 3 2"), (420, "enc 4 1")] + FX(5) + [(1200, "enc 2 -60"), (1250, "enc 3 40"), (1300, "enc 4 1"),
                                                             (1350, "enc 5 -2"), (1400, "btn 2"), (1500, "enc 2 50"),
                                                             (1550, "enc 3 3"), (1600, "enc 4 -27"), (1650, "enc 5 -60"),
                                                             (1700, "keys 0")]
_, p1, _ = run("save", SAVE, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "saves.bin")))
_, p2, _ = run("load", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "saves.bin")))
want = "fx mf 67,40,1,0 dr 50,3,100,67 ft 6 0 0 0"
check("saved and loaded: MASTER, DRIVE, the track filter as set", want in p1 and want in p2,
      f"set: {p1.split(' fx ')[-1]}; loaded: {p2.split(' fx ')[-1]}")
_, p3, _ = run("v10", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("a GR!MM 1.0 project: LP PRE, MASTER open (END), DRIVE off", "fx mf 127,0,0,2 dr 0,0,127,127 ft 0 0 0 0" in p3,
      p3.split(" fx ")[-1])
print("fx test: passed" if not bad else "fx test: FAILED")
sys.exit(1 if bad else 0)
