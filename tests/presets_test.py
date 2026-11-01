#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2 (rc9): PRESETS turned through every factory sound while it plays, as the rc8 no-sound report had it. The
effects as that FM-1 had them (TAPE WOW 127 SAT 127 TONE 99, GRAIN all up, DELAY, SPACE), every track's DLY, REV, GRAIN
and TAPE tile at the top; then on T1 the 44 sounds one by one, a chord on each (MIDI; a LATCH sound's chord let go
again), HOME's FX (KNOB 4) turned off and on again between them, the FX pages' MIX off and on now and then; then the
same on T2. Checks: every sound sounds; no track's LEVEL, CUTOFF, RES, FX, sends, TAPE or PAN moved but by the knob
that turned it (the probe after each); no mute, no solo; at the end T1 and T2 still sound, and their sends still work
(a chord's reverb tail with FX up, none with FX at 0).
  python3 tests/presets_test.py [build/host/grimm]"""
import array
import subprocess
import sys
import wave
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/presets")
OUT.mkdir(parents=True, exist_ok=True)
NPRE = 44
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  presets: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


ev, t = [], 300


def at(e, dt=40):
    global t
    ev.append((t, e))
    t += dt


FXV = [(127, 127, 99, 0), (127, 127, 127, 127), (88, 64, 50, 127), (126, 49, 127, 19)]   # TAPE, GRAIN, DELAY, SPACE
for vals in FXV:                                # (FX tapped: the next page; each knob from 0 to the FM-1's)
    at("btn 2")
    for k, v in enumerate(vals):
        at(f"enc {2 + k} -127"), at(f"enc {2 + k} {v}")
at("btn 6"), at("btn 8"), at("btn 8")           # (EDIT, HOME; MIXER 1)
for tr in range(4):                             # each track's DLY, REV, GRAIN, TAPE tiles to the top
    for tile in (1, 2, 4, 3):
        at("enc 0 -5"), at(f"enc 0 {tile}"), at("enc 2 127")
    at("enc 1 1")
at("enc 1 -4"), at("btn 6"), at("btn 8")        # (HOME, T1)
WANT = "100,127,0,100,127,127,127,127,0,64"     # LEVEL, CUTOFF, RES, FX, DLY, REV, GRN, TAPE, DRIVE, PAN: each track
t = 6500
steps = []                                      # (the probe's index, track, preset, the chord's start, end)


def chord(track, a, b, rel=True):
    for n in (48, 55, 60, 64):
        ev.append((a, f"midi {0x90 | track:#x} {n} 100"))
    for n in (48, 55, 60, 64):
        ev.append((b, f"midi {0x80 | track:#x} {n} 0"))
    if rel:                                     # (a LATCH sound's chord: pressed again, let go; else a short blip)
        for n in (48, 55, 60, 64):
            ev.append((b + 30, f"midi {0x90 | track:#x} {n} 100")), ev.append((b + 60, f"midi {0x80 | track:#x} {n} 0"))


for track in (0, 1):
    if track:
        at("enc 1 1", 200)
    for i in range(NPRE):
        at("enc 6 1", 250)                      # PRESETS: the next sound
        chord(track, t, t + 900)
        steps.append((track, i, t + 250, t + 900))
        at("enc 5 -100", 250), at("enc 5 100", 400)   # KNOB 4: FX off, on again (as it was: 100)
        if i % 4 == 3:                          # the FX pages' MIX now and then: GRAIN, DELAY off and on (to 127)
            at("btn 2"), at("btn 2"), at("enc 5 -127"), at("enc 5 127"), at("btn 2"), at("enc 5 -127"), at("enc 5 127")
            at("btn 6"), at("btn 8", 100)       # (back HOME)
        ev.append((t, "probe"))
        t += 400
# the end: T1, T2 still sound; their sends work (a chord, its tail with FX up; again with FX at 0)
fin = []
for track in (0, 1):
    at(f"enc 1 {-1 if track == 0 else 1}", 300)
    chord(track, t, t + 1200)
    fin.append((track, "up", t, t + 1200))
    t += 7000
    at("enc 5 -100", 300)
    chord(track, t, t + 1200)
    fin.append((track, "zero", t, t + 1200))
    t += 7000
    at("enc 5 100", 300)
END = t + 500
path = OUT / "presets.txt"
path.write_text("".join(f"{a} {e}\n" for a, e in sorted(ev, key=lambda q: q[0])) + f"{END} end\n")
r = subprocess.run([EXE, str(OUT / "presets"), str(path)], capture_output=True, text=True, check=True)
w = wave.open(str(OUT / "presets.wav"))
x = array.array("h", w.readframes(w.getnframes()))
probes = [ln for ln in r.stdout.splitlines() if ln.startswith("probe ")]


def rms(a, b):                                  # (ms; L and R)
    s = x[2 * 44100 * a // 1000:2 * 44100 * b // 1000]
    return (sum(v * v for v in s) / max(1, len(s))) ** .5


quiet, moved, named, chords_on = [], [], set(), []
for k, (track, i, a, b) in enumerate(steps):
    p = probes[k]
    if p.split(" chd ")[1].split(" voices")[0].split()[track].split("/")[0] != "0":   # (rc12: a factory sound never sets chord mode)
        chords_on.append(f"T{track + 1} {p.split(' sname ')[1].split()[0]}")
    nm = p.split(" sname ")[1].split()[0]
    named.add((track, nm))
    if rms(a, b) < 100:
        quiet.append(f"T{track + 1} {nm} {rms(a, b):.0f}")
    mix = p.split(" mix ")[1].split()[0].split("/")
    ms = p.split(" mute ")[1].split()
    if any(m != WANT for m in mix) or ms[0] != "0" or ms[2] != "0":
        moved.append(f"T{track + 1} {nm}: {'/'.join(mix)} mute {ms[0]} solo {ms[2]}")
check(f"PRESETS through all {NPRE} sounds on T1, then T2, a chord each: every one sounds", not quiet and
      len(named) == 2 * NPRE, "; ".join(quiet[:6]) or f"{len(named)} sounds")
check(f"no factory sound switches chord mode on when it is loaded (all {NPRE}, on T1 and T2)", not chords_on and
      len(named) == 2 * NPRE, "; ".join(chords_on[:6]))
check("no track's LEVEL, CUTOFF, RES, FX, sends, TAPE or PAN moved but by its knob; no mute, no solo",
      not moved, "; ".join(moved[:3]))
for track in (0, 1):
    up = [f for f in fin if f[0] == track and f[1] == "up"][0]
    z = [f for f in fin if f[0] == track and f[1] == "zero"][0]
    tail_up, tail_z = rms(up[3] + 3500, up[3] + 5500), rms(z[3] + 3500, z[3] + 5500)   # (past the voices' release)
    check(f"after it all, T{track + 1} sounds, and its sends work (a reverb tail with FX up, none with FX 0)",
          rms(up[2] + 200, up[3]) > 300 and tail_up > 4 * max(tail_z, 1.0),
          f"chord {rms(up[2] + 200, up[3]):.0f}, tail FX up {tail_up:.0f}, FX 0 {tail_z:.0f}")

# (rc9) a broken save: the working project with T1's WAVE (VA: 0..2) at 127 and the effects' 16 knobs at 255, its CRC
# made good (a save no GR!MM wrote): loads with each value at its knob's end, and sounds; the mix as saved
import os, zlib                                 # noqa: E402
env = dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "saves.bin"))
(OUT / "save.txt").write_text("300 enc 2 -10\n9000 end\n")   # (T1's VOLUME 100 -> 90: an autosave)
subprocess.run([EXE, str(OUT / "save"), str(OUT / "save.txt")], capture_output=True, check=True, env=env)
img = bytearray((OUT / "saves.bin").read_bytes())
WORK_PK = 2576
sec = max((1, 2), key=lambda s: int.from_bytes(img[4096 * s + 4:4096 * s + 8], "little")
          if img[4096 * s:4096 * s + 4] == b"GRMK" else -1)   # (the WORK pair's newer copy)
o = 4096 * sec
img[o + 12 + 1] = 127                           # T1: VA's WAVE
img[o + 12 + 4 * 52:o + 12 + 4 * 52 + 16] = b"\xff" * 16   # TAPE, GRAIN, DELAY, SPACE's knobs
img[o + 12 + WORK_PK:o + 16 + WORK_PK] = zlib.crc32(bytes(img[o:o + 12 + WORK_PK])).to_bytes(4, "little")
(OUT / "broken.bin").write_bytes(bytes(img))
(OUT / "load.txt").write_text("1000 midi 0x90 60 100\n2500 probe\n2600 end\n")
r = subprocess.run([EXE, str(OUT / "load"), str(OUT / "load.txt")], capture_output=True, text=True, check=True,
                   env=dict(os.environ, GRIMM_SAVE_IN=str(OUT / "broken.bin")))
p = [ln for ln in r.stdout.splitlines() if ln.startswith("probe ")][-1]
w = wave.open(str(OUT / "load.wav"))
y = array.array("h", w.readframes(w.getnframes()))
s = y[2 * 44100 * 1300 // 1000:2 * 44100 * 2400 // 1000]
lv = (sum(v * v for v in s) / len(s)) ** .5
check("a broken save (T1's WAVE past its range, the effects' knobs at 255) loads clamped (WAVE 2, knobs 127), the "
      "mix as saved (T1 VOLUME 90), and sounds", p.split(" p1 ")[1].split()[0].startswith("2,") and
      p.split(" fxk ")[1].split()[0] == ",".join(["127"] * 16) and p.split(" mix ")[1].startswith("90,") and lv > 300,
      f"p1 {p.split(' p1 ')[1].split()[0]} fxk {p.split(' fxk ')[1].split()[0]} mix {p.split(' mix ')[1][:12]} rms {lv:.0f}")
print(f"presets test: {'passed' if not bad else 'FAILED'}")
sys.exit(1 if bad else 0)
