#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2.0's chord mode is per track (host mode, firmware/src/host.c; the probe's "notes" and "chd", firmware/src/probe.c): a
track's chord on / off, CHORD TYPE and VOICING apply to that track only; only the SCALE is shared.
  - T1 chord on, T2 .. T4 off; each track played every way a note can play: the keys, MIDI in (a channel a track;
    ALL > SEL; CH n > SEL), the sequencer (steps recorded with REC, steps entered on GRID), ARP, GENERATE: only T1
    plays chords
  - T2 on too, another CHORD TYPE and VOICING: both right, T3 and T4 still single notes (a key, then three keys held at
    once, ALGORITHM turned between)
  - a key held while ALGORITHM changes the track; a preset loaded on T2 (it never switches chord mode on); a project (slot A) saved and recalled; NEW
  - the SEQ grid's "CHORD" caption and SEL's CHORD page show the selected track's chord mode
    python3 tests/chord_test.py build/host/grimm"""
import os
import re
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/chordtest")
OUT.mkdir(parents=True, exist_ok=True)
bad = 0

# CHORD TYPE: its notes' semitones over the note (scale.c CH_IV; SCALE with no SCALE set: MIN)
IV = [(0, 3, 7), (0, 3, 7), (0, 4, 7), (0, 2, 7), (0, 5, 7), (0, 7), (0, 3, 7, 10), (0, 4, 7, 11), (0, 3, 6), (0, 4, 7, 14),
      (0, 2, 3)]
C4, E4 = 60, 64
KEY_C, KEY_E = 0x80, 0x800                      # (keys 7 and 11 of the FM-1's 27: C4, E4 at OCTAVE +0)
STEP = (1, 3, 5, 8)                             # (the black keys of GENERATE: T1 .. T4)


def chord(root, ty=0, vo=0):                    # (CHORD TYPE ty, then VOICING vo: CLOSE, OPEN, SPREAD, +OCT, DROP)
    n = [root + i for i in IV[ty]]
    if vo == 1 and len(n) >= 3:
        n[1] += 12
    elif vo == 2:
        n = [n[0] - 12] + [x + 12 for x in n[1:]]
    elif vo == 3:
        n = n + [root + 12]
    elif vo == 4:
        n[0] -= 12
    return sorted(set(n))


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  chord: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, fps=0):
    """the script run: {ms: ({track 1..4: the notes on}, [(chord mode, CHORD TYPE, VOICING) of each track])} per probe"""
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda q: q[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(path)] + ([str(fps)] if fps else []), capture_output=True, text=True,
                         check=True).stdout
    pr = {}
    for ln in out.splitlines():
        if not ln.startswith("probe "):
            continue
        notes = {t: set() for t in (1, 2, 3, 4)}
        for x in re.search(r" notes(.*?) fx mf", ln).group(1).split():
            t, n = x.split(":")
            notes[int(t)].add(int(n))
        pr[int(ln.split()[1])] = ({t: sorted(v) for t, v in notes.items()},
                                  [tuple(int(v) for v in x.split("/")) for x in ln.split(" chd ")[1].split(" voices")[0].split()])
    return pr


def tog(t):                                     # SEL held, let go: chord mode on / off for the selected track
    return [(t, "down 3"), (t + 800, "up 3")]


def ctype(t, ty, vo):                           # SEL, SEL again: CHORD page; KNOB 1 CHORD TYPE, KNOB 2 VOICING (turned from 0)
    return [(t, "btn 3"), (t + 50, "btn 3"), (t + 100, f"enc 2 {ty}"), (t + 150, f"enc 3 {vo}")]


def to(t, frm, tr):                             # ALGORITHM: the track from frm to tr (0 .. 3)
    return [(t, f"enc 1 {tr - frm}")] if tr != frm else []


def go(t, tr):                                  # (the tracks one after the other: T1 is selected, then T2, ...)
    return to(t, tr - 1, tr) if tr else []


def total(snap):                                # (the notes on, all tracks)
    return sum(len(v) for v in snap.values())


T1_ON = tog(300)                                # (T1 is selected at power-on: chord mode on, CHORD TYPE SCALE, no SCALE: MIN)
PLAY = [(f"keys {KEY_C:#x}", "keys 0")]

# ---- the keys: each track played alone
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += go(t - 100, tr) + [(t, f"keys {KEY_C:#x}"), (t + 200, "probe"), (t + 300, "keys 0")]
    t += 800
pr = run("keys", ev, t + 200)
got = [pr[m][0] for m in sorted(pr)]
check("keys: T1 (chord on) plays its chord, T2 .. T4 (chord off) each the one note",
      got[0][1] == chord(C4) and total(got[0]) == 3 and all(got[i][i + 1] == [C4] and total(got[i]) == 1 for i in (1, 2, 3)), got)
check("keys: chord mode on T1 only", pr[min(pr)][1][0][0] == 1 and all(pr[max(pr)][1][i][0] == 0 for i in (1, 2, 3)),
      pr[max(pr)][1])

# ---- MIDI in, a channel a track (MIDI CHANNEL 1-4)
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += [(t, f"midi {0x90 + tr:#x} 60 100"), (t + 200, "probe"), (t + 300, f"midi {0x80 + tr:#x} 60 0")]
    t += 800
pr = run("midi", ev, t + 200)
got = [pr[m][0] for m in sorted(pr)]
check("MIDI in, a channel a track: T1 plays its chord, T2 .. T4 the one note",
      got[0][1] == chord(C4) and total(got[0]) == 3 and all(got[i][i + 1] == [C4] and total(got[i]) == 1 for i in (1, 2, 3)), got)

# ---- MIDI CHANNEL ALL > SEL (SYSTEM: HOME held, KNOB 2 to 13) and CH 3 > SEL (16): the selected track plays
for nm, steps, ch in (("ALL > SEL", 13, 5), ("CH 3 > SEL", 16, 2)):
    ev = list(T1_ON) + [(1100, "down 8"), (1900, "up 8"), (2000, f"enc 3 {steps}"), (2200, "btn 8"), (2300, "btn 8")]
    t = 3000
    for tr in range(4):
        ev += go(t - 100, tr) + [(t, f"midi {0x90 + ch:#x} 60 100"), (t + 200, "probe"), (t + 300, f"midi {0x80 + ch:#x} 60 0")]
        t += 800
    pr = run("midi_" + nm.split()[0].lower(), ev, t + 200)
    got = [pr[m][0] for m in sorted(pr)]
    check(f"MIDI in, {nm}: the selected track plays: T1 its chord, T2 .. T4 the one note",
          got[0][1] == chord(C4) and total(got[0]) == 3 and all(got[i][i + 1] == [C4] and total(got[i]) == 1 for i in (1, 2, 3)),
          got)

# ---- the sequencer: C4, E4 recorded on each track with REC, on steps of their own (T1: 1, 2; T2: 5, 6; T3: 9, 10; T4: 13, 14:
# the six voices are every track's: one track plays at a time, its chord whole), then all four play (T1's are chord steps)
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += [(t, "btn 11")] + ([(t + 50, f"enc 0 {4 * tr}")] if tr else []) + [(t + 100, "btn 13"), (t + 150, f"keys {KEY_C:#x}"),
                                                                           (t + 250, "keys 0"), (t + 300, f"keys {KEY_E:#x}"),
                                                                           (t + 400, "keys 0"), (t + 450, "btn 13"), (t + 500, "btn 8"),
                                                                           (t + 600, "enc 1 1")]
    t += 800
ev += [(t, "enc 1 -3"), (t + 100, "btn 12")] + [(t + 300 + 50 * i, "probe") for i in range(80)]
pr = run("seq", ev, t + 4500)
sn = [pr[m][0] for m in sorted(pr)]
mx = [max(len(s[tr]) for s in sn) for tr in (1, 2, 3, 4)]
seen = [sorted({n for s in sn for n in s[tr]}) for tr in (1, 2, 3, 4)]
check("sequencer, steps recorded with REC: T1 plays its chord steps, T2 .. T4 single notes",
      mx == [3, 1, 1, 1] and set(seen[0]) <= set(chord(C4)) | set(chord(E4)) and all(s == [C4, E4] for s in seen[1:]),
      f"most notes at once {mx}, notes {seen}")

# ---- the sequencer, steps entered on GRID (note steps: the track's chord mode when they play)
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += [(t, "btn 11")] + ([(t + 20, f"enc 0 {4 * tr}")] if tr else []) + [(t + 50, "enc 2 1"), (t + 100, "enc 0 1"),
                                                                           (t + 150, "enc 2 3"), (t + 200, "btn 8"), (t + 250, "enc 1 1")]
    t += 400
ev += [(t, "enc 1 -3"), (t + 50, "btn 12")] + [(t + 300 + 50 * i, "probe") for i in range(80)]
pr = run("grid", ev, t + 4500)
sn = [pr[m][0] for m in sorted(pr)]
mx = [max(len(s[tr]) for s in sn) for tr in (1, 2, 3, 4)]
check("sequencer, steps entered on GRID: T1 plays chords, T2 .. T4 single notes", mx == [3, 1, 1, 1], f"most notes at once {mx}")

# ---- ARP (ARP held: the track's arp on), a key held: the arp steps through its notes
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += go(t - 600, tr) + [(t - 500, "down 10"), (t + 200, "up 10"), (t + 400, f"keys {KEY_C:#x}")]
    ev += [(t + 500 + 100 * i, "probe") for i in range(20)] + [(t + 2600, "keys 0"), (t + 2700, "down 10"), (t + 3400, "up 10")]
    t += 4200
pr = run("arp", ev, t)
res = []
for tr in range(4):
    sn = [pr[m][0] for m in sorted(pr) if 1400 + 4200 * tr <= m < 1400 + 4200 * tr + 2600]
    res.append((sorted({n for s in sn for n in s[tr + 1]}), sorted({n for s in sn for u in (1, 2, 3, 4) if u != tr + 1 for n in s[u]})))
check("ARP: T1's arp steps through its chord's notes, T2 .. T4's play the one note; no arp plays on another track",
      res[0] == (chord(C4), []) and all(r == ([C4], []) for r in res[1:]), res)

# ---- GENERATE (FX held + the track's black key): chords on T1 only
ev, t = list(T1_ON), 1400
for tr in range(4):
    ev += go(t - 100, tr) + [(t, "down 2"), (t + 20, f"keys {1 << STEP[tr]:#x}"), (t + 40, "keys 0"), (t + 60, "up 2")]
    ev += [(t + 500 + 50 * i, "probe") for i in range(110)]
    ev += [(t + 6100, "down 2"), (t + 6120, f"keys {1 << STEP[tr]:#x}"), (t + 6140, "keys 0"), (t + 6160, "up 2")]
    t += 7000
pr = run("gen", ev, t)
res = []
for tr in range(4):
    sn = [pr[m][0] for m in sorted(pr) if 1400 + 7000 * tr <= m < 1400 + 7000 * tr + 6000]
    res.append((max(len(s[tr + 1]) for s in sn), max(len(s[u]) for s in sn for u in (1, 2, 3, 4) if u != tr + 1)))
check("GENERATE: T1 generates chords, T2 .. T4 single notes; none plays on another track",
      res[0][0] >= 3 and all(r[0] == 1 for r in res[1:]) and all(r[1] == 0 for r in res),
      f"(most notes at once, the other tracks') {res}")

# ---- T2 on too: MAJ7, OPEN (T2 selected: SEL held; SEL, SEL: CHORD page, KNOB 1 to 7, KNOB 2 to 1); T1 as it was
ev = list(T1_ON) + to(1300, 0, 1) + tog(1400) + ctype(2300, 7, 1) + to(2600, 1, 0)
t = 3000
for tr in range(4):                             # (3 s apart: a chord's release tails (2 s) hold voices too)
    ev += go(t - 100, tr) + [(t, f"keys {KEY_C:#x}"), (t + 200, "probe"), (t + 300, "keys 0")]
    t += 3000
pr = run("two", ev, t + 200)
ms = sorted(pr)
got = [pr[m][0] for m in ms]
want = [chord(C4), chord(C4, 7, 1)]
check("T1 and T2 on (T2 MAJ7 OPEN): T1 plays its own chord, T2 its own, T3 and T4 the one note",
      got[0][1] == want[0] and total(got[0]) == len(want[0]) and got[1][2] == want[1] and total(got[1]) == len(want[1]) and
      got[2][3] == [C4] and total(got[2]) == 1 and got[3][4] == [C4] and total(got[3]) == 1, got)
chd = pr[ms[-1]][1]
check("T1 and T2 on: each track's chord mode, CHORD TYPE and VOICING as set (T1 SCALE CLOSE, T2 MAJ7 OPEN; T3, T4 off)",
      chd[0] == (1, 0, 0) and chd[1] == (1, 7, 1) and chd[2][0] == 0 and chd[3][0] == 0, chd)

# T1 (MIN) + T2 (FIFTH) + T3, three keys held together, ALGORITHM turned between: each its own
ev = list(T1_ON) + to(1300, 0, 1) + tog(1400) + ctype(2300, 5, 0) + to(2600, 1, 0)
ev += [(3000, f"keys {KEY_C:#x}"), (3100, "enc 1 1"), (3200, "keys 0x180"), (3300, "enc 1 1"), (3400, "keys 0x580"), (3600, "probe"),
       (3700, "keys 0")]
pr = run("three", ev, 3900)
s = pr[3600][0]                                 # (the keys C4, C#4 (61), D#4 (63): T1, T2, T3)
check("three keys held, ALGORITHM turned between: T1 its minor chord, T2 its fifth (T2's own CHORD TYPE), T3 one note",
      s == {1: chord(60), 2: chord(61, 5), 3: [63], 4: []}, s)

# ---- a key held while ALGORITHM changes the track: its chord ends with its key; the new track's key plays one note
ev = list(T1_ON) + [(1500, f"keys {KEY_C:#x}"), (1700, "enc 1 1"), (1900, f"keys {KEY_C | KEY_E:#x}"), (2100, "probe"),
                    (2300, f"keys {KEY_E:#x}"), (2500, "probe"), (2700, "keys 0"), (3000, "probe")]
pr = run("held", ev, 3300)
check("a key held, ALGORITHM turned, a key on the new track: T1's chord stays and ends with its key, T2's note is single",
      pr[2100][0] == {1: chord(C4), 2: [E4], 3: [], 4: []} and pr[2500][0] == {1: [], 2: [E4], 3: [], 4: []} and
      total(pr[3000][0]) == 0, [pr[m][0] for m in (2100, 2500, 3000)])

# ---- a preset loaded on T2 (a choir: its CHORD TYPE and VOICING are set, chord mode stays off: a factory sound never
# switches it on): T1 as it was (on); T2, T3, T4 play the one note
ev = list(T1_ON) + to(1300, 0, 1) + [(1400, "enc 6 36"), (1500, "probe"), (1600, "enc 1 -1")]
t = 1800
for tr in range(4):
    ev += go(t - 100, tr) + [(t, f"keys {KEY_C:#x}"), (t + 200, "probe"), (t + 300, "keys 0")]
    t += 3000
pr = run("preset", ev, t + 200)
ms = sorted(pr)
chd = pr[ms[0]][1]
got = [pr[m][0] for m in ms[1:]]
check("a preset loaded on T2 (GREGORIAN: FIFTH OPEN): chord mode stays off, its CHORD TYPE and VOICING are set, T1 as it was (on), "
      "T3 and T4 untouched", chd == [(1, 0, 0), (0, 5, 1), (0, 0, 0), (0, 0, 0)], chd)
check("a preset loaded on T2: T1 plays its chord, T2, T3 and T4 the one note",
      got[0][1] == chord(C4) and got[1][2] == [C4] and got[2][3] == [C4] and got[3][4] == [C4] and
      all(total(g) == len(g[i + 1]) for i, g in enumerate(got)), got)

# ---- a project: slot A saved with T1 on (MIN7 SPREAD); T1 off, T2 on (SUS2); slot A recalled: as saved; NEW: all off
ev = list(T1_ON) + ctype(1300, 6, 2) + [(1700, "down 9"), (1720, "keys 0x80"), (1740, "keys 0"), (1760, "up 9"), (2000, "probe"),
                                        (2100, "enc 1 1")] + tog(2200) + ctype(3100, 3, 0) + to(3500, 1, 0) + tog(3600)
ev += [(4500, "probe"), (4600, "down 9"), (4620, "keys 0x1"), (4640, "keys 0"), (4660, "up 9"), (5000, "probe"),
       (5100, "btn 9"), (5150, "btn 9"), (5200, "enc 5 3"), (5300, "btn 1"), (5350, "btn 1"), (5700, "probe")]   # (NEW)
pr = run("project", ev, 6000)
a, b, c, d = pr[2000][1], pr[4500][1], pr[5000][1], pr[5700][1]
check("a project: slot A saved with T1 on (MIN7 SPREAD); then T1 off and T2 on (SUS2); slot A recalled: all as saved",
      a == [(1, 6, 2), (0, 0, 0), (0, 0, 0), (0, 0, 0)] and b[0][0] == 0 and b[1] == (1, 3, 0) and c == a, [a, b, c])
check("NEW: every track's chord mode, CHORD TYPE and VOICING as at power-on", all(x == (0, 0, 0) for x in d), d)

# ---- the screen: the SEQ grid's CHORD caption and SEL's CHORD page show the selected track's chord mode. The same note
# step on all four tracks, T1's chord on; a frame of each track's GRID and CHORD page, compared (after the boot screen)
ev, t = list(T1_ON), 6500
for tr in range(4):
    ev += [(t, "btn 11"), (t + 50, "enc 2 1"), (t + 150, "btn 8"), (t + 200, "enc 1 1")]
    t += 300
ev += [(t, "enc 1 -3")]
t += 200
shots = []
for tr in range(4):
    ev += [(t, "btn 11"), (t + 800, "btn 3"), (t + 850, "btn 3"), (t + 1600, "btn 8"), (t + 1700, "enc 1 1")]
    shots.append((t + 600, t + 1500))
    t += 2000
run("screen", ev, t + 300, fps=4)


def crop(ms, box):
    f = (OUT / f"screen-{ms // 250:05d}.ppm").read_bytes()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", f)
    w, px = int(m.group(1)), f[m.end():]
    x0, y0, x1, y1 = box
    return b"".join(px[3 * (y * w + x0):3 * (y * w + x1)] for y in range(y0, y1))


g = [crop(a, (146, 100, 240, 118)) for a, _ in shots]   # (the grid's right panel: "CHORD OFF" / "CHORD Cm")
p = [crop(b, (0, 120, 240, 150)) for _, b in shots]     # (the CHORD page's picture: the chord's name and its mode)
check("SEQ grid: the CHORD caption of T1 (on) is another than T2's, and T2, T3, T4 (off) show the same",
      g[0] != g[1] and g[1] == g[2] == g[3])
check("SEL's CHORD page: T1 (on) shows another picture than T2, and T2, T3, T4 (off) the same", p[0] != p[1] and p[1] == p[2] == p[3])

print(f"chord test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
