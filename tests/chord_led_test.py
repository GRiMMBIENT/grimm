#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0 item 4 in host mode (tests/run_host.sh): chord mode is always visible.
  - the SEL button's light: LIT and steady = the selected track's chord mode is on (on any page, the SCALE / CHORD pages too); the
    SCALE or CHORD page open with chord off = a SLOW BLINK of the full light (0.8 s lit, 0.8 s dark, from the page's opening),
    never fast: SEL's LED is written once a pass, with its final value (a lit-then-dark write in one pass flickered on the
    device); DARK = neither; it follows chord on / off and the selected track. The blink's TIME is kept by the key-scan tick
    (hal/fm1_input.h, FM1_BLINK_MS; here the host's clock stands in for it): the main loop only asks for the blink, the same value
    every pass, and is tested for that
  - HOME: a small "CHD" tag under the track's name while that track's chord mode is on (T1 .. T4 each its own), with CUT / DRY
    under it when both show (the probe's slab warning: +4 for CHD); the screen's pixels read too
  python3 tests/chord_led_test.py [build/host/grimm]"""
import os
import re
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/chordled")
OUT.mkdir(parents=True, exist_ok=True)
SEL, HOME = 3, 8
ALGO, K2, K4 = 1, 3, 5                          # encoders: ALGORITHM (the track), KNOB 2 (CUTOFF), KNOB 4 (FX)
bad = 0
litw, blw = [], []                              # SEL's lit layer, and the blink asked for: every write that changed them


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  chord led: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def script(name, ev, end):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    return path


def run(name, ev, end):
    path = script(name, ev, end)
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=dict(os.environ, GRIMM_LED_LOG="1")).stdout
    leds = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"^\s*(\d+) ms  led SEL (\d)$", out, re.M)]
    global litw, blw
    litw = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"^\s*(\d+) ms  led SELLIT (\d)$", out, re.M)]
    blw = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r"^\s*(\d+) ms  led SELBL (\d)$", out, re.M)]
    probes = {int(ln.split()[1]): ln for ln in out.splitlines() if ln.startswith("probe ")}
    return leds, probes


def at(leds, t):                                # SEL's light at t ms: the last change before it
    v = 0                                       # (dark at power-on: only changes are logged)
    for ms, s in leds:
        if ms <= t:
            v = s
    return v


def hold(t, n=SEL):                             # the button held 0.7 s, let go
    return [(t, f"down {n}"), (t + 700, f"up {n}")]


def warn(p, i):                                 # the probe's slab i: "T1_VA:5" -> 5
    return int(p.split(" slabs ")[1].split(" mute ")[0].split()[i].split(":")[1])


# ---- the light. T1 selected, chord off, HOME at first
ev = [(6100, f"btn {SEL}"),                     # SEL tapped: SCALE page opens, chord off: blink (lit first)
      (6400, f"btn {SEL}"),                     # (SCALE -> CHORD page: the same blink goes on)
      (6700, f"btn {HOME}"),                    # HOME: dark
      *hold(7000),                              # SEL held and let go: T1's chord mode on, the page stays HOME: LIT
      (7900, f"btn {SEL}"),                     # SCALE page open, chord on: LIT
      (8200, f"btn {SEL}"),                     # CHORD page: LIT
      (8500, f"btn {HOME}"),
      (8800, f"enc {ALGO} 1"),                  # T2 selected (chord off): dark
      (9100, f"btn {SEL}"),                     # T2 on the SCALE page, chord off: blink
      (9400, f"btn {HOME}"),
      (9700, f"enc {ALGO} -1"),                 # T1 again: LIT
      *hold(10000),                             # chord off: dark
      (10900, f"btn {SEL}"), (11200, f"btn {SEL}"), (11500, f"btn {HOME}"),   # SCALE, CHORD pages, HOME: blink, blink, dark
      (11800, f"enc {ALGO} 3"),                 # T4
      *hold(12100),                             # T4's chord on: LIT
      (13000, f"enc {ALGO} -3"),                # T1 (off): dark
      (13300, f"enc {ALGO} 3"),                 # T4: LIT again
      ]
leds, _ = run("led", ev, 13800)
want = [(6000, 0), (6300, 1), (6600, 1), (6900, 0), (7800, 1), (8100, 1), (8400, 1), (8700, 1), (9000, 0), (9300, 1),
        (9600, 0), (9900, 1), (10800, 0), (11100, 1), (11400, 1), (11700, 0), (11900, 0), (12900, 1), (13200, 0), (13500, 1)]
got = [(t, at(leds, t)) for t, _ in want]
check("SEL: dark at power-on; lit (the blink's first half) on the SCALE and CHORD pages with chord off; dark on HOME; lit when chord "
      "mode is on (HOME, SCALE and CHORD pages); follows ALGORITHM (T2 off: dark / blink on SCALE, T1 on again: lit); lit again for "
      "T4; dark when off", got == want, f"got {got}")
check("SEL: chord on, on the SCALE / CHORD pages: steady, no change at all (7800 .. 8700 ms)",
      not [x for x in leds if 7800 <= x[0] <= 8700], str([x for x in leds if 7800 <= x[0] <= 8700]))
check("SEL: only the changes are logged, in order (no flicker between the states in one frame)",
      all(a[1] != b[1] for a, b in zip(leds, leds[1:])) and len(leds) <= 30, str(leds))

# ---- the blink: the SCALE page open for 5 s, chord off (SEL tapped at 6100): lit 0.8 s, dark 0.8 s ...; then chord on: steady
ev = [(6100, f"btn {SEL}"), (11100, f"btn {HOME}"), *hold(11500), (12400, f"btn {SEL}"), (15500, f"btn {HOME}")]
leds, _ = run("pulse", ev, 16000)
phase = [at(leds, 6100 + 800 * k + 400) for k in range(6)]
check("SEL blink: SCALE page open, chord off: lit, dark, lit, dark, lit, dark (0.8 s each, from the page's opening)",
      phase == [1, 0, 1, 0, 1, 0], str(phase))
edges = [x for x in leds if 6000 < x[0] <= 11000]
check("SEL blink: the only writes are the blink's edges, 0.8 s apart, from the opening (no fast flicker, no lit blip)",
      [s for _, s in edges] == [1, 0, 1, 0, 1, 0, 1] and all(abs(t - (6100 + 800 * k)) <= 30 for k, (t, _) in enumerate(edges)), str(edges))
check("SEL blink: the main loop never writes the light while chord mode is off and asks for the blink once (on at the page's opening, off at "
      "HOME): the key-scan tick keeps the time (hal/fm1_input.h), nothing in the loop toggles it",
      [w for w in litw if w[0] < 11500] == [] and [v for t, v in blw if t < 11500] == [1, 0] and 6100 <= blw[0][0] <= 6200 and
      11100 <= blw[1][0] <= 11200, str(litw) + str(blw))
after = [x for x in leds if x[0] > 11000]
check("SEL: chord on (hold at 11500): lit once, steady for 3 s on HOME and the SCALE page (no change after)",
      [s for _, s in after] == [0, 1] and 12100 <= after[1][0] <= 12500, str(after))

# ---- the tag: T1's chord mode on (SEL held), CUT, DRY with it; T2 .. T4 their own
ev = [(6100, "probe"),
      *hold(6200), (7000, "probe"),             # T1: CHD
      (7100, f"enc {K2} -127"), (7300, "probe"),   # + the filter closed: CHD, CUT
      (7400, f"enc {K2} 127"), (7500, f"enc {K4} -100"), (7700, "probe"),   # + FX 0 with a send up: CHD, DRY
      (7800, f"enc {K4} 100"), (8000, "probe"),     # back to CHD only
      (8100, f"enc {ALGO} 2"), *hold(8200), (9000, "probe"),   # T3's too
      (9100, f"enc {ALGO} -2"), *hold(9200), (10000, "probe")]   # T1 off: T3 only
_, p = run("tag", ev, 10100)
w = {t: [warn(p[t], i) for i in range(4)] for t in (6100, 7000, 7300, 7700, 8000, 9000, 10000)}
check("HOME: no tag at power-on; chord on for T1: CHD only on T1's slab (probe +4)", w[6100] == [0, 0, 0, 0] and w[7000] == [4, 0, 0, 0], str(w))
check("HOME: CHD with CUT (5), with DRY (6), back to CHD alone (4); the others untouched", w[7300] == [5, 0, 0, 0] and w[7700] == [6, 0, 0, 0] and w[8000] == [4, 0, 0, 0], str(w))
check("HOME: each track its own: T3's chord on as well (T1 + T3), then T1 off (T3 alone)", w[9000] == [4, 0, 4, 0] and w[10000] == [0, 0, 4, 0], str(w))

# ---- the screen: the tag's pixels (red, 180 .. 188 under the name; with CUT / DRY the second line 188 .. 196)
ev = [(7000, f"down {SEL}"), (7700, f"up {SEL}"),   # T1's chord on
      (8800, f"enc {K2} -127"),                 # + CUT
      (9800, f"enc {ALGO} 1")]                  # (T2 selected: the tags are the tracks' own, not the selected one's)
path = script("pix", ev, 10300)
subprocess.run([EXE, str(OUT / "pix_f"), str(path), "4"], capture_output=True, check=True)


def px(ms, x0, y0, y1):                         # red pixels of the slab at x0 (56 wide), rows y0 .. y1
    f = open(OUT / f"pix_f-{ms // 250:05d}.ppm", "rb").read()
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", f)
    w, d = int(m.group(1)), f[m.end():]
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x0 + 56):
            r, g, b = d[3 * (y * w + x):3 * (y * w + x) + 3]
            n += r > 150 and g < 110
    return n


# (the selected track's segments are red too: only the rows between the name and the segments are read)
a = {t: (px(t, 3, 180, 188), px(t, 3, 188, 196), px(t, 62, 180, 188)) for t in (6750, 8500, 9500, 10250)}
check("screen: no tag before; CHD (red under T1's name) after; T2 has none", a[6750][0] == 0 and a[8500][0] > 20 and a[8500][2] == 0, str(a))
check("screen: CHD + CUT: CHD on the first line, CUT under it", a[9500][0] > 20 and a[9500][1] > 20 and a[9500][2] == 0, str(a))
check("screen: T2 selected: T1 keeps its tags (the tags are the tracks' own)", a[10250][0] > 20 and a[10250][1] > 20, str(a))

sys.exit(1 if bad else 0)
