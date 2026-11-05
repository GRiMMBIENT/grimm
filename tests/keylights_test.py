#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 item 6 in host mode (tests/run_host.sh): the key lights after an octave change.
  - LATCH ON, C4 latched, OCT+, the lit C4 key struck again: it switches its own note OFF (and goes dark), it does not swap it for C5; the
    same with OCT-, and with the ARP's list; a key struck for a note latched by another key (the same pitch) still switches it off
  - a latched hand-played note whose pitch is not what its key plays at the current octave: its key BLINKS (full light, slow; the tick keeps
    the time, hal/fm1_input.h); a note at the current octave: steady; a MIDI note, a sequencer note: lit by pitch, as ever, never blinking
  - no fast flicker: with nothing pressed the lights are written no more (the blink is asked for once, the same every pass)
  python3 tests/keylights_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/keylights")
OUT.mkdir(parents=True, exist_ok=True)
SEL, OCT_DOWN, OCT_UP, ARP, PLAY, REC = 3, 0, 1, 10, 12, 13
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  key lights: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end):
    """-> {ms: (keys lit, keys blinking, light writes so far, notes in the list, T1's arp's notes)}"""
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True).stdout
    res = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            m = re.search(r" keyl ([0-9a-f]+) ([0-9a-f]+) (\d+) nl (\d+) an (\d+)", ln)
            res[int(ln.split()[1])] = (int(m.group(1), 16), int(m.group(2), 16), int(m.group(3)), int(m.group(4)), int(m.group(5)))
    return res


def strike(t, k):                               # key k struck and let go
    return [(t, f"keys {1 << k}"), (t + 100, "keys 0")]


def tap(t, b):                                  # a button tapped
    return [(t, f"btn {b}")]


LATCH = [(6000, f"down {SEL}"), (6050, f"btn {OCT_UP}"), (6150, f"up {SEL}")]   # LATCH ON (SEL held + OCT+)
C4 = 7                                          # the key: F3 + 7 = C4 (octave 0)

# ---- the bug: C4 latched, OCT+, the lit C4 key struck again: off, dark (not swapped for C5)
ev = LATCH + strike(6300, C4) + [(6600, "probe")] + tap(6700, OCT_UP) + [(7000, "probe"), (7300, "probe"), (7600, "probe")] + \
    strike(7700, C4) + [(8000, "probe")]
p = run("bug", ev, 8200)
check("C4 latched: its key lit, steady", p[6600][:2] == (1 << C4, 0) and p[6600][3] == 1, str(p[6600]))
check("OCT+: the key plays C5 now, the note is C4: the key BLINKS (not lit steady)", p[7000][:2] == (0, 1 << C4) and p[7000][3] == 1, str(p[7000]))
check("the lit C4 key struck again: its note is OFF (none in the list, not swapped for C5), the key dark",
      p[8000][:2] == (0, 0) and p[8000][3] == 0, str(p[8000]))
check("no flicker: with nothing pressed the blink is asked for once and the light writes stay still (7.0 .. 7.6 s)",
      p[7000][2] == p[7300][2] == p[7600][2] and p[7000][:2] == p[7300][:2] == p[7600][:2], str((p[7000], p[7300], p[7600])))

# ---- OCT- the same
ev = LATCH + strike(6300, C4) + tap(6600, OCT_DOWN) + [(6900, "probe")] + strike(7000, C4) + [(7400, "probe")]
p = run("down", ev, 7600)
check("OCT-: the latched key blinks; struck again: off, dark", p[6900][:2] == (0, 1 << C4) and p[7400][:2] == (0, 0) and p[7400][3] == 0, str((p[6900], p[7400])))

# ---- another key for the same pitch still switches the note off (the pitch lookup is kept): C5 latched by key 7 at octave +1; OCT-: key 19 plays C5
ev = LATCH + tap(6300, OCT_UP) + strike(6500, C4) + [(6800, "probe")] + tap(6900, OCT_DOWN) + [(7200, "probe")] + strike(7300, C4 + 12) + [(7700, "probe")]
p = run("pitch", ev, 7900)
check("C5 latched by key 7 at octave +1 (steady); OCT-: key 7 blinks; key 19 (plays C5 now) struck: that note is off",
      p[6800][:2] == (1 << C4, 0) and p[7200][:2] == (0, 1 << C4) and p[7700][3] == 0 and p[7700][:2] == (0, 0), str((p[6800], p[7200], p[7700])))

# ---- two notes, two octaves: the one at the current octave steady, the other blinking; back: swapped
ev = LATCH + strike(6300, C4) + tap(6600, OCT_UP) + strike(6800, 3) + [(7200, "probe")] + tap(7300, OCT_DOWN) + [(7700, "probe")]
p = run("two", ev, 7900)
check("C4 latched, OCT+, G#3 latched: G#3 (at this octave) steady, C4 blinks", p[7200][:2] == (1 << 3, 1 << C4) and p[7200][3] == 2, str(p[7200]))
check("OCT-: C4's key steady again, G#3's key blinks", p[7700][:2] == (1 << C4, 1 << 3), str(p[7700]))

# ---- MIDI: a latched MIDI note is lit by its pitch (a key at the octave, none out of it), steady, never blinking
ev = LATCH + [(6300, "midi 0x90 60 100"), (6400, "midi 0x80 60 0"), (6700, "probe")] + tap(6800, OCT_UP) + [(7100, "probe"), (7200, "midi 0x90 72 100"),
                                                                                                             (7300, "midi 0x80 72 0"), (7700, "probe")]
p = run("midi", ev, 7900)
check("MIDI note 60 latched: lit by its pitch (key 7), steady; OCT+: out of the keys' range, dark; note 72: lit (key 7), steady, none blinking",
      p[6700][:2] == (1 << C4, 0) and p[7100][:2] == (0, 0) and p[7100][3] == 1 and p[7700][:2] == (1 << C4, 0), str((p[6700], p[7100], p[7700])))

# ---- the sequencer: a step recorded with C4, playing: lit by pitch, never blinking
ev = tap(6000, REC) + strike(6100, C4) + tap(6400, REC) + tap(6500, PLAY) + [(t, "probe") for t in range(6600, 9000, 40)]
p = run("seq", ev, 9100)
lit = [v[0] for v in p.values()]
check("sequencer: the played note's key lit (by pitch) at some time, no key blinks at any time", any(l & (1 << C4) for l in lit) and not any(v[1] for v in p.values()),
      str(sorted(set((v[0], v[1]) for v in p.values()))))

# ---- the arp's list
ev = [(6000, f"down {ARP}"), (6700, f"up {ARP}"),   # ARP on (held)
      (6800, f"down {SEL}"), (6850, f"btn {OCT_UP}"), (6950, f"up {SEL}")] + strike(7100, C4) + [(7400, "probe")] + tap(7500, OCT_UP) + \
    [(7800, "probe")] + strike(7900, C4) + [(8300, "probe")]
p = run("arp", ev, 8500)
check("ARP on, LATCH ON, C4 latched: its key lit steady (one in the arp's list); OCT+: blinks; struck again: out of the list, dark",
      p[7400][:2] == (1 << C4, 0) and p[7400][4] == 1 and p[7800][:2] == (0, 1 << C4) and p[8300][:2] == (0, 0) and p[8300][4] == 0,
      str((p[7400], p[7800], p[8300])))

sys.exit(1 if bad else 0)
