#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2.0 rc11's fix for the flicker and the hole in the USB recording (host mode, tests/host.c). A flash step (an
autosave, the settings' save) stops the key scan, the LEDs and the USB audio for ~50 ms: while a computer reads the USB
audio (host mode: GRIMM_USB_REC=FROM,TO) they wait for the hands to rest a minute, not 2.5 s (the settings' 2 s). Not reading:
as before. The reading ends: they go at once. Nothing is lost: it is saved, later.
    python3 tests/rc11_test.py build/host/grimm"""
import os
import re
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/rc11test")
OUT.mkdir(parents=True, exist_ok=True)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  rc11: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, rec=None):
    """the project's flash steps and the settings' saves: ([ms of each sector erased], [ms of each settings save])"""
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda q: q[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=dict(os.environ, **({"GRIMM_USB_REC": rec} if rec else {}))).stdout
    erased = [int(m.group(1)) for m in re.finditer(r"^\s*(\d+) ms  flash sector \d+ erased", out, re.M)]
    sets = [int(m.group(1)) for m in re.finditer(r"^\s*(\d+) ms  settings saved", out, re.M)]
    return erased, sets


# something changed at 1.0 s (HOME's knobs: a track's VOLUME), the octave at 1.2 s (a setting): the last touch 1.2 s
EV = [(900, "btn 8"), (1000, "enc 2 -9"), (1200, "btn 1")]

e, s = run("idle", EV, 20000)
check("no computer reading the USB audio: the project saves 2.5 s after the last touch, the settings 2 s after theirs",
      e and 3600 <= e[0] <= 4500 and s and 3100 <= s[0] <= 3600, f"project {e[:3]}, settings {s[:3]}")

e, s = run("rec", EV, 90000, rec="1,200000")
check("a computer reading the USB audio: neither saves for a minute after the last touch (no flash step, no flicker, no "
      "hole in the recording)", not [x for x in e + s if x < 60000], f"project {e[:3]}, settings {s[:3]}")
check("... and after the minute they are saved: nothing is lost", e and 61000 <= e[0] <= 63000 and s and 61000 <= s[0] <= 63000,
      f"project {e[:3]}, settings {s[:3]}")

e, s = run("rec_end", EV, 40000, rec="1,20000")
check("the reading ends (the DAW stopped): they are saved at once, not a minute on",
      e and 20000 <= e[0] <= 21500 and s and 20000 <= s[0] <= 21500, f"project {e[:3]}, settings {s[:3]}")

e, s = run("rec_touch", EV + [(30000, "enc 2 2"), (60000, "enc 2 2")], 100000, rec="1,200000")
check("reading, the hands busy again at 30 s and 60 s: the project's minute counts from the last touch (not yet at 100 s); "
      "the settings', from their change (61 s)", not e and s and 61000 <= s[0] <= 63000, f"project {e[:3]}, settings {s[:3]}")

print(f"rc11 test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
