#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0's metronome in host mode (tests/run_host.sh): SEQ held + F#3 turns CLICK on and off (the header says so, F#3
is lit while SEQ is held and CLICK is on, no note plays, no page changes); with it on, while the sequencer plays (REC on
or off) a click lands on every beat of the FM-1's tempo and of a MIDI clock (USB), a higher and louder one on beat 1 of
the bar, with no count-in and nothing when stopped; no voice is used, no pattern is touched; CLICK is not saved (off at power-on);
F#3 flashes with the clicks while CLICK is on and the sequencer plays (longer on beat 1).
The clicks are found in the sound the host run writes (build/host/click/NAME.wav).
  python3 tests/click_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
import wave
from array import array
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/click")
OUT.mkdir(parents=True, exist_ok=True)
SEQ, PLAY, REC, EDIT, HOME = 11, 12, 13, 6, 8
F_SHARP3 = 2                                    # keys: bit 1 = F#3
FS = 44100
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  click: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, env=None):
    """-> (the probes {ms: line}, the clickstates {ms: (on, left, leds)}, the left channel as a list, the output)"""
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=dict(os.environ, **(env or {}))).stdout
    probes, cstate = {}, {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            probes[int(ln.split()[1])] = ln
        elif ln.startswith("clickstate "):
            w = ln.split()
            cstate[int(w[1])] = (int(w[3]), int(w[5]), int(w[7], 16))
    with wave.open(str(OUT / f"{name}.wav")) as w:
        a = array("h")
        a.frombytes(w.readframes(w.getnframes()))
    return probes, cstate, a[0::2].tolist(), out


def clicks(x, thr=300):
    """the clicks in a mono signal: [(onset ms, peak, zero crossings in 8 ms)], an onset after 100 ms under thr"""
    res, quiet, i, n = [], FS // 10, 0, len(x)
    last = -quiet
    while i < n:
        if abs(x[i]) > thr:
            if i - last >= quiet:
                seg = x[i:i + FS * 40 // 1000]
                zc = sum(1 for k in range(1, FS * 8 // 1000) if k < len(seg) and (seg[k - 1] < 0) != (seg[k] < 0))
                res.append((i * 1000.0 / FS, max(abs(v) for v in seg), zc))
            last = i
        i += 1
    return res


def field(line, key):
    return line.split(f" {key} ", 1)[1].split()[0]


def toggle(t):                                  # SEQ held, F#3 tapped
    return [(t, f"down {SEQ}"), (t + 100, f"keys {F_SHARP3}"), (t + 200, "keys 0"), (t + 300, f"up {SEQ}")]


# ---- the combo: on, off; the header; the key lit while SEQ is held; no note, no page change
ev = [(900, "probe"), (950, f"down {SEQ}"), (1000, "probe")] + [(1050, f"keys {F_SHARP3}"), (1100, "probe"), (1150, "keys 0"),
      (1200, "probe"), (1250, f"up {SEQ}"), (1300, "probe"),
      (2000, f"down {SEQ}"), (2050, f"keys {F_SHARP3}"), (2100, "probe"), (2150, "keys 0"), (2200, f"up {SEQ}"), (2300, "probe"),
      (3000, f"down {SEQ}"), (3100, "probe"), (3200, f"up {SEQ}"), (3300, "probe"),
      (4000, f"keys {F_SHARP3}"), (4100, "probe"), (4200, "keys 0")]
pr, cs, x, _ = run("combo", ev, 4600)
check("at power-on CLICK is off", cs[900][0] == 0)
check("SEQ held + F#3: CLICK ON in the header, on, F#3 lit", field(pr[1100], "hd") == "CLICK_ON" and cs[1100][0] == 1 and cs[1100][2] >> 1 & 1,
      f"{field(pr[1100], 'hd')} {cs[1100]}")
check("... no note plays and no voice starts (the key is the combo's, not a note), the page stays",
      pr[1100].split(" notes", 1)[1].split(" fx ", 1)[0].strip() == "" and
      all(v.split("/")[0] == "0" for v in pr[1100].split(" voices ")[1].split()) and
      field(pr[1300], "page") == field(pr[900], "page"))
check("SEQ let go: F#3 not lit any more, CLICK stays on", cs[1300][0] == 1 and not cs[1300][2] >> 1 & 1)
check("SEQ held + F#3 again: CLICK OFF in the header, off", field(pr[2100], "hd") == "CLICK_OFF" and cs[2100][0] == 0,
      f"{field(pr[2100], 'hd')} {cs[2100]}")
check("SEQ held, CLICK off: F#3 not lit", not cs[3100][2] >> 1 & 1)
check("F#3 alone (no SEQ) is still a note: it sounds on the selected track and CLICK stays as it was",
      "1:" in pr[4100].split(" notes", 1)[1].split(" fx ", 1)[0] or any(v.split("/")[0] != "0" for v in pr[4100].split(" voices ")[1].split()),
      pr[4100].split(" notes", 1)[1][:40])
check("a key pressed with SEQ held that is not a tool or F#3 (white keys 1..8 are tools, others play) leaves CLICK alone", cs[3300][0] == 0)

# ---- off: PLAY, nothing but silence; stopped with CLICK on: silence
pr, cs, x, _ = run("off", [(1000, f"btn {PLAY}")], 6000)
check("CLICK off: the sequencer plays, no click (silent)", not clicks(x) and max(abs(v) for v in x) <= 2, f"{len(clicks(x))} clicks")
pr, cs, x, _ = run("stopped", toggle(500) + [(4000, "probe")], 6000)
check("CLICK on, the sequencer stopped: silent", not clicks(x) and cs[4000][0] == 1)

# ---- on, playing: a click on every beat at the FM-1's tempo, an accent on beat 1 of each bar, no count-in
ev = toggle(500) + [(2000, f"btn {PLAY}"), (4010, "probe"), (6010, "probe"), (9000, f"btn {PLAY}")]
pr, cs, x, _ = run("beats", ev, 12000)
c = clicks(x)
t0 = c[0][0] if c else 0
beats = [round((t - t0) / 500.0) for t, _, _ in c]
check("PLAY with CLICK on: the first click comes at once (no count-in)", bool(c) and 2000 <= t0 <= 2030, f"first click at {t0:.1f} ms")
check("120 BPM: a click on every beat, 500 ms apart (+-2 ms), 14 of them in 7 s, none after STOP",
      len(c) == 14 and all(abs((t - t0) - 500.0 * k) <= 2.0 for k, (t, _, _) in enumerate(c)) and not [1 for t, _, _ in c if t > 9100],
      f"{len(c)} clicks {[round(t) for t, _, _ in c][:6]}")
acc = [k for k, (t, p, z) in enumerate(c) if k % 4 == 0]
oth = [k for k, (t, p, z) in enumerate(c) if k % 4]
check("beat 1 of the bar is a different tick: louder and higher than the other beats",
      c and min(c[k][1] for k in acc) > 1.3 * max(c[k][1] for k in oth) and min(c[k][2] for k in acc) > max(c[k][2] for k in oth) + 6,
      f"accent peaks {[c[k][1] for k in acc]} zc {[c[k][2] for k in acc]}; beats {[c[k][1] for k in oth][:4]} zc {[c[k][2] for k in oth][:4]}")
vo = [v for t in (4010, 6010) for v in pr[t].split(" voices ")[1].split()]
check("no voice is used by the click: all six voices idle while it clicks", all(v.split("/")[0] == "0" for v in vo), str(vo[:6]))

# ---- REC on: the clicks go on, nothing is written into any pattern
def pat_hashes(line):
    return line.split(" ph ", 1)[1].split()[0]
ev = toggle(500) + [(1500, "probe"), (2000, f"btn {REC}"), (2100, f"btn {PLAY}"), (4010, "probe"), (6010, "probe"), (7000, f"btn {PLAY}"),
                    (7200, f"btn {REC}"), (7400, "probe")]
pr, cs, x, _ = run("rec", ev, 9000)
c = clicks(x)
gaps = [c[k + 1][0] - c[k][0] for k in range(len(c) - 1)]
check("REC on, playing: the clicks go on (a click every beat, 500 ms apart)", len(c) >= 8 and all(abs(g - 500.0) <= 3.0 for g in gaps),
      f"{len(c)} clicks {[round(g) for g in gaps]}")
check("REC was on while it played", field(pr[4010], "rec") == "1" and field(pr[6010], "rec") == "1")
check("... and nothing is recorded: the four patterns are as they were (REC on, no key pressed)",
      pat_hashes(pr[1500]) == pat_hashes(pr[6010]) == pat_hashes(pr[7400]), f"{pat_hashes(pr[1500])} {pat_hashes(pr[6010])}")

# ---- follows the tempo: the FM-1's tempo turned (HOME, SELECT), and an external MIDI clock
ev = toggle(500) + [(1200, f"btn {EDIT}"), (1300, f"btn {HOME}"), (1400, "enc 0 40"), (1800, "probe"), (2000, f"btn {PLAY}"), (9000, f"btn {PLAY}")]
pr, cs, x, _ = run("tempo", ev, 11000)
own = int(field(pr[1800], "own"))
c = clicks(x)
iv = 60000.0 / own
check(f"the FM-1's tempo turned to {own} BPM: the clicks {iv:.0f} ms apart",
      own != 120 and len(c) >= 6 and all(abs((c[k][0] - c[0][0]) - iv * k) <= 3.0 for k in range(len(c))),
      f"{len(c)} clicks {[round(t) for t, _, _ in c][:6]}")
for bpm10 in (1200, 2400, 600):
    ev = [(500, "down 8"), (1300, "up 8"), (1400, "btn 8"), (1500, "enc 5 2")]            # SYSTEM 2: CLOCK = USB
    ev += toggle(1600) + [(2500, f"clock usb {bpm10} 12000"), (3000, "midi 0xFA 0 0"), (3500, "probe")]
    pr, cs, x, _ = run(f"ext_{bpm10}", ev, 14000)
    c = clicks(x)
    iv = 600000.0 / bpm10
    ok = len(c) >= 8 and all(abs((c[k][0] - c[0][0]) - iv * k) <= 4.0 for k in range(len(c)))
    check(f"a USB MIDI clock at {bpm10 / 10:g} BPM: a click on every beat, {iv:.0f} ms apart (+-4 ms)", ok and field(pr[3500], "ext") == "1",
          f"{len(c)} clicks {[round(t) for t, _, _ in c][:6]}, ext {field(pr[3500], 'ext')}")
    check(f"... beat 1 (the clock's START) is the accent, then every fourth", len(c) >= 8 and all(
        (c[k][1] > 1.3 * c[1][1]) == (k % 4 == 0) for k in range(len(c))), str([c[k][1] for k in range(min(len(c), 8))]))

# ---- not saved: CLICK is no setting (no settings save follows the switch); it starts off at every power-on, also after settings that
# a V3.0 test build wrote with CLICK on in bit 2 (that bit goes, LATCH and VELOCITY stay)
st = OUT / "settings.bin"
st.unlink(missing_ok=True)
pr, cs, x, out = run("save_on", toggle(500) + [(3000, "probe")], 6000, env={"GRIMM_SETTINGS_OUT": str(st)})
check("CLICK on: the settings are not saved because of it (nothing to save)", cs[3000][0] == 1 and "settings saved" not in out and not st.exists())
pr, cs, x, _ = run("restart", [(1000, "probe"), (1500, f"btn {PLAY}")], 6000)
check("a restart: CLICK is off (starts off at power-on): no click", cs[1000][0] == 0 and not clicks(x))
old = bytearray(170)                            # settings as a V3.0 test build saved them: version 3, LATCH ALWAYS OFF (bit 0), CLICK (bit 2)
old[167], old[169] = 3, 5
(OUT / "old.bin").write_bytes(bytes(old))
pr, cs, x, _ = run("restart_old", [(1000, "probe"), (1500, f"btn {PLAY}")], 6000, env={"GRIMM_SETTINGS_IN": str(OUT / "old.bin")})
check("settings with the old CLICK bit: CLICK starts off, no click, LATCH ALWAYS OFF kept", cs[1000][0] == 0 and not clicks(x) and
      field(pr[1000], "new") == "1", f"{cs[1000]} new {field(pr[1000], 'new')}")

# ---- the F#3 light flashes with the clicks while CLICK is on and the sequencer plays (no SEQ held): ~60 ms a beat, ~160 ms on beat 1
def led(out):
    """F#3's light: [(ms, 0 / 1)] each change"""
    return [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"^\s*(\d+) ms  led F#3 (\d)$", out, re.M)]


def flashes(ch, lo, hi):
    """[(on ms, off ms)] of the light's on-periods starting in lo..hi"""
    res, on = [], None
    for t, v in ch:
        if v and on is None:
            on = t
        elif not v and on is not None:
            if lo <= on <= hi:
                res.append((on, t))
            on = None
    return res


ev = toggle(500) + [(2000, f"btn {PLAY}"), (6000, f"btn {PLAY}")]
pr, cs, x, out = run("flash", ev, 8000, env={"GRIMM_LED_LOG": "1"})
ch, c = led(out), clicks(x)
fl = flashes(ch, 1900, 6100)
check("CLICK on, playing: F#3 flashes with every click: 8 flashes in 4 s, each starting with its click (within 8 ms)",
      len(fl) == 8 and len(c) >= 8 and all(abs(fl[k][0] - c[k][0]) <= 8 for k in range(8)),
      f"flashes {fl} clicks {[round(t) for t, _, _ in c][:8]}")
check("... 60 ms on a beat, 160 ms on beat 1 of the bar (+-12 ms)",
      len(fl) == 8 and all(abs((o - n) - (160 if k % 4 == 0 else 60)) <= 12 for k, (n, o) in enumerate(fl)), str([o - n for n, o in fl]))
check("... and dark between them, and after STOP (none past 6.2 s)", not flashes(ch, 6200, 8000) and ch[-1][1] == 0, str(ch[-3:]))
pr, cs, x, out = run("flash_off", [(2000, f"btn {PLAY}"), (5000, f"btn {PLAY}")], 6000, env={"GRIMM_LED_LOG": "1"})
check("CLICK off, playing: F#3 does not flash", not led(out), str(led(out)[:4]))
pr, cs, x, out = run("flash_stop", toggle(500) + [(3000, "probe")], 5000, env={"GRIMM_LED_LOG": "1"})
check("CLICK on, stopped: F#3 does not flash (lit only while SEQ is held: 0.1 s .. 0.8 s)", [t for t, v in led(out) if t > 900] == [] and cs[3000][2] >> 1 & 1 == 0,
      str(led(out)))

# ---- who wins on F#3: a note's light (the key held, the sequencer's or the arp's note on the selected track) keeps it on
# steadily, the flash is only seen where nothing else lights it; SEQ held: lit as before; GLO / SAVE / FX held: no flash
def steady(name, ev2, lo, hi, what):
    pr, cs, x, out = run(name, toggle(500) + [(2000, f"btn {PLAY}")] + ev2 + [(6000, f"btn {PLAY}")], 8000, env={"GRIMM_LED_LOG": "1"})
    ch = led(out)
    before = [v for t, v in ch if t <= lo][-1:]      # (the light at lo, and any change after it)
    check(what, before == [1] and not [1 for t, v in ch if lo < t < hi], str(ch))
    return ch


steady("note_held", [(3000, f"keys {F_SHARP3}"), (5000, "keys 0")], 3100, 4950,
       "F#3 held as a note (CLICK on, playing): its light stays on, the beats do not blink it")
steady("seq_held", [(3000, f"down {SEQ}"), (5000, f"up {SEQ}")], 3100, 4950,
       "SEQ held (CLICK on, playing): F#3 lit steadily, as before")
for nm, b in (("glo", 7), ("save", 9)):
    pr, cs, x, out = run(f"hold_{nm}", toggle(500) + [(2000, f"btn {PLAY}"), (3000, f"down {b}"), (5000, f"up {b}")], 7000, env={"GRIMM_LED_LOG": "1"})
    check(f"{nm.upper()} held (CLICK on, playing): F#3 does not flash (the keys mean something else)", not [1 for t, v in led(out) if 3100 < t < 5000],
          str([(t, v) for t, v in led(out) if 3000 < t < 5100]))

print(f"click test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
