#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's quick wins in host mode (tests/run_host.sh): SYSTEM 3's LATCH (FROM SOUND: a sound loaded brings its
own, as 1.0; ALWAYS OFF: a sound loaded sets it OFF; saved with the settings; older settings: FROM SOUND), SEL + OCT+
(the selected track's LATCH on, off: its latched notes released), REC held ~1 s (the selected track's pattern cleared,
undoable; REC as before; a short press as ever), SAVES WHEN SILENT said ~2 s, then out of the way; MIDI in from a
looping DAW: a note-on again before its off neither restarts the arp nor is ended by the earlier one's late off, ALL
NOTES OFF lets go, and with CLOCK INT the clock's messages leave the arp's rate alone. Prints a line a check, exit 1
on a failure.
  python3 tests/quick_test.py [build/host/grimm]"""
import os
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/quick")
OUT.mkdir(parents=True, exist_ok=True)
PG_HOME, PG_SYSTEM, PG_SYS2, PG_PROJECTS, PG_SYS3 = 7, 10, 11, 13, 27
bad = 0


def run(name, ev, end, env=None):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=env).stdout
    pr = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            w = ln.split(" ui ", 1)[1].split()
            d = {w[i]: w[i + 1] for i in range(0, len(w) - 1, 2)}
            pr[int(ln.split()[1])] = dict(page=int(d["page"]), sel=int(d["sel"]), rec=int(d["rec"]),
                                          latch=[int(x) for x in d["latch"].split(",")], new=int(d["new"]),
                                          steps=int(d["steps"]), wait=int(d["wait"]), said=int(d["said"]),
                                          notes=ln.split(" notes", 1)[1].split(" fx ", 1)[0].split(),
                                          voices=[tuple(int(x) for x in v.split("/"))
                                                  for v in ln.split(" voices ")[1].split()])
    return pr, out


def settings(name, out=0, latch=0):             # version 3's settings (170 B): SEQ MIDI OUT, SYSTEM 3's LATCH
    b = bytearray(170)
    b[5], b[167], b[169] = out, 3, latch
    f = OUT / f"set_{name}.bin"
    f.write_bytes(bytes(b))
    return dict(os.environ, GRIMM_SETTINGS_IN=str(f))


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  quick: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def probes(*ts):
    return [(t, "probe") for t in ts]


# SYSTEM 3: HOME held (SYSTEM), HOME, HOME; LATCH turned to ALWAYS OFF (saved: 170 B); HOME: SYSTEM again
ev = [(300, "down 8"), (1000, "up 8"), (1200, "btn 8"), (1400, "btn 8"), (1600, "enc 2 1"), (4000, "btn 8")]
pr, out = run("sys3", ev + probes(1100, 1300, 1500, 1700, 4100), 4200)
check("HOME held, HOME, HOME: SYSTEM, SYSTEM 2, SYSTEM 3; HOME again: SYSTEM",
      [pr[t]["page"] for t in (1100, 1300, 1500, 4100)] == [PG_SYSTEM, PG_SYS2, PG_SYS3, PG_SYSTEM],
      [pr[t]["page"] for t in (1100, 1300, 1500, 4100)])
check("SYSTEM 3's LATCH: FROM SOUND as set out; turned to ALWAYS OFF, saved with the settings (170 B)",
      pr[1500]["new"] == 0 and pr[1700]["new"] == 1 and "settings saved (170 B)" in out)
# a sound loaded (PRESETS: CRYPT PAD, saved with LATCH ON)
PRESET = [(300, "enc 6 1")]
pr, _ = run("snd_from", PRESET + probes(500), 600)
check("FROM SOUND (as 1.0): a sound loaded brings its LATCH (CRYPT PAD: ON)", pr[500]["latch"][0] == 1, pr[500]["latch"])
pr, _ = run("snd_off", PRESET + probes(500), 600, settings("off", latch=1))
check("ALWAYS OFF: CRYPT PAD loaded, the track's LATCH stays OFF", pr[500]["latch"][0] == 0, pr[500]["latch"])
pr, _ = run("snd_hand", [(300, "down 3"), (400, "btn 1"), (500, "up 3"), (700, "enc 6 1"), (900, "enc 6 1"),
                         (1100, "enc 6 -1")] + probes(600, 1200), 1300, settings("off", latch=1))
check("ALWAYS OFF: latched by hand (SEL + OCT+), then a sound loaded: LATCH OFF (at the load)",
      pr[600]["latch"][0] == 1 and pr[1200]["latch"][0] == 0, f"{pr[600]['latch']} {pr[1200]['latch']}")
NEW = [(1500, "btn 9"), (1700, "btn 9"), (1900, "enc 5 3"), (2100, "btn 1")]
for name, env in (("from", None), ("off", settings("off", latch=1))):
    pr, _ = run(f"new_{name}", [(300, "down 3"), (400, "btn 1"), (500, "up 3")] + NEW + probes(600, 2300), 2400, env)
    check(f"{'ALWAYS OFF' if env else 'FROM SOUND'}: NEW's tracks LATCH OFF, as in 1.0",
          pr[600]["latch"][0] == 1 and pr[2300]["page"] == PG_PROJECTS and pr[2300]["latch"] == [0, 0, 0, 0],
          f"{pr[600]['latch']} {pr[2300]['latch']}")

# the settings saved before: V2's version 2 (169 B) and V1.0's (168 B) load as FROM SOUND; version 3's as saved
v1 = bytearray(168)
v1[0:7] = bytes([1, 256 - 12, 2, 5, 1, 1, 0])
v1[167] = 1
for name, blob, want in (("v1", bytes(v1), 0), ("v2", bytes(v1[:167]) + bytes([2, 2]), 0),
                         ("v3", bytes(v1[:167]) + bytes([3, 2, 1]), 1), ("v3from", bytes(v1[:167]) + bytes([3, 2, 0]), 0)):
    f = OUT / f"settings_{name}.bin"
    f.write_bytes(blob)
    pr, out = run(f"settings_{name}", probes(1000), 1100, dict(os.environ, GRIMM_SETTINGS_IN=str(f)))
    check(f"settings {name} ({len(blob)} B): LATCH {'ALWAYS OFF' if want else 'FROM SOUND'}", pr[1000]["new"] == want,
          pr[1000]["new"])

# SEL held + OCT+: T1's LATCH ON (no page change, no chord mode); a key then latches its note; again: OFF, the note let go
ev = [(300, "down 3"), (400, "btn 1"), (500, "up 3"), (700, "keys 0x80"), (800, "keys 0"), (1000, "down 3"),
      (1100, "btn 1"), (1200, "up 3"), (1500, "down 3"), (1600, "btn 1"), (2400, "up 3")] + probes(250, 600, 900, 1300, 2500)
pr, out = run("combo", ev, 2600)
check("SEL + OCT+: the track's LATCH ON, the page as it was", pr[600]["latch"] == [1, 0, 0, 0] and pr[600]["page"] == PG_HOME,
      f"latch {pr[600]['latch']} page {pr[600]['page']}")
check("LATCH ON: a key tapped, its note stays", pr[900]["notes"] == ["1:60"], pr[900]["notes"])
check("SEL + OCT+ again: LATCH OFF, the latched note let go", pr[1300]["latch"] == [0, 0, 0, 0] and pr[1300]["notes"] == [],
      f"latch {pr[1300]['latch']} notes {pr[1300]['notes']}")
check("SEL + OCT+ held long, let go: LATCH ON, still no page change (not the chord mode's hold)",
      pr[2500]["latch"] == [1, 0, 0, 0] and pr[2500]["page"] == PG_HOME,
      f"latch {pr[2500]['latch']} page {pr[2500]['page']}")

# REC: a pattern (SEQ held + white key 2: RANDOM); a short press: REC on, the steps kept; again: off. Held 1 s: the
# pattern cleared, REC as before the press; EDIT + OCT-: undone. Held while recording: cleared, still recording
SEQ_RANDOM = [(300, "down 11"), (350, "keys 0x4"), (400, "keys 0"), (450, "up 11")]
ev = SEQ_RANDOM + [(600, "btn 13"), (800, "btn 13"), (1000, "down 13"), (2300, "up 13"), (2500, "down 6"), (2550, "btn 0"),
                   (2600, "up 6")] + probes(500, 700, 900, 1500, 2100, 2400, 2700)
pr, out = run("rec", ev, 2800)
n = pr[500]["steps"]
check("a pattern made (SEQ + RANDOM)", n > 0, n)
check("REC tapped: on, the pattern kept; tapped again: off", pr[700]["rec"] == 1 and pr[700]["steps"] == n and
      pr[900]["rec"] == 0 and pr[900]["steps"] == n)
check("REC held: under ~1 s nothing cleared (REC on meanwhile, as a tap)", pr[1500]["rec"] == 1 and pr[1500]["steps"] == n)
check("REC held ~1 s: the track's pattern cleared, REC off again (as before the press)",
      pr[2100]["steps"] == 0 and pr[2100]["rec"] == 0 and pr[2400]["steps"] == 0 and pr[2400]["rec"] == 0,
      f"{pr[2100]} {pr[2400]}")
check("EDIT + OCT-: the clear undone", pr[2700]["steps"] == n, pr[2700]["steps"])
ev = SEQ_RANDOM + [(600, "btn 13"), (1000, "down 13"), (2300, "up 13")] + probes(900, 2100, 2400)
pr, _ = run("rec_on", ev, 2500)
check("REC held while recording: cleared, still recording", pr[2100]["steps"] == 0 and pr[2400]["rec"] == 1 and
      pr[900]["rec"] == 1, f"{pr[2100]} {pr[2400]}")
ev = [(200, "enc 1 1")] + SEQ_RANDOM + [(1000, "down 13"), (2300, "up 13")] + probes(900, 2400)
pr, _ = run("rec_t2", ev, 2500)
check("REC held on T2: T2's pattern cleared", pr[900]["sel"] == 1 and pr[900]["steps"] > 0 and pr[2400]["steps"] == 0,
      f"{pr[900]} {pr[2400]}")

# SAVES WHEN SILENT: a latched note (never silent), a knob turned: the autosave waits; the header says so ~2 s, then
# gives the page back while it still waits; PLAY, STOP: a new wait, said again
ev = [(300, "down 3"), (400, "btn 1"), (500, "up 3"), (700, "keys 0x80"), (800, "keys 0"), (1200, "enc 2 -5"),
      (9000, "btn 12"), (9400, "btn 12")]
pr, out = run("said", ev + probes(*range(1300, 15000, 100)), 15000)
w = [t for t in sorted(pr) if pr[t]["wait"]]
t0 = w[0] if w else 0
check("an autosave waits for a drone: SAVES WHEN SILENT said", bool(w) and pr[t0]["said"] == 1, w[:1])
check("~2 s on: still waiting (no save), the header given back",
      bool(w) and all(pr[t]["wait"] and not pr[t]["said"] for t in pr if t0 + 2100 <= t < 9000) and
      all(pr[t]["said"] for t in pr if t0 <= t < t0 + 1900), f"from {t0}")
w2 = [t for t in sorted(pr) if t > 9400 and pr[t]["wait"]]
check("PLAY, STOP: a new wait, said again (~2 s)", bool(w2) and pr[w2[0]]["said"] == 1 and
      not pr[min(14900, w2[0] + 2200)]["said"], w2[:1])


# MIDI in from a looping DAW (T1, ARP on, SEQ MIDI OUT: the arp's notes out, timed)
def arp_ons(name, ev, end=9000, env=None):
    _, out = run(name, [(300, "down 10"), (1100, "up 10")] + ev, end, env or settings("mo", out=1))
    return [int(ln.split()[0]) for ln in out.splitlines() if " MIDI out 90 " in ln and not ln.endswith(" 0")]


N = lambda t, n: (t, f"midi 0x90 {n} 100")      # noqa: E731
F = lambda t, n: (t, f"midi 0x80 {n} 0")        # noqa: E731
LOOP = [3530, 5560, 7590]                       # (the DAW's loop points, mid-step)
CH = (60, 64, 67)
one = arp_ons("arp_one", [N(1500, 60)])
chord = arp_ons("arp_chord", [N(1500, n) for n in CH])
check("the arp steady (a note held): its steps ~125 ms apart", len(one) > 50 and
      all(115 <= b - a <= 135 for a, b in zip(one, one[1:])))
r = arp_ons("arp_onoff", [N(1500, 60)] + [e for t in LOOP for e in (N(t, 60), F(t + 3, 60))])
check("a note-on again before its off, the off 3 ms late: the arp as if held (no restart, not stopped)", r == one,
      f"{len(r)} steps, steady {len(one)}")
r = arp_ons("arp_onon", [N(1500, 60)] + [N(t, 60) for t in LOOP])
check("a note-on again, no off: the arp as if held", r == one, f"{len(r)} steps")
r = arp_ons("arp_chordloop", [N(1500, n) for n in CH] + [N(t, n) for t in LOOP for n in CH] +
            [F(t + 2, n) for t in LOOP for n in CH])
check("a chord again before its offs: the arp as if held", r == chord, f"{len(r)} steps, steady {len(chord)}")
r = arp_ons("arp_chordoff", [N(1500, n) for n in CH] + [F(4000, n) for n in CH])
check("a chord let go: the arp stops (as ever)", r and r[-1] < 4130, r[-1:])
for name, ev in (("START at the loops", [(t, "midi 0xFA 0 0") for t in LOOP]),
                 ("STOP, START at the loops", [e for t in LOOP for e in ((t, "midi 0xFC 0 0"), (t + 1, "midi 0xFA 0 0"))]),
                 ("a clock (120 BPM)", [(1400, "clock usb 1200 7000")]),
                 ("a clock and START at the loops", [(1400, "clock usb 1200 7000")] + [(t, "midi 0xFA 0 0") for t in LOOP]),
                 ("SONG POSITION at the loops", [(t, "midi 0xF2 0 0") for t in LOOP])):
    r = arp_ons("arp_rt", [N(1500, 60)] + ev)
    check(f"CLOCK INT, {name}: the arp's steps as without them", r == one, f"{len(r)} steps")
P = [(1200, "btn 12")]
play = arp_ons("arp_play", P + [N(1500, 60)])
r = arp_ons("arp_play_rt", P + [N(1500, 60), (1400, "clock usb 1200 7000")] + [(t, "midi 0xFA 0 0") for t in LOOP])
check("CLOCK INT, the sequencer playing, a clock and START at the loops: the arp's steps as without them", r == play,
      f"{len(r)} steps, {len(play)}")
r = arp_ons("arp_cc123", [N(1500, 60), N(1500, 64), (4000, "midi 0xB0 123 0")])
check("ALL NOTES OFF (CC 123): the notes MIDI holds let go, the arp stops", r and r[-1] < 4130, r[-1:])
# without the arp: a note-on again before its off sounds again; the earlier one's late off leaves it on
pr, _ = run("midi_again", [N(1000, 60), N(2000, 60), F(2003, 60)] + probes(1500, 2500) + [F(3000, 60)] + probes(3500), 3600)
check("no arp: a note-on again before its off; the earlier one's late off: the note goes on; its own off ends it",
      pr[1500]["notes"] == ["1:60"] and pr[2500]["notes"] == ["1:60"] and pr[3500]["notes"] == [],
      f"{pr[1500]['notes']} {pr[2500]['notes']} {pr[3500]['notes']}")
pr, _ = run("midi_plain", [N(1000, 60), F(2000, 60), N(2000, 60), F(3000, 60)] + probes(1500, 2500, 3500), 3600)
check("no arp, a note let go and played again: as ever", pr[2500]["notes"] == ["1:60"] and pr[3500]["notes"] == [],
      f"{pr[2500]['notes']} {pr[3500]['notes']}")

# (V2) voices: a track never takes another's (1.0: a 7th note took the oldest note of any track, a release of any)
SIX = 0x80 | 0x200 | 0x800 | 0x1000 | 0x4000 | 0x10000          # (C4 D4 E4 F4 G4 A4)
ev = [(300, "down 3"), (350, "btn 1"), (400, "up 3")] + [(500, "keys 0x10"), (560, "keys 0")]   # T1: LATCH, A3 latched
ev += [(700, "enc 1 1"), (900, f"keys {SIX:#x}"), (1200, "probe")]                                 # T2: six keys held
ev += [(1300, "enc 1 1"), (1400, f"keys {SIX | 0x4:#x}"), (1700, "probe")]                         # T3: one more (G3)
pr, _ = run("voices", ev, 1800)
n1 = lambda p, t: len([x for x in p["notes"] if x.startswith(f"{t}:")])   # noqa: E731
check("six keys on T2 beside T1's latched drone: the drone stays, T2 gets the five voices free",
      n1(pr[1200], 1) == 1 and n1(pr[1200], 2) == 5, pr[1200]["notes"])
check("T3 with no voice at all: one from the track holding the most (T2), T1's drone untouched",
      n1(pr[1700], 1) == 1 and n1(pr[1700], 2) == 4 and n1(pr[1700], 3) == 1, pr[1700]["notes"])
ev = [(300, "keys 0x880"), (500, "keys 0"), (550, "enc 1 1"), (600, f"keys {SIX:#x}"), (700, "probe")]
pr, _ = run("voicerel", ev, 800)
rel1 = [v for v in pr[700]["voices"] if v[1] == 1 and v[0] == 3]
check("T1's release tails not taken by T2's six keys (T2: the four idle voices)",
      len(rel1) == 2 and n1(pr[700], 2) == 4, f"{pr[700]['notes']} {pr[700]['voices']}")
ev = [(300, "btn 6"), (350, "btn 6"), (400, "enc 2 5"), (600, f"keys {SIX:#x}"), (900, "probe")]   # (T1: PHYS)
pr, _ = run("voicephys", ev, 1000)
check("PHYS: at most four notes still (the fifth, sixth not played)", n1(pr[900], 1) == 4, pr[900]["notes"])
ev = [(300, "down 3"), (350, "btn 1"), (400, "up 3"), (500, "keys 0x10"), (560, "keys 0"), (700, "enc 1 1"),
      (900, f"keys {SIX:#x}"), (1200, "keys 0"), (1500, "probe")]   # (T1's drone; T2: six keys, five played, let go)
pr, out = run("voiceecho", ev, 1600)
mo = [ln.split("MIDI out ")[1].split() for ln in out.splitlines() if " MIDI out 9" in ln or " MIDI out 8" in ln]
on2 = sorted(m[1] for m in mo if m[0] == "91" and m[2] != "0")
off2 = sorted(m[1] for m in mo if m[0] == "81" or (m[0] == "91" and m[2] == "0"))
check("a key's note not played (no voice): no note-on out over USB either, so none is left on out there",
      len(on2) == 5 and on2 == off2 and pr[1500]["notes"] == ["1:57"], f"on {on2} off {off2} {pr[1500]['notes']}")
print("quick test: passed" if not bad else "quick test: FAILED")
sys.exit(1 if bad else 0)
