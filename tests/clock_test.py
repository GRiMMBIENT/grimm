#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's MIDI in host mode (tests/run_host.sh): the clock in from USB and the TRS jack at 60, 120 and 240 BPM for
5 minutes each (the tempo shown, no drift: 4 ticks a pulse, exactly), START / STOP / CONTINUE, the clock lost (the
FM-1's own tempo, playing on), the other source's clock and the tempo knobs ignored, the clock out (CLOCK INT+OUT:
evenly spaced, START and STOP), INT ignoring it all, and MIDI CHANNEL's CH n > SEL. Each a script for build/host/grimm
(firmware/src/host.c: "clock", "trs", "probe"); prints a line a check, exit 1 on a failure.
  python3 tests/clock_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/clock")
OUT.mkdir(parents=True, exist_ok=True)
CLOCK = {"INT": 0, "OUT": 1, "USB": 2, "TRS": 3}
bad = 0


def clock_setting(c):                           # SYSTEM 2 (HOME held, then HOME), KNOB 4: CLOCK
    return [(500, "down 8"), (1300, "up 8"), (1400, "btn 8"), (1500, f"enc 5 {CLOCK[c]}")]


def run(name, ev, end, env=None):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=env).stdout
    probes = {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            w = ln.split()
            d = {w[i]: w[i + 1] for i in range(2, len(w) - 1, 2) if w[i] != "notes"}
            p = {k: (int(v) if re.fullmatch(r"-?\d+", v) else v) for k, v in d.items()}
            p["pos"] = [int(x) for x in d["pos"].split(",")]
            p["notes"] = ln.split(" notes", 1)[1].split(" fx ", 1)[0].split()
            probes[int(w[1])] = p
    return probes, out


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  clock: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def steady(c, src, bpm10):                      # 5 minutes of clock at a tempo: START, then probes each minute
    ev = clock_setting(c) + [(2000, f"clock {src} {bpm10} 300000"), (3000, f"{'trs' if src == 'trs' else 'midi'} 0xFA 0 0")]
    ev += [(2000 + 60000 * k, "probe") for k in range(1, 6)] + [(301900, "probe"), (303000, "probe"), (304000, "probe")]
    ev += [(5000, "btn 6"), (5100, "btn 8"), (5200, "enc 0 30"), (5300, "probe")]   # HOME, SELECT turned: ignored
    pr, _ = run(f"steady_{src}_{bpm10}", ev, 304100)
    run_p = [pr[t] for t in sorted(pr) if 60000 <= t <= 301900]
    check(f"{src.upper()} {bpm10 / 10:g} BPM: followed, its tempo shown",
          all(p["ext"] == 1 and abs(p["bpm"] - bpm10) <= 1 for p in run_p), [p["bpm"] for p in run_p])
    offs = {p["done"] - p["ticks"] for p in run_p}
    check(f"{src.upper()} {bpm10 / 10:g} BPM: 5 minutes, no drift (4 ticks a pulse, the sequencer's ticks with them)",
          all(4 * p["pulses"] - 3 <= p["done"] <= 4 * p["pulses"] for p in run_p) and len(offs) == 1,
          f"done-ticks {sorted(offs)}")
    t_end = 301900
    want = (t_end - 3000) * bpm10 * 96 // 600000  # ticks since START at the tempo (START waits up to a pulse)
    check(f"{src.upper()} {bpm10 / 10:g} BPM: the ticks the tempo makes",
          abs(pr[t_end]["ticks"] - want) <= 4 * 2 + 4 * bpm10 * 24 // 600 // 50, f"{pr[t_end]['ticks']} vs {want}")
    check(f"{src.upper()} {bpm10 / 10:g} BPM: SELECT on HOME leaves the tempo alone", pr[5300]["own"] == 120)
    check(f"{src.upper()} {bpm10 / 10:g} BPM: the clock gone: the FM-1's own tempo, playing on",
          pr[303000]["ext"] == 0 and pr[303000]["seq"] == 1 and pr[304000]["ticks"] > pr[303000]["ticks"])


def transport():                                # USB at 240: STOP, CONTINUE, START, the TRS clock ignored, PLAY waits
    ev = clock_setting("USB") + [(2000, "clock usb 2400 60000"), (3000, "midi 0xFA 0 0"), (3500, "probe"),
                                 (9990, "probe"), (10000, "midi 0xFC 0 0"), (10100, "probe"), (11000, "probe"),
                                 (12000, "midi 0xFB 0 0"), (12050, "probe"), (14000, "probe"),
                                 (20000, "midi 0xFA 0 0"), (20030, "probe"),
                                 (30000, "midi 0xFC 0 0"), (30100, "probe"), (31000, "btn 12"), (31002, "probe"),
                                 (31100, "probe"), (40000, "btn 12"), (40100, "probe"),
                                 (63000, "clock trs 1200 5000"), (63500, "trs 0xFA 0 0"), (64000, "probe")]
    pr, _ = run("transport", ev, 64100)
    check("START: the sequencer from step 1, PLAY with it", pr[3500]["seq"] == 1 and pr[3500]["play"] == 1)
    stop, still = pr[10100], pr[11000]
    check("STOP: stopped, PLAY off", stop["seq"] == 0 and stop["play"] == 0 and still["seq"] == 0)
    b, a = pr[9990], pr[12050]                  # (240 BPM: a 1/16 step is 24 ticks, 62.5 ms; a tick 2.6 ms)
    check("CONTINUE: on from where it stopped", a["seq"] == 1 and a["play"] == 1 and
          b["ticks"] < a["ticks"] <= b["ticks"] + 32 and (a["pos"][0] - b["pos"][0]) % 16 in (0, 1, 2) and
          pr[14000]["seq"] == 1, f"before STOP {b['pos'][0]}/{b['ticks']}, after CONTINUE {a['pos'][0]}/{a['ticks']}")
    check("START again: from step 1", pr[20030]["seq"] == 1 and pr[20030]["pos"][0] == 0 and pr[20030]["ticks"] <= 14,
          f"pos {pr[20030]['pos']}, ticks {pr[20030]['ticks']}")
    check("PLAY on the FM-1 while following: at the next pulse", pr[31002]["seq"] == 0 and pr[31100]["seq"] == 1 and
          pr[31100]["play"] == 1, f"{pr[31002]['seq']} {pr[31100]['seq']}")
    check("STOP on the FM-1 while following", pr[40100]["seq"] == 0 and pr[40100]["play"] == 0)
    check("CLOCK USB: the jack's clock and START ignored", pr[64000]["ext"] == 0 and pr[64000]["seq"] == 0)


def clock_out():                                # INT+OUT at 120: evenly spaced pulses, START, STOP; INT: none
    ev = clock_setting("OUT") + [(3000, "btn 12"), (33000, "btn 12"), (34000, "probe")]
    pr, out = run("out", ev, 40000)
    m = re.search(r"clock  out: (\d+) pulses, ([\d.]+) \.\. ([\d.]+) ms apart", out)
    lo, hi = (float(m.group(2)), float(m.group(3))) if m else (0, 0)
    check("INT+OUT 120 BPM: a pulse every 20.833 ms, stopped and playing, never skipped",
          m is not None and 20.80 <= lo and hi <= 20.87 and int(m.group(1)) >= 1800, m.group(0) if m else "none")
    fa = [int(x) for x in re.findall(r"(\d+) ms  clock out FA", out)]
    fc = [int(x) for x in re.findall(r"(\d+) ms  clock out FC", out)]
    check("INT+OUT: START as PLAY starts, STOP as it stops", len(fa) == 1 and len(fc) == 1 and 3000 <= fa[0] <= 3040 and
          33000 <= fc[0] <= 33020, f"FA {fa} FC {fc}")
    # (V2.0's clock out on the timer: the audio ISR 0 .. 100 us late, its clock 44118 Hz, not 44100: the pulses still
    # exactly 60e6 / BPM ticks apart, the remainder carried; a new tempo glides there, never past it)
    env = dict(os.environ, GRIMM_CLOCK_LOG="1", GRIMM_ISR_JITTER="100", GRIMM_AUDIO_HZ="44118")
    HOME = [(1600, "btn 6"), (1700, "btn 8")]   # (EDIT, then HOME: HOME, where SELECT is the tempo)
    for bpm, turn in ((120, None), (133, "enc 0 13")):
        _, out = run(f"out{bpm}", clock_setting("OUT") + HOME + ([(1800, turn)] if turn else []), 20000, env)
        p = [int(ln.split()[1]) for ln in out.splitlines() if ln.startswith("pulse ")]
        p = [x for x in p if x > 6 * 24000000]
        dev = max(abs(x - p[0] - (i * 60000000 + bpm // 2) // bpm) for i, x in enumerate(p))
        check(f"INT+OUT {bpm} BPM, the audio ISR late 0 .. 0.1 ms, its clock 44118 Hz: each pulse on the exact grid "
              f"(60e6 / {bpm} ticks)", len(p) > 300 and dev <= 1, f"{len(p)} pulses, {dev} ticks off")
    _, out = run("outglide", clock_setting("OUT") + HOME + [(8000, "enc 0 10")], 16000, env)
    p = [int(ln.split()[1]) for ln in out.splitlines() if ln.startswith("pulse ")]
    beats = [1440000000.0 / (p[i + 24] - p[i]) for i in range(0, len(p) - 24, 24) if p[i] > 8 * 24000000]
    check("INT+OUT 120 -> 130 BPM: glides there (no jump, never past it), within 0.05 BPM in 3 s",
          beats[0] < 129 and all(a <= b + 0.01 for a, b in zip(beats, beats[1:])) and max(beats) < 130.05 and
          abs(beats[6] - 130) < 0.05, " ".join(f"{b:.2f}" for b in beats[:8]))
    ev = clock_setting("INT") + [(2000, "clock usb 1200 10000"), (3000, "midi 0xFA 0 0"), (5000, "probe")]
    pr, out = run("int", ev, 6000)
    check("INT: the clock and START from outside ignored, nothing out",
          pr[5000]["ext"] == 0 and pr[5000]["seq"] == 0 and "clock out" not in out and "clock  out" not in out)


def channels():                                 # MIDI CHANNEL CH 5 > SEL, USB and the jack
    ev = [(500, "down 8"), (1300, "up 8"), (1400, "enc 3 18"),       # SYSTEM: MIDI CHANNEL 1-4 -> CH 5 > SEL
          (2000, "midi 0x94 60 100"), (2050, "probe"), (2100, "midi 0x84 60 0"),
          (2200, "midi 0x90 62 100"), (2250, "probe"), (2300, "midi 0x80 62 0"),
          (2400, "trs 0x94 64 100"), (2450, "probe"), (2500, "trs 0x84 64 0"), (2550, "probe"),
          (2600, "enc 1 1"), (2700, "trs 0x94 65 100"), (2750, "probe"), (2800, "trs 0x84 65 0"),
          (2900, "midi 0x95 67 100"), (2950, "probe"), (3000, "midi 0x85 67 0")]
    pr, _ = run("channels", ev, 3100)
    check("CH 5 > SEL: channel 5 plays the selected track (USB)", pr[2050]["notes"] == ["1:60"], pr[2050]["notes"])
    check("CH 5 > SEL: channel 1 ignored", pr[2250]["notes"] == [], pr[2250]["notes"])
    check("CH 5 > SEL: from the TRS jack the same", pr[2450]["notes"] == ["1:64"] and pr[2550]["notes"] == [],
          pr[2450]["notes"])
    check("CH 5 > SEL: another track selected, it plays that one", pr[2750]["notes"] == ["2:65"], pr[2750]["notes"])
    check("CH 5 > SEL: channel 6 ignored", pr[2950]["notes"] == [], pr[2950]["notes"])


def settings():                                 # V1.0's settings (168 B, version 1): as they were, CLOCK INT; V2's
    v1 = bytearray(168)                         # (docs/EDITOR_PROTOCOL.md: the settings)
    v1[0:7] = bytes([1, 256 - 12, 2, 5, 1, 1, 1])   # octave +1, TUNE -12, TEAL, MIDI CHANNEL 6-9, 1 MIN, SEQ OUT, a link
    v1[7:12] = bytes([74, 0, 1, 0, 0])          # (the link: CC 74 to SOUND's KNOB 2, T1)
    v1[167] = 1
    for name, blob, mch, clk in (("v1", bytes(v1), 5, 0), ("v2", bytes(v1[:167]) + bytes([2, 2]), 5, 2),
                                 ("v2mch", bytes(v1[:3]) + bytes([20]) + bytes(v1[4:167]) + bytes([2, 3]), 20, 3)):
        f = OUT / f"settings_{name}.bin"
        f.write_bytes(blob)
        pr, _ = run(f"settings_{name}", [(1000, "probe")], 1100, dict(os.environ, GRIMM_SETTINGS_IN=str(f)))
        p = pr[1000]
        check(f"settings {name} ({len(blob)} B): MIDI CHANNEL, the links as saved, CLOCK "
              f"{['INT', 'INT+OUT', 'USB', 'TRS'][clk]}", p["mch"] == mch and p["clk"] == clk and p["links"] == 1,
              f"mch {p['mch']} clk {p['clk']} links {p['links']}")


for src, bpm in (("usb", 600), ("usb", 1200), ("trs", 1200), ("usb", 2400), ("trs", 600), ("trs", 2400)):
    steady(src.upper(), src, bpm)
transport()
clock_out()
channels()
settings()
print("clock test: passed" if not bad else "clock test: FAILED")
sys.exit(1 if bad else 0)
