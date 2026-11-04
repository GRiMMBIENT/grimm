#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0's LEVELS page in host mode (tests/run_host.sh): HOME's 4th page, after MIXER 2 (HOME > LEVELS), its KNOB 1..4
the four tracks' LEVEL directly: the page is in HOME's tap round and the round still returns to HOME (the SYSTEM pages
and the other buttons' rounds are as they were); each knob moves only its own track's level and the header says "T3 VOL
079" as HOME does; the value is HOME's VOLUME and MIXER 1's LEVEL tile (one variable); it works while the sequencer
plays; a MIDI LEARN link or a CC moves a level and the page follows; MIDI LEARN on a LEVELS knob makes MIXER 1's LEVEL
link of that track; the levels are saved with the project.
  python3 tests/levels_test.py [build/host/grimm]"""
import os
import subprocess
import sys
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/levels")
OUT.mkdir(parents=True, exist_ok=True)
EDIT, HOME, PLAY = 6, 8, 12
ALGORITHM, KNOB1 = 1, 2
PG_HOME, PG_MIX1, PG_MIX2, PG_SYSTEM, PG_SYS2, PG_SYS3, PG_LEVELS = 7, 8, 9, 10, 11, 27, 30
BOOT = 6000                                    # (the boot screen runs 5.6 s: the slabs and the header exist after it; the times below are after it)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  levels: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, ev, end, env=None):
    """-> {ms: probe line}, {ms: slabvals line}, the output"""
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t + BOOT} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end + BOOT} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=dict(os.environ, **(env or {}))).stdout
    pr, sl = {}, {}
    for ln in out.splitlines():
        if ln.startswith("probe "):
            pr[int(ln.split()[1]) - BOOT] = ln
        elif ln.startswith("slabvals "):
            sl[int(ln.split()[1]) - BOOT] = ln
    return pr, sl, out


def page(pl):
    return int(pl.split(" ui page ")[1].split()[0])


def levels(pl):                                  # T1..T4's LEVEL (0..127), as the project holds them
    return [int(g.split(",")[0]) for g in pl.split(" mix ")[1].split()[0].split("/")]


def hd(pl):
    return pl.split(" hd ")[1].split()[0]


def slabs(sl):                                   # [(label, value, accent, fill)]
    return [tuple(x.split(":")) for x in sl.split(" head")[0].split()[2:]]


def head(sl):
    return sl.split(" head ")[1].strip().split("|")


def tap(t, b=HOME):
    return [(t, f"btn {b}")]


def pc(v):                                       # ui_pc: 0..127 -> 0..100 %
    return (v * 100 + 63) // 127


# ---- HOME's round: HOME, MIXER 1, MIXER 2, LEVELS, HOME; the other rounds as they were
ev = [(500, "probe")]
for k in range(4):
    ev += tap(600 + 200 * k) + [(700 + 200 * k, "probe")]
pr, sl, _ = run("round", ev, 2000)
seq = [page(pr[500])] + [page(pr[700 + 200 * k]) for k in range(4)]
check("HOME's tap round: HOME, MIXER 1, MIXER 2, LEVELS, then HOME again", seq == [PG_HOME, PG_MIX1, PG_MIX2, PG_LEVELS, PG_HOME], str(seq))
ev = [(500, "down 8"), (1300, "up 8"), (1400, "probe")] + tap(1500) + [(1600, "probe")] + tap(1700) + [(1800, "probe")] + tap(1900) + [(2000, "probe")]
ev += tap(2200, EDIT) + tap(2300) + [(2400, "probe")]
pr, sl, _ = run("round2", ev, 2600)
check("HOME held: SYSTEM, SYSTEM 2, SYSTEM 3 and round again, no LEVELS among them",
      [page(pr[t]) for t in (1400, 1600, 1800, 2000)] == [PG_SYSTEM, PG_SYS2, PG_SYS3, PG_SYSTEM], str([page(pr[t]) for t in (1400, 1600, 1800, 2000)]))
check("EDIT, then HOME: HOME's first page", page(pr[2400]) == PG_HOME)
ev = [(500, "probe")] + [x for k in range(3) for x in tap(600 + 200 * k, 10)] + [(1300, "probe")] + [x for k in range(3) for x in tap(1400 + 200 * k, 11)] + [(2100, "probe")]
pr, sl, _ = run("round3", ev, 2300)
check("the ARP and SEQ buttons' rounds are as they were (ARP 1 page, SEQ's GRID and PATTERN SETTINGS)",
      page(pr[500]) == PG_HOME and page(pr[1300]) == 14 and page(pr[2100]) == 15, f"{page(pr[1300])} {page(pr[2100])}")

# ---- the slabs, the header, the knobs: each moves only its own track's level
base = [(600, "btn 8"), (800, "btn 8"), (1000, "btn 8"), (1200, "probe")]
pr0, sl0, _ = run("base", base, 1300)
l0 = levels(pr0[1200])
check("LEVELS opens with the four tracks' levels as HOME's slabs: T1..T4 and their LEVEL in %, no track in the accent",
      page(pr0[1200]) == PG_LEVELS and [s[0] for s in slabs(sl0[1200])] == ["T1_VA", "T2_VA", "T3_VA", "T4_VA"] and
      [s[1] for s in slabs(sl0[1200])] == [f"{pc(v):03d}" for v in l0] and all(s[2] == "0" for s in slabs(sl0[1200])),
      f"{slabs(sl0[1200])} levels {l0}")
check("... the header names the page: HOME > LEVELS", head(sl0[1200])[0] == "HOME" and head(sl0[1200])[1] == "LEVELS", str(head(sl0[1200])))
for k in range(4):
    ev = base[:3] + [(1100, f"enc {KNOB1 + k} -12"), (1150, "probe")]
    pr, sl, _ = run(f"knob{k}", ev, 1400)
    lv = levels(pr[1150])
    others = [lv[j] == l0[j] for j in range(4) if j != k]
    want = f"T{k + 1}_VOL_{pc(lv[k]):03d}"
    check(f"KNOB {k + 1} moves T{k + 1}'s level only ({l0[k]} -> {lv[k]}), the others stay; the header says T{k + 1} VOL {pc(lv[k]):03d}",
          lv[k] < l0[k] and all(others) and hd(pr[1150]) == want and want in head(sl[1150])[1] and slabs(sl[1150])[k][1] == f"{pc(lv[k]):03d}",
          f"levels {lv} hd {hd(pr[1150])} head {head(sl[1150])}")
# turned up past the top, down past the bottom: within 0..127, 0 % .. 100 %
pr, sl, _ = run("range", base[:3] + [(1100, f"enc {KNOB1 + 1} -400"), (1150, "probe"), (1200, f"enc {KNOB1 + 1} 700"), (1250, "probe")], 1400)
check("a knob turned all the way down and up: 0 % and 100 %, never out of range", levels(pr[1150])[1] == 0 and levels(pr[1250])[1] == 127 and
      slabs(sl[1150])[1][1] == "000" and slabs(sl[1250])[1][1] == "100", f"{levels(pr[1150])} {levels(pr[1250])}")

# ---- one value: HOME's VOLUME and MIXER 1's LEVEL are the same level (the selected track's)
ev = [(300, f"enc {ALGORITHM} 2")] + base[:3] + [(1100, f"enc {KNOB1 + 2} -20"), (1150, "probe"), (1160, "btn 8"), (1300, "probe")]   # T3 selected; LEVELS, T3 turned; HOME
pr, sl, _ = run("same", ev, 1500)
lv = levels(pr[1150])
check("T3 turned on LEVELS: HOME's slab for T3 (selected, in the accent) shows the same level",
      page(pr[1300]) == PG_HOME and slabs(sl[1300])[2][1] == f"{pc(lv[2]):03d}" and slabs(sl[1300])[2][2] == "1" and lv[2] != l0[2],
      f"{slabs(sl[1300])} levels {lv}")
ev = [(300, f"enc {ALGORITHM} 2"), (500, f"enc {KNOB1} -30"), (600, "probe")] + tap(700) + [(800, f"enc {KNOB1} -10"), (900, "probe")] + tap(1000) + tap(1100) + [(1400, "probe")]
pr, sl, _ = run("same2", ev, 1600)                # T3 selected; HOME's VOLUME turned; MIXER 1's LEVEL tile turned; MIXER 2, LEVELS
a, b, c = (levels(pr[t]) for t in (600, 900, 1400))
check("T3's level turned on HOME (VOLUME), then on MIXER 1 (LEVEL tile): one value; LEVELS shows it (the others untouched)",
      page(pr[900]) == PG_MIX1 and page(pr[1400]) == PG_LEVELS and a[2] < l0[2] and b[2] < a[2] and c == b and
      [x for i, x in enumerate(b) if i != 2] == [x for i, x in enumerate(l0) if i != 2] and slabs(sl[1400])[2][1] == f"{pc(b[2]):03d}",
      f"{l0} {a} {b} {slabs(sl[1400])}")

# ---- while the sequencer plays
ev = [(300, f"btn {PLAY}")] + base[:3] + [(1100, f"enc {KNOB1} -25"), (1150, "probe")]
pr, sl, _ = run("play", ev, 1400)
check("while the sequencer plays: LEVELS works the same (T1 moved, still playing)", pr[1150].split(" play ")[1].split()[0] == "1" and levels(pr[1150])[0] < l0[0] and
      levels(pr[1150])[1:] == l0[1:], f"{levels(pr[1150])}")

# ---- MIDI LEARN and CCs: a link to MIXER 1's LEVEL of T3 (cc 20) moves T3's level and LEVELS follows; LEARN on a LEVELS knob makes that link
st = bytearray(170)
st[6], st[167] = 1, 3                            # one link: CC 20 -> MIXER 1 (page 8), knob 0 (LEVEL), T3, x 0
st[7:12] = bytes([20, PG_MIX1, 0, 2, 0])
(OUT / "link.bin").write_bytes(bytes(st))
ev = base[:3] + [(1100, "midi 0xB0 20 0"), (1200, "probe"), (1300, "midi 0xB0 20 127"), (1400, "probe")]
pr, sl, _ = run("cc", ev, 1500, env={"GRIMM_SETTINGS_IN": str(OUT / "link.bin")})
check("a CC linked to MIXER 1's LEVEL of T3 moves it with LEVELS open: the page follows (000, then 100 %), the others stay",
      levels(pr[1200])[2] == 0 and levels(pr[1400])[2] == 127 and slabs(sl[1200])[2][1] == "000" and slabs(sl[1400])[2][1] == "100" and
      levels(pr[1200])[:2] == l0[:2] and levels(pr[1200])[3] == l0[3], f"{levels(pr[1200])} {slabs(sl[1200])[2]}")
ev = [(500, "down 8"), (1300, "up 8"), (1400, "btn 8"), (1500, f"enc {KNOB1} 1")]       # SYSTEM 2: MIDI LEARN := LEARN
ev += tap(1700, EDIT) + tap(1800) + tap(1900) + tap(2000) + tap(2100)                      # EDIT, then HOME: HOME, MIXER 1, MIXER 2, LEVELS
ev += [(2200, "probe"), (2300, f"enc {KNOB1 + 2} 1"), (2400, "probe"), (2500, "midi 0xB0 22 90"), (2600, "probe"),
       (2700, "midi 0xB0 22 0"), (2800, "probe"), (2900, "midi 0xB0 22 127"), (3000, "probe")]
pr, sl, out = run("learn", ev, 3200)
nl = lambda t: int(pr[t].split(" links ")[1].split()[0])
check("MIDI LEARN: a LEVELS knob (KNOB 3) turned says MOVE A CC; the first CC is linked (one link) and moves nothing yet",
      page(pr[2200]) == PG_LEVELS and "MOVE_A_CC" in head(sl[2400])[1] and nl(2400) == 0 and nl(2600) == 1 and levels(pr[2600]) == levels(pr[2400]),
      f"page {page(pr[2200])} head {head(sl[2400])} links {nl(2400)} {nl(2600)} levels {levels(pr[2400])} {levels(pr[2600])}")
check("... then that CC moves T3's level only (0, then 127), the page follows",
      levels(pr[2800])[2] == 0 and levels(pr[3000])[2] == 127 and levels(pr[3000])[:2] == levels(pr[2200])[:2] and
      levels(pr[3000])[3] == levels(pr[2200])[3] and slabs(sl[2800])[2][1] == "000" and slabs(sl[3000])[2][1] == "100",
      f"{levels(pr[2800])} {levels(pr[3000])} {slabs(sl[2800])[2]}")

# ---- saved with the project, as LEVEL is
ev = base[:3] + [(1100, f"enc {KNOB1} -20"), (1150, f"enc {KNOB1 + 3} -40"), (1200, "probe")]
pr, sl, out = run("save", ev, 30000, env={"GRIMM_SAVE_OUT": str(OUT / "saves.bin")})
lv = levels(pr[1200])
pr2, sl2, _ = run("load", [(1000, "probe")], 1200, env={"GRIMM_SAVE_IN": str(OUT / "saves.bin")})
check("the levels set on LEVELS are saved with the project (the autosave) and come back after a restart",
      "flash sector" in out and levels(pr2[1000]) == lv and lv[0] < l0[0] and lv[3] < l0[3] and lv[1:3] == l0[1:3], f"{lv} -> {levels(pr2[1000])}")

print(f"levels test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
