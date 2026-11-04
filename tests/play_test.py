#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's playing additions in host mode (firmware/src/host.c):
  - VELOCITY (SYSTEM 3, KNOB 2): ON, a MIDI note's volume from how hard; OFF (as before): the same at any velocity;
    the keys at full either way; the setting loaded from the settings (byte 169's bit 1)
  - PHASE's LINE 2 page (RING, NOISE) and GLIDE (SOURCE, KNOB 4): off, as before; on, at work; saved, loaded; 1.0: off
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
check("RING on: another sound, in range", " pd1 1,0" in out and r_b != base_b and pk < 32767 and rms(r, 3200, 4800) > 100,
      f"peak {pk}")
out, n_b, n, pk = pd_on("pd_noise", "enc 5 1")
check("NOISE on: another sound, in range", " pd1 0,1" in out and n_b != base_b and pk < 32767 and
      rms(n, 3200, 4800) > 100, f"peak {pk}")
out, w_b, w, pk = pd_on("pd_wav", "enc 2 4")
check("LINE 2's WAVE (DSIN): line 2 on, another sound, in range", " pd1 0,0,4,0" in out and w_b != base_b and pk < 32767 and
      rms(w, 3200, 4800) > 100, f"peak {pk}")
out, d_b, d, pk = pd_on("pd_dcw", "enc 3 100")
check("LINE 2's DCW (100): line 2 on, another sound, in range", " pd1 0,0,0,100" in out and d_b != base_b and d_b != w_b and
      pk < 32767 and rms(d, 3200, 4800) > 100, f"peak {pk}")
SV = PD + [(600, "enc 4 1"), (650, "enc 5 1"), (660, "enc 2 6"), (670, "enc 3 33"), (1700, "keys 0")]

# PHASE ENV (EDIT: SOUND, SOURCE, LINE 2, PHASE ENV): off, as before (above); DCW ENV, PITCH ENV up and down: another
# sound, in range, the start unlike the end (the envelopes decayed); saved, loaded; 1.0: off
PE = PD + [(550, "btn 6")]


def pe_on(name, turns):
    out, raw, a = pd_run(name, PE + [(600 + 10 * i, t) for i, t in enumerate(turns)] + [(700, "probe")] + PLAY)
    return out, raw, a, max(abs(x) for x in a)


out, _, _ = pd_run("pe_page", PE + [(600, "probe")], 700)
check("PHASE: the PHASE ENV page after LINE 2", " page 29 " in out, out.split(" ui page ")[1][:8] if " ui page " in out else "?")
out, e_b, e, pk = pe_on("pe_dcw", ["enc 2 127", "enc 3 60"])
check("DCW ENV 100 %: another sound, in range", " pde1 127,60,0,0" in out and e_b != base_b and pk < 32767 and
      rms(e, 3200, 4800) > 100, f"peak {pk}")
out, u_b, u, pk = pe_on("pe_up", ["enc 4 24", "enc 5 90"])
_, dn_b, dn, pk2 = pe_on("pe_down", ["enc 4 -24", "enc 5 90"])
check("PITCH ENV +24 / -24 ST: other sounds, unlike each other, in range", " pde1 0,0,24,90" in out and u_b != base_b and
      dn_b != base_b and u_b != dn_b and max(pk, pk2) < 32767, f"peak {pk} {pk2}")


def zc(a, t0, t1):                              # zero crossings (left), t0 .. t1 ms: the pitch, roughly
    x = a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000:2]
    return sum((x[i] < 0) != (x[i + 1] < 0) for i in range(len(x) - 1))


z0, zu, zd = zc(base, 3020, 3120), zc(u, 3020, 3120), zc(dn, 3020, 3120)
l0, lu = zc(base, 4300, 4900), zc(u, 4300, 4900)
check("PITCH ENV: up at the start (+24: higher, -24: lower), back on the note after its DECAY",
      zu > 1.5 * z0 and zd < 0.8 * z0 and abs(lu - l0) <= 0.05 * l0 + 2, f"start {zd} {z0} {zu}, later {l0} {lu}")
SV += [(1000, "btn 6"), (1100, "enc 2 90"), (1110, "enc 3 20"), (1120, "enc 4 -12"), (1130, "enc 5 70")]
o1, _, _ = run("pd_save", SV, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "pdsaves.bin")))
o2, _, _ = run("pd_load", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "pdsaves.bin")))
o3, _, _ = run("pd_v10", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("LINE 2 (RING, NOISE, WAVE, DCW) and PHASE ENV saved with the project, loaded; a GR!MM 1.0 project: off",
      " pd1 1,1,6,33 " in o2 and " pd1 0,0,0,0 " in o3 and " pde1 90,20,-12,70" in o2 and " pde1 0,0,0,0" in o3,
      f"{o2.split(' pd1 ')[1][:9] if ' pd1 ' in o2 else '?'} {o3.split(' pd1 ')[1][:9] if ' pd1 ' in o3 else '?'}")

# GLIDE (SOURCE, KNOB 4; EDIT: SOUND, SOURCE): off (as before): a second note straight there (to the byte, against the build
# given, if one); on: the second note starts at the first's pitch and gets there; saved, loaded; 1.0: off
GL = [(300, "btn 6"), (400, "btn 6")]
TWO = [(3000, "keys 0x80"), (3500, "keys 0x800"), (3520, "probe"), (5000, "probe"), (5200, "keys 0")]
og, g0_b, _ = run("gl_off", GL + TWO, 5500)
if OLD:
    _, old_b, _ = pd_run("gl_old", GL + TWO, 5500, binary=OLD)
    check("GLIDE OFF: two notes as before, to the byte", old_b == g0_b)
check("GLIDE OFF: no glide", og.count(" gl1 0 0") == 2, og.split(" gl1 ")[1][:8] if " gl1 " in og else og[:80])
o, g_b, _ = run("gl_on", GL + [(500, "enc 5 64")] + TWO, 5500)
w = [x.split(" gl1 ")[1].split()[:2] for x in o.splitlines() if " gl1 " in x]
check("GLIDE 64 (160 ms): the second note starts at the first's pitch, gets there", len(w) == 2 and w[0][0] == "64" and
      int(w[0][1]) > 8 * 256 and w[1] == ["64", "0"] and g_b != g0_b, str(w))
o1, _, _ = run("gl_save", GL + [(500, "enc 5 77"), (1700, "keys 0")], 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "glsaves.bin")))
o2, _, _ = run("gl_load", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "glsaves.bin")))
check("GLIDE saved with the project, loaded; a GR!MM 1.0 project: off", " gl1 77 " in o2 and " gl1 0 " in o3,   # (o3: 1.0)
      f"{o2.split(' gl1 ')[1][:6] if ' gl1 ' in o2 else '?'} {o3.split(' gl1 ')[1][:6] if ' gl1 ' in o3 else '?'}")
print("play test: passed" if not bad else "play test: FAILED")
sys.exit(1 if bad else 0)
