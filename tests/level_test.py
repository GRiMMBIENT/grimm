#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's gain staging in host mode (tests/host.c; GRIMM_MASTER: the MASTER knob, 0 .. 1023):
  - every factory sound at LEVEL 100 (T1, the default FX): a note, a chord (chord mode on), two chord keys: no sample
    reaches full scale (the limiter, just under it) with MASTER all the way up, at 3/4 or low
  - four tracks together (four mixes, chord mode off and on, DLY sends up): the same
  - SPACE's and DELAY's lines never past their soft knee, SPACE's pre-delay never clipped (the buses 6 dB lower in)
  - MASTER: up to 3/4 the output 6 dB under what it was before V2; at the top as loud as before (a note)
    python3 tests/level_test.py build/host/grimm"""
import array
import os
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/leveltest")
OUT.mkdir(parents=True, exist_ok=True)
LIM, FULL = 32440, 32767
MASTERS = (1023, 768, 300)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  level: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, master=1023):
    (OUT / f"{name}.txt").write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda x: x[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(OUT / f"{name}.txt")], capture_output=True, text=True,
                         env=dict(os.environ, GRIMM_MASTER=str(master))).stdout
    pr = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            lv = ln.split(" lv ")[1].split()
            pr[int(ln.split()[1])] = dict(lv=[int(x) for x in lv[:3]], ch=int(lv[4]), snd=ln.split(" snd1 ")[1].split(" voices")[0].strip())
    a = array.array("h", (OUT / f"{name}.wav").read_bytes()[44:])
    return pr, a


def peak(a, t0=0):
    return max(abs(x) for x in a[2 * 44100 * t0 // 1000:])


# the factory sounds, by PRESETS (T1, from power-on: 1 .. 44), and whether each loads with chord mode on
SNDS = []
for i in range(1, 45):
    pr, _ = run(f"name{i}", [(300, f"enc 6 {i}"), (3000, "probe")], 3100)
    SNDS.append((pr[3000]["snd"], pr[3000]["ch"]))
check("the 44 factory sounds on PRESETS", len(set(s for s, c in SNDS)) == 44, f"{len(set(SNDS))}")

LEVEL100 = [(3000, "enc 2 27")]                 # (HOME, KNOB 1: VOLUME, 100 -> 127: "100")
CASES = (("a note", 0x80, 0), ("a chord", 0x80, 1), ("two chord keys", 0x4080, 1))
worst = {}
for m in MASTERS:
    clip, over, knee = [], [], []
    for i, (snd, ch0) in enumerate(SNDS):
        for what, keys, ch in CASES:
            ev = [(300, f"enc 6 {i + 1}")] + LEVEL100 + [(3500, "probe"), (4000, f"keys {keys:#x}"), (9000, "probe")]
            if ch != ch0:
                ev += [(3100, "down 3"), (3900, "up 3")]   # (SEL held, let go: chord mode switched)
            pr, a = run(f"s{i}-{keys}-{ch}-{m}", ev, 9100, m)
            p = peak(a, 3500)
            worst[(m, snd, what)] = p
            if p >= FULL: clip.append(f"{snd} {what}")
            if p > LIM: over.append(f"{snd} {what} {p}")
            if pr[9000]["lv"] != pr[3500]["lv"]: knee.append(f"{snd} {what} {pr[9000]['lv']}")
    w = max((v, k) for k, v in worst.items() if k[0] == m)
    check(f"MASTER {m}: the 44 sounds at LEVEL 100, a note, a chord, two chord keys: no sample at full scale "
          f"(the loudest {w[0]}: {w[1][1]}, {w[1][2]})", not clip and not over, f"{clip[:4]} {over[:4]}")
    if m == 768:
        check("SPACE's and DELAY's lines never past their soft knee, the pre-delay never clipped (the 44 sounds)",
              not knee, f"{knee[:4]}")

# four tracks together, LEVEL 100 and DLY sends up, a key each (chord mode off, then on)
MIXES = {"loudest": (39, 41, 13, 40), "typical": (7, 1, 13, 31), "choirs and cello": (36, 39, 38, 43), "default VA": (0, 0, 0, 0)}
for m in MASTERS:
    for nm, idx in MIXES.items():
        for ch in (0, 1):
            ev = []
            for t, i in enumerate(idx):
                if i:
                    ev += [(300 + 200 * t, f"enc 6 {i}")]
                ev += [(400 + 200 * t, "enc 1 1")]
            ev += [(1300, "enc 1 -3")]
            for t in range(4):                  # (HOME: VOLUME 100; chord mode as asked, SEL held)
                ev += [(3000 + 1000 * t, "enc 2 27")]
                if ch != SNDS[idx[t] - 1][1] if idx[t] else ch:
                    ev += [(3050 + 1000 * t, "down 3"), (3850 + 1000 * t, "up 3")]
                ev += [(3950 + 1000 * t, "enc 1 1")]
            ev += [(7000, "enc 1 -3"), (7100, "btn 8"), (7150, "enc 0 1")]   # (MIXER 1: the DELAY tile, 100 a track)
            for t in range(4):
                ev += [(7200 + 100 * t, "enc 2 127"), (7250 + 100 * t, "enc 1 1")]
            ev += [(7700, "enc 1 -3"), (7800, "btn 8"), (7850, "btn 8"), (8000, "probe")]
            keys = 0
            for t, k in enumerate((0x80, 0x400, 0x4000, 0x20)):   # (C4, D#4, G4, A#3: a key each, on its track)
                keys |= k
                ev += [(8100 + 300 * t, f"keys {keys:#x}"), (8200 + 300 * t, "enc 1 1")]
            ev += [(14000, "probe")]
            pr, a = run(f"four-{nm.replace(' ', '')}-{ch}-{m}", ev, 14100, m)
            p = peak(a, 8000)
            check(f"MASTER {m}: four tracks ({nm}, chord mode {'on' if ch else 'off'}, DLY sends up): peak {p}, "
                  f"under full scale" + (", the lines under their knee" if m == 768 else ""),
                  p <= LIM and (m != 768 or pr[14000]["lv"] == pr[8000]["lv"]), f"lv {pr[8000]['lv']} -> {pr[14000]['lv']}")

# MASTER's curve: a single quiet note (no limiting): at 3/4 half the top's (6 dB), at the top as before V2 (x 0.999)
ps = {}
for m in (1023, 768):
    _, a = run(f"curve{m}", [(300, "enc 6 1"), (4000, "keys 0x80")], 7000, m)
    ps[m] = peak(a, 4500)
want = ps[1023] * 2304 // 4092 // 2             # (before V2, at 3/4: 768^2 / 256 of 4092; now 6 dB under that)
check(f"MASTER at 3/4: 6 dB under its level before V2 (peaks {ps[768]}, wanted {want}; the top {ps[1023]})",
      abs(ps[768] - want) <= want // 50 + 2)
print("level test: passed" if not bad else "level test: FAILED")
sys.exit(1 if bad else 0)
