#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2 rc9's (and rc10's) fixes (build/bugs/AUDIT-rc8.md) in host mode, one check each:
  A-01 NEW: the tracks' mix as at power-on too (VOLUME, CUTOFF, FX, sends, mutes)
  A-02 HOME: a track whose filter is closed says so on its slab ("T1 CUT", in the accent); A-17: CUTOFF 0 is closed
  A-03 HOME: a track at FX 0 with a send up: "T1 DRY"
  A-05 an LFO on VOLUME: a track at VOLUME 0 stays silent (STATIC PRAYER); at 100 as before (tapeoff_test: bit for bit)
  A-06 a sound with RESONANCE past its range (an editor's, a broken one): loaded within it, the filter stable
  A-08 the overload guard: a release goes first, a latched note last; said in the header
  A-10 DELAY ringing for ever (FEEDBACK at the top): a SYSTEM setting is still saved, 10 s after the last voice
(A-07: tests/presets_test.py, a broken project; A-18: tests/uac_ring_test.c, the USB ring.)
  python3 tests/rc9_test.py [build/host/grimm]"""
import array
import re
import os
import subprocess
import sys
import wave
import zlib
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/rc9")
OUT.mkdir(parents=True, exist_ok=True)
sr = 44100
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  rc9: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, env=None):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda q: q[0])) + f"{end} end\n")
    r = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                       env=dict(os.environ, **(env or {})))
    w = wave.open(str(OUT / f"{name}.wav"))
    return array.array("h", w.readframes(w.getnframes())), [ln for ln in r.stdout.splitlines() if ln.startswith("probe ")]


def rms(a, t0, t1):
    s = a[2 * sr * t0 // 1000:2 * sr * t1 // 1000]
    return (sum(v * v for v in s) / max(1, len(s))) ** .5


def field(p, name):
    return p.split(f" {name} ")[1].split()[0]


HOME = [(150, "btn 6"), (200, "btn 8")]         # (EDIT, then HOME: HOME)

# A-01: T1 VOLUME 0, CUTOFF 0, FX 0, its DELAY send up; T2 muted (GLO + the 2nd white key); then NEW (SAVE x2:
# PROJECTS, ACTION NEW, OCT+): all as at power-on, no mute
ev = HOME + [(300, "enc 2 -127"), (350, "enc 3 -127"), (400, "enc 5 -100"), (450, "btn 8"), (500, "enc 0 1"),
             (550, "enc 2 90"), (600, "down 7"), (650, "keys 0x4"), (700, "keys 0"), (750, "up 7"), (800, "probe"),
             (900, "btn 9"), (950, "btn 9"), (1000, "enc 5 3"), (1100, "btn 1"), (1150, "btn 1"), (1600, "probe")]
_, p = run("new", ev, 1700)
before, after = p[0], p[1]
check("NEW: the tracks' VOLUME, CUTOFF, FX, sends, mutes as at power-on (A-01)",
      field(before, "mix").startswith("0,0,0,0,90") and field(before, "mute") == "2" and
      field(after, "mix").split("/")[0] == "100,127,0,100,0,100,0,0,0,64" and field(after, "mute") == "0",
      f"before {field(before, 'mix')[:26]} mute {field(before, 'mute')}; after {field(after, 'mix')[:30]} "
      f"mute {field(after, 'mute')}")

# A-02, A-17, A-03: HOME's slabs: T1's CUTOFF 0 -> "T1 CUT"; T2 at FX 0 (its REVERB send up, as at power-on) -> "T2 DRY";
# T3 as it is; T4's CUTOFF 30 (not closed) as it is
ev = HOME + [(300, "enc 3 -127"), (400, "enc 1 1"), (450, "enc 5 -100"), (500, "enc 1 2"), (550, "enc 3 -97"),
             (700, "enc 1 -3"), (6500, "probe")]   # (after the boot screen: HOME drawn)
_, p = run("slabs", ev, 6600)
labs = p[0].split(" slabs ")[1].split(" mute ")[0].split()
check("HOME: T1's filter closed (CUTOFF 0) warns CUT, T2 at FX 0 warns DRY, their labels as ever; T3, T4 (CUTOFF "
      "30) no warning (A-02, A-03, A-17)", labs[:4] == ["T1_VA:1", "T2_VA:2", "T3_VA:0", "T4_VA:0"], " ".join(labs[:4]))

# (rc10) the warning beside the label, never in its place: T1 FX 0 (its REVERB send up, as at power-on) -> DRY under
# "T1 VA"; FX up again -> gone; the label always there. The screen read too: the label's pixels light, the warning's red
ev = [(6000, "btn 6"), (6050, "btn 8"), (6100, "probe"), (6200, "enc 5 -100"), (6800, "probe"), (6900, "enc 5 1"),
      (7500, "probe"), (7600, "enc 5 99"), (8200, "probe")]
_, p = run("drylab", ev, 8300, {})
st = [x.split(" slabs ")[1].split()[0] for x in p]
sub = subprocess.run([EXE, str(OUT / "drylab_f"), str(OUT / "drylab.txt"), "4"], capture_output=True, check=True)


def slab_px(ms):                                # T1's slab: the label's rows, the warning's: light, red pixels
    f = open(OUT / f"drylab_f-{ms // 250:05d}.ppm", "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", f)
    w, px = int(m.group(1)), f[m.end():]
    def count(y0, y1, red):
        n = 0
        for y in range(y0, y1):
            for x in range(3, 59):
                r, g, b = px[3 * (y * w + x):3 * (y * w + x) + 3]
                n += (r > 150 and g < 110) if red else (r > 150 and g > 150 and b > 150)
        return n
    return count(170, 178, False), count(180, 188, True)


shots = [slab_px(t) for t in (6000, 6750, 7500, 8250)]
check("HOME's warning beside the label (rc10): FX 0 with a send up, DRY under 'T1 VA'; FX up (1, 100), gone; the label "
      "always there, the warning red only while so",
      st == ["T1_VA:0", "T1_VA:2", "T1_VA:0", "T1_VA:0"] and all(lab > 20 for lab, _ in shots) and
      shots[1][1] > 20 and shots[0][1] == shots[2][1] == shots[3][1] == 0,
      f"probe {st}; label/warning pixels {shots}")

# A-05: T3 at VOLUME 0, STATIC PRAYER (an LFO on VOLUME): silent; TOMB HUM (none) silent too
for pre, nm in ((5, "STATIC PRAYER"), (4, "TOMB HUM")):
    ev = [(300, "enc 1 2"), (400, f"enc 6 {pre}")] + HOME + [(500, "enc 2 -127"), (1000, "midi 0x92 60 100"),
                                                            (1000, "midi 0x92 64 100"), (4000, "midi 0x82 60 0"),
                                                            (4000, "midi 0x82 64 0")]
    a, _ = run(f"lfo{pre}", ev, 4200)
    check(f"VOLUME 0, {nm} (an LFO on VOLUME: {'yes' if pre == 5 else 'no'}): silent (A-05)", rms(a, 1500, 3900) == 0,
          f"rms {rms(a, 1500, 3900):.0f}")

# A-06: a user sound (VA, RESONANCE 200; its knob 0..127) loaded on T1 (SOUNDS: LOAD, OCT+): RESONANCE 127, the
# output steady (no jump past what a loud saw makes)
img = bytearray(b"\xff" * (6 * 4096))
r = bytearray(64)
r[0], r[1], r[2], r[3] = 0x54, 1, 1, 0
r[4:8], r[8:12] = bytes([0, 10, 90, 200]), bytes([0, 64, 127, 20])
r[60:64] = zlib.crc32(bytes(r[:60])).to_bytes(4, "little")
img[0:64] = r
(OUT / "badsound.bin").write_bytes(bytes(img))
ev = [(300, "btn 9"), (400, "btn 1"), (1000, "midi 0x90 48 120"), (1000, "midi 0x90 60 120"), (3000, "midi 0x80 48 0"),
      (3000, "midi 0x80 60 0"), (3100, "probe")]
a, p = run("badsound", ev, 4000, {"GRIMM_SAVE_IN": str(OUT / "badsound.bin")})
m = [(a[2 * i] + a[2 * i + 1]) // 2 for i in range(sr * 1200 // 1000, sr * 2900 // 1000)]
jump = max(abs(m[i + 2] - 2 * m[i + 1] + m[i]) for i in range(len(m) - 2))
pk = max(abs(v) for v in m)
check("a sound with RESONANCE 200 (its knob 0..127): loaded as 127, the filter stable (A-06)",
      field(p[-1], "p1") == "0,10,90,127" and jump < 8000 and pk < 32000,
      f"p1 {field(p[-1], 'p1')}, largest jump {jump}, peak {pk}")

# A-08: T1 a chord latched (keys; LATCH ON: SEL + OCT+), T2 a note let go (its RELEASE long), then an overload: the
# first voice let go is T2's release, T1's notes on; the header says so
ev = [(300, "down 3"), (320, "btn 1"), (340, "up 3"), (500, "keys 0x91"), (550, "keys 0"), (600, "enc 1 1"),
      (650, "btn 4"), (700, "enc 5 127"), (800, "btn 8"),     # (T2: ENV, RELEASE at the top)
      (1000, "midi 0x91 72 100"), (1500, "midi 0x81 72 0"), (1700, "probe"), (1810, "probe"), (2300, "probe")]
_, p = run("shed", ev, 2400, {"GRIMM_OVERLOAD": "1750,1756"})   # (one block over: one voice let go)
v0, v1 = p[0].split(" voices ")[1].split(), p[1].split(" voices ")[1].split()


def on(vs, tr):                                 # (track tr's voices sounding, not fading)
    return sum(1 for x in vs if x.split("/")[1] == str(tr) and x.split("/")[0] in ("1", "2", "3"))


check("the overload guard: T2's releasing note let go first, T1's latched chord kept; the header says so (A-08)",
      on(v0, 2) == 1 and on(v1, 2) == 0 and on(v1, 1) == on(v0, 1) == 3 and field(p[1], "shed") == "1" and
      "CPU_FULL" in p[1].split(" hd ")[1].split()[0],
      f"before T1 {on(v0, 1)} T2 {on(v0, 2)}; after T1 {on(v1, 1)} T2 {on(v1, 2)}; shed {field(p[1], 'shed')}; "
      f"hd {p[1].split(' hd ')[1].split()[0]}")

# A-10: DELAY's FEEDBACK at the top (it rings for ever), a chord; then SYSTEM's COLOUR changed: saved 10 s after the
# last voice ended (before rc9: never)
ev = [(200 + 40 * i, "btn 2") for i in range(3)] + [(400, "enc 3 127"), (450, "enc 5 127")]   # (FX x3: DELAY)
ev += [(600, "btn 6"), (650, "btn 8"), (700, "btn 8"), (750, "enc 0 1"), (800, "enc 2 127"),   # (MIXER 1: T1's DLY)
       (850, "btn 6"), (900, "btn 8"), (1000, "midi 0x90 60 120"), (1500, "midi 0x80 60 0"),
                  (3000, "down 8"), (3700, "up 8"), (3800, "enc 2 1"), (6000, "probe"), (24000, "probe")]
a, p = run("ring", ev, 24500)
check("DELAY ringing for ever: a SYSTEM setting saved anyway, 10 s after the last voice (A-10)",
      field(p[0], "setd") == "1" and field(p[1], "setd") == "0" and rms(a, 22000, 24000) > 50,
      f"setd at 6 s {field(p[0], 'setd')}, at 24 s {field(p[1], 'setd')}; the delay at 22..24 s: rms {rms(a, 22000, 24000):.0f}")
print(f"rc9 test: {'passed' if not bad else 'FAILED'}")
sys.exit(1 if bad else 0)
