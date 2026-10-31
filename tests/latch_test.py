#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""LATCH in host mode (tests/run_host.sh), every factory sound, SYSTEM 3's LATCH FROM SOUND and ALWAYS OFF:
  a sound loaded (PRESETS): FROM SOUND, the LATCH saved in it (as 1.0); ALWAYS OFF, OFF (a latched note let go)
  then EDIT > SOURCE's LATCH ON / OFF and SEL + OCT+ always work: a key tapped latches, again lets go
  slots A..D and the project (saved, loaded) keep their tracks' LATCH, under both
  PHYS (at most 4 voices): a latched note stays while a release's voice is there to take
  REC held, ALL NOTES OFF, a MIDI note-on again (the arp's fix): latching as in 1.0
Prints a line a check (a sound's: only when it fails), exit 1 on a failure.
  python3 tests/latch_test.py [build/host/grimm]"""
import os
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/latch")
OUT.mkdir(parents=True, exist_ok=True)
ENG_FM, ENG_PHYS = 1, 5
C4, E4, G4, A3, D4 = 0x80, 0x800, 0x4000, 0x10, 0x200
bad = 0


def settings(latch):
    b = bytearray(170)
    b[167], b[169] = 3, latch
    f = OUT / f"set{latch}.bin"
    f.write_bytes(bytes(b))
    return dict(os.environ, GRIMM_SETTINGS_IN=str(f))


def run(name, ev, end, env):
    p = OUT / f"{name}.txt"
    p.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(p)], capture_output=True, text=True, check=True, env=env).stdout
    pr = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            w = ln.split(" ui ", 1)[1].split()
            d = {w[i]: w[i + 1] for i in range(0, len(w) - 1, 2)}
            pr[int(ln.split()[1])] = dict(latch=int(d["latch"].split(",")[0]), page=int(d["page"]), eng=int(d["eng"]),
                                          saved=int(d["slatch"]),
                                          notes=[x for x in ln.split(" notes", 1)[1].split(" fx ")[0].split()
                                                 if x.startswith("1:")])
    return pr


def check(what, ok, detail="", quiet=False):
    global bad
    if not ok or not quiet:
        print(f"  {'ok' if ok else 'FAIL':4s}  latch: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def tap(t, k):
    return [(t, f"keys {k:#x}"), (t + 60, "keys 0")]


SEL_OCT = lambda t: [(t, "down 3"), (t + 50, "btn 1"), (t + 100, "up 3")]   # noqa: E731
ENV = {0: settings(0), 1: settings(1)}
NAME = {0: "FROM SOUND", 1: "ALWAYS OFF"}

# every factory sound (PRESETS 1 .. 40), each setting
n_ok = 0
for k in range(1, 45):
    eng = run("eng", [(300, f"enc 6 {k}"), (500, "probe")], 600, ENV[0])[500]["eng"]
    edit = [(700 + 100 * i, "btn 6") for i in range(4 if eng == ENG_FM else 2)]   # (FM: FM ALGO, FM OP before SOURCE)
    for s in (0, 1):
        ev = [(300, f"enc 6 {k}"), (600, "probe")] + edit + [(1200, "probe"), (1250, "enc 3 1"), (1300, "probe")]
        ev += tap(1400, C4) + [(1700, "probe")] + tap(1800, C4) + [(2100, "probe")]          # latched; let go
        ev += SEL_OCT(2200) + [(2400, "probe")] + tap(2500, C4) + [(2800, "probe")]          # OFF: not latched
        ev += SEL_OCT(3000) + [(3200, "probe")] + tap(3300, C4) + [(3600, "probe")]          # ON: latched
        ev += [(3700, "enc 3 -1"), (3900, "probe")]                                          # EDIT OFF: let go
        ev += [(4000, "enc 3 1")] + tap(4100, E4) + [(4300, f"enc 6 1"), (4500, "probe")]    # latched, a sound loaded
        p = run(f"s{s}_{k}", ev, 4600, ENV[s])
        want = p[600]["saved"] if s == 0 else 0
        nxt = p[4500]["saved"] if s == 0 else 0
        ok = (p[600]["latch"] == want and p[1200]["page"] == 1 and p[1300]["latch"] == 1 and p[1700]["notes"] and
              not p[2100]["notes"] and p[2400]["latch"] == 0 and not p[2800]["notes"] and p[3200]["latch"] == 1 and
              p[3600]["notes"] and p[3900]["latch"] == 0 and not p[3900]["notes"] and p[4500]["latch"] == nxt and
              (bool(p[4500]["notes"]) == bool(nxt)))
        n_ok += ok
        check(f"{NAME[s]}, sound {k}", ok, {t: (p[t]["latch"], p[t]["notes"]) for t in sorted(p)}, quiet=True)
check(f"all 44 factory sounds, both settings: loaded as set; EDIT's LATCH and SEL + OCT+ latch and let go ({n_ok}/88)",
      n_ok == 88)

# slots A..D and the project keep their tracks' LATCH (SYSTEM 3 acts on a sound's load only)
for s in (0, 1):
    ev = SEL_OCT(300) + [(500, "down 9")] + tap(600, C4) + [(800, "up 9")]                # LATCH ON; SAVE + C4: into A
    ev += SEL_OCT(1000) + [(1200, "probe"), (1300, "down 9")] + tap(1400, 0x1) + [(1600, "up 9")]   # OFF; SAVE + F3: A
    ev += [(1900, "probe")] + tap(2000, C4) + [(2300, "probe")] + tap(2400, C4) + [(2700, "probe")]
    p = run(f"slot{s}", ev, 2800, ENV[s])
    check(f"{NAME[s]}: a slot recalled brings its tracks' LATCH (ON); keys latch, let go", p[1200]["latch"] == 0 and
          p[1900]["latch"] == 1 and p[2300]["notes"] and not p[2700]["notes"],
          {t: (p[t]["latch"], p[t]["notes"]) for t in sorted(p)})
    env = dict(ENV[s], GRIMM_SAVE_OUT=str(OUT / f"pj{s}.bin"))
    run(f"pj{s}", SEL_OCT(300), 6000, env)                                                 # (the autosave keeps it)
    p = run(f"pjl{s}", [(1000, "probe")] + tap(1100, C4) + [(1400, "probe")], 1500,
            dict(ENV[s], GRIMM_SAVE_IN=str(OUT / f"pj{s}.bin")))
    check(f"{NAME[s]}: the project saved, loaded: its LATCH ON, a key latches", p[1000]["latch"] == 1 and
          p[1400]["notes"], {t: (p[t]["latch"], p[t]["notes"]) for t in sorted(p)})

# PHYS: three keys latched, two let go again (their release tails), two more: the third stays (a release's voice taken)
for k in range(1, 45):
    if run("eng", [(300, f"enc 6 {k}"), (500, "probe")], 600, ENV[0])[500]["eng"] != ENG_PHYS:
        continue
    ev = [(300, f"enc 6 {k}"), (700, "btn 6"), (800, "btn 6"), (1200, "enc 3 1")]
    t = 1500
    for kb in (C4, E4, G4, C4, E4, A3, D4):
        ev += tap(t, kb)
        t += 250
    p = run(f"phys{k}", ev + [(4500, "probe")], 4600, ENV[0])
    check(f"PHYS sound {k}: G4 latched stays while A3, D4 latch beside two release tails",
          sorted(p[4500]["notes"]) == ["1:57", "1:62", "1:67"], p[4500]["notes"])

# block 4's other changes leave latching alone: REC held (REC back as before: keys latch); a MIDI note latched: ALL
# NOTES OFF leaves it, the note-on again lets it go (as 1.0: LATCH ON toggles), its fix for LATCH OFF not involved
ev = SEL_OCT(300) + [(500, "down 13"), (1700, "up 13")] + tap(1900, C4) + [(2200, "probe")]
p = run("rechold", ev, 2300, ENV[0])
check("REC held (pattern cleared, REC off again): a key latches", p[2200]["notes"] == ["1:60"], p[2200]["notes"])
ev = SEL_OCT(300) + [(600, "midi 0x90 64 100"), (700, "midi 0x80 64 0"), (900, "probe"), (1000, "midi 0xB0 123 0"),
                     (1200, "probe"), (1300, "midi 0x90 64 100"), (1600, "probe")]
p = run("midilatch", ev, 1700, ENV[0])
check("LATCH ON: a MIDI note latches; ALL NOTES OFF leaves it; the note again lets it go",
      p[900]["notes"] == ["1:64"] and p[1200]["notes"] == ["1:64"] and not p[1600]["notes"],
      {t: p[t]["notes"] for t in sorted(p)})
# (V2) whichever latched a note, its key or the MIDI note again lets it go; LATCH OFF lets go of all the track's notes
# (a DAW's notes on channel 1: T1; its STOP's note-offs ignored, as LATCH ON does)
ev = SEL_OCT(300) + [(600, "midi 0x90 60 100"), (650, "midi 0x80 60 0"), (700, "midi 0x90 64 100"),
                     (750, "midi 0x80 64 0"), (900, "probe")] + tap(1000, C4) + [(1300, "probe")]
ev += tap(1400, G4) + [(1600, "midi 0x91 67 100"), (1650, "midi 0x81 67 0"), (1900, "probe")]
p = run("cross", ev, 2000, ENV[0])
check("LATCH ON: a MIDI-latched note let go by its key (the same note in on T1's channel just after its key: taken for "
      "the FM-1's own echo, ignored as ever)",
      sorted(p[900]["notes"]) == ["1:60", "1:64"] and p[1300]["notes"] == ["1:64"] and p[1900]["notes"] == ["1:64", "1:67"],
      {t: p[t]["notes"] for t in sorted(p)})
ev = SEL_OCT(300) + [(400, "down 10"), (1200, "up 10"), (1400, "midi 0x90 60 100"), (1450, "midi 0x80 60 0")]
ev += [(1700, "probe")] + tap(1800, C4) + [(2300, "probe")]
p = run("crossarp", ev, 2400, ENV[0])
check("LATCH ON, ARP: a MIDI note in the arp let go by its key", p[1700]["notes"] and not p[2300]["notes"],
      {t: p[t]["notes"] for t in sorted(p)})
SEQ_RANDOM = [(300, "down 11"), (350, "keys 0x4"), (400, "keys 0"), (450, "up 11")]
ev = SEQ_RANDOM + SEL_OCT(600) + [(800, "btn 12"), (900, "midi 0x90 60 100"), (950, "midi 0x80 60 0")]
ev += tap(1000, G4) + [(1400, "probe")] + SEL_OCT(1500) + [(1700, "probe"), (1800, "btn 12"), (2600, "probe")]
p = run("latchoff", ev, 2700, ENV[0])
check("LATCH OFF: the track's notes let go, MIDI's, the keys', the sequencer's (none left on)",
      "1:60" in p[1400]["notes"] and "1:67" in p[1400]["notes"] and
      not [x for x in p[1700]["notes"] if x in ("1:60", "1:67")] and not p[2600]["notes"],
      {t: p[t]["notes"] for t in sorted(p)})
print("latch test: passed" if not bad else "latch test: FAILED")
sys.exit(1 if bad else 0)
