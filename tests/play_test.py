#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's playing additions in host mode (tests/host.c):
  - VELOCITY (SYSTEM 3, KNOB 2): ON, a MIDI note's volume from how hard; OFF (as before): the same at any velocity;
    the keys at full either way; the setting loaded from the settings (byte 169's bit 1)
    python3 tests/play_test.py build/host/grimm"""
import array
import math
import os
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/playtest")
OUT.mkdir(parents=True, exist_ok=True)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  play: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, env=None):
    (OUT / f"{name}.txt").write_text("".join(f"{t} {e}\n" for t, e in sorted(ev, key=lambda x: x[0])) + f"{end} end\n")
    out = subprocess.run([BIN, str(OUT / name), str(OUT / f"{name}.txt")], capture_output=True, text=True, env=env).stdout
    raw = (OUT / f"{name}.wav").read_bytes()
    return out, raw, array.array("h", raw[44:])


def rms(a, t0, t1):                             # the left channel, t0 .. t1 ms
    x = a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000:2]
    return math.sqrt(sum(v * v for v in x) / max(len(x), 1))


SYS3 = [(500, "down 8"), (1300, "up 8"), (1400, "btn 8"), (1500, "btn 8")]   # (HOME held: SYSTEM; HOME: 2, 3)
LEAVE = [(1700, "btn 6"), (1800, "btn 8")]     # (EDIT, HOME: back on HOME)
VEL_ON = SYS3 + [(1600, "enc 3 1")] + LEAVE


def midi_note(v):                               # T1 (channel 1), C4 at velocity v, 3 .. 4 s
    return [(3000, f"midi 0x90 60 {v}"), (4000, "midi 0x80 60 0")]


# VELOCITY
_, _, hard = run("vel_on127", VEL_ON + midi_note(127), 5000)
_, _, soft = run("vel_on40", VEL_ON + midi_note(40), 5000)
r_h, r_s = rms(hard, 3200, 3900), rms(soft, 3200, 3900)
check("VELOCITY ON: a soft MIDI note (40) much quieter than a hard one (127)", r_s < 0.25 * r_h and r_s > 0.02 * r_h,
      f"rms {r_s:.0f} / {r_h:.0f}")
_, b127, _ = run("vel_off127", SYS3 + LEAVE + midi_note(127), 5000)
_, b40, _ = run("vel_off40", SYS3 + LEAVE + midi_note(40), 5000)
check("VELOCITY OFF (as before): a MIDI note the same at any velocity, to the byte", b127 == b40)
KEY = [(3000, "keys 0x80"), (4000, "keys 0")]
_, k_on, _ = run("key_on", VEL_ON + KEY, 5000)
_, k_off, _ = run("key_off", SYS3 + LEAVE + KEY, 5000)
check("the FM-1's keys at full volume, VELOCITY ON or OFF (to the byte)", k_on == k_off)
st = bytearray(170)
st[167], st[169] = 3, 2                         # (version 3; LATCH FROM SOUND, VELOCITY ON)
(OUT / "vel_set.bin").write_bytes(bytes(st))
out, _, _ = run("vel_load", [(1000, "probe")], 1200, dict(os.environ, GRIMM_SETTINGS_IN=str(OUT / "vel_set.bin")))
check("VELOCITY loaded from the settings (byte 169, bit 1), LATCH beside it as it was",
      " vel 1 " in out and " latch " in out and "new 0 " in out, out.split(" new ")[1][:6] if " new " in out else out[:80])

# PHASE (WRAITH, preset 25, on T1), its LINE 2 page (EDIT: SOUND, SOURCE, LINE 2): off, the sound as before (to the
# byte, against the build given as the second argument, if one); RING: another sound, in range; saved, loaded; 1.0: off
OLD = sys.argv[2] if len(sys.argv) > 2 else None
PD = [(300, "enc 6 25"), (400, "btn 6"), (450, "btn 6"), (500, "btn 6")]
PLAY = [(3000, "keys 0x880"), (5000, "keys 0")]


def pd_run(name, ev, end=6500, env=None, binary=None):
    global BIN
    keep, BIN = BIN, binary or BIN
    try:
        return run(name, ev, end, env)
    finally:
        BIN = keep


out, base_b, base = pd_run("pd_base", PD + [(600, "probe")] + PLAY)
check("PHASE: the LINE 2 page after SOURCE on a PHASE track", " page 28 " in out, out.split(" ui page ")[1][:8])
if OLD:
    _, old_b, _ = pd_run("pd_old", PD + PLAY, binary=OLD)
    check("PHASE with LINE 2 as it starts: the sound as before, to the byte", old_b == base_b)


def pd_on(name, turn, extra=()):
    out, raw, a = pd_run(name, PD + [(600, turn)] + list(extra) + [(700, "probe")] + PLAY)
    pk = max(abs(x) for x in a)
    return out, raw, a, pk


out, r_b, r, pk = pd_on("pd_ring", "enc 4 1")
check("RING on: another sound, in range", " pd1 1" in out and r_b != base_b and pk < 32767 and rms(r, 3200, 4800) > 100,
      f"peak {pk}")
SV = PD + [(600, "enc 4 1"), (1700, "keys 0")]
o1, _, _ = run("pd_save", SV, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "pdsaves.bin")))
o2, _, _ = run("pd_load", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "pdsaves.bin")))
o3, _, _ = run("pd_v10", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("RING saved with the project, loaded; a GR!MM 1.0 project: off", " pd1 1" in o2 and " pd1 0" in o3,
      f"{o2.split(' pd1 ')[1][:3] if ' pd1 ' in o2 else '?'} {o3.split(' pd1 ')[1][:3] if ' pd1 ' in o3 else '?'}")
print("play test: passed" if not bad else "play test: FAILED")
sys.exit(1 if bad else 0)
