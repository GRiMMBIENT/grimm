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
    return [tuple(x.split(":")) for x in sl.split(" meter ")[0].split()[2:]]


def sx(t, *b):                                   # an editor frame in (docs/EDITOR_PROTOCOL.md; host mode's "sx")
    return (t, "sx " + " ".join(f"{x:02X}" for x in (0x7D, 0x47, 0x52) + b))


def fx_set(t, i, v):                             # 25 FX, op 1: value i := v (28 + 4 t + 3: T1.. DRIVE send; 20 + k: the DRIVE page's four; 29: T1's REV send)
    return sx(t, 25, 1, i, v)


def tk(sl):                                      # the meters' readings since the last probe (Q15 of the mix), the mix's peak
    return [int(x) for x in sl.split(" tk ")[1].split()[0].split(",")], int(sl.split(" mixpk ")[1].split()[0])


def meter(sl):                                   # the four meters' cells lit (0..8)
    return [int(x) for x in sl.split(" meter ")[1].split()[0].split(",")]


def lcdpx(sl):                                   # the pixels sent to the screen so far
    return int(sl.split(" lcdpx ")[1].split()[0])


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


# ---- the live meter (right half of each column): rises with a playing track, falls back smoothly when it stops
ev = base[:3] + [(1400, "probe"), (1500, "keys 1"), (1900, "probe"), (2900, "probe"), (3000, "keys 0")]
ev += [(t, "probe") for t in (3100, 3200, 3300, 3400, 3600, 3900, 4500, 6000, 9000)]
pr, sl, _ = run("meter", ev, 9200)
m0, mup, mpk = meter(sl[1400]), meter(sl[1900]), meter(sl[2900])
fall = [meter(sl[t])[0] for t in (3100, 3200, 3300, 3400, 3600, 3900, 4500, 6000, 9000)]
check("the meter is empty with nothing playing, rises with a playing track (T1 only: the others stay at 0)",
      m0 == [0, 0, 0, 0] and mup[0] > 0 and mup[1:] == [0, 0, 0] and mpk[0] >= mup[0] and mpk[1:] == [0, 0, 0], f"{m0} {mup} {mpk}")
check("... and falls back when the sound stops: never up again, down to 0, by steps (not from the top to nothing in one)",
      all(a >= b for a, b in zip(fall, fall[1:])) and fall[-1] == 0 and fall[0] <= mpk[0] and any(0 < v < mpk[0] for v in fall) and
      len({v for v in fall}) >= 3, f"peak {mpk[0]} then {fall}")
check("the setting half does not follow the sound: LEVEL as set while the meter moves", levels(pr[1400]) == levels(pr[2900]) == levels(pr[3900]))

# ---- the picture itself: both halves, the three colours (read from ui.c's one place), a muted track
import re
ui = open("firmware/src/ui.c", encoding="utf-8").read()


def col(name):
    m = re.search(r"#define %s RGB\(0x(..), 0x(..), 0x(..)\)" % name, ui)
    r, g, b = (int(x, 16) for x in m.groups())
    return ((r >> 3) * 255 // 31, (g >> 2) * 255 // 63, (b >> 3) * 255 // 31)       # (as host mode writes a pixel)


C = {n: col(n) for n in ("UI_D", "UI_M", "UI_L", "UI_METER_NORMAL", "UI_METER_LOUD", "UI_METER_HOT")}
LOUD_FROM, HOT_FROM = (int(re.search(r"#define %s (\d+)" % n, ui).group(1)) for n in ("UI_METER_LOUD_FROM", "UI_METER_HOT_FROM"))
check("the three meter colours are named once (green / yellow / red) and chosen by height in one function", all(
      len(re.findall(r"\b%s\b" % n, ui)) == 2 for n in ("UI_METER_NORMAL", "UI_METER_LOUD", "UI_METER_HOT")) and
      len(re.findall(r"\bui_meter_col\(", ui)) == 2 and len({C["UI_METER_NORMAL"], C["UI_METER_LOUD"], C["UI_METER_HOT"]}) == 3 and
      0 < LOUD_FROM < HOT_FROM < 8)


FROM = [int(x) for x in re.search(r"UI_METER_FROM\[8\] = \{([^}]*)\}", ui).group(1).split(",")]
cells = lambda v: sum(1 for f in FROM if v >= f)
dbfs = lambda v: 20 * __import__("math").log10(max(v, 1) / 32767.0)
check("the scale: eight segments, rising, from -46 to -4.5 dBFS of the mix; yellow from -12, red from -6 (two such tracks fill the output limiter, which starts at -0.09)",
      len(FROM) == 8 and FROM == sorted(FROM) and abs(dbfs(FROM[0]) + 46) < 0.5 and abs(dbfs(FROM[-1]) + 4.5) < 0.2 and
      abs(dbfs(FROM[LOUD_FROM]) + 12) < 0.2 and abs(dbfs(FROM[HOT_FROM]) + 6) < 0.2, str(FROM))

# the factory sounds: a 4-note chord each on T1, LEVEL 100, its REV send off (the mix is then the track's own); the meter's reading against the mix itself
ev = [fx_set(1000, 29, 0)] + tap(1100) + tap(1200) + tap(1300)      # (LEVELS shown: the meter measures only while a meter is on screen)
t, ends = 1500, []
for i in range(44):
    ev.append((t, "enc 6 1")), ev.append((t + 280, "probe"))
    ev += [(t + 300, f"midi 0x90 {n} 100") for n in (48, 55, 60, 64)]
    ev.append((t + 1600, "probe")), ends.append(t + 1600)
    for n in (48, 55, 60, 64):
        ev += [(t + 1650, f"midi 0x80 {n} 0"), (t + 1680, f"midi 0x90 {n} 100"), (t + 1710, f"midi 0x80 {n} 0")]
    t += 2500
pr, sl, _ = run("factory", ev, t + 500)
rd = [(tk(sl[e])[0][0], tk(sl[e])[1]) for e in ends]
ratio = [a / b for a, b in rd if b > 2000]
srt = sorted(a for a, _ in rd)
nred = sum(1 for a in srt if cells(a) > HOT_FROM)
nloud = sum(1 for a in srt if cells(a) >= LOUD_FROM + 1)
print(f"  info  levels: the 44 factory sounds as a 4-note chord on T1 (LEVEL 100): the track's part of the mix, dBFS: quietest {dbfs(srt[0]):.1f}, "
      f"median {dbfs(srt[22]):.1f}, quarter-loudest {dbfs(srt[33]):.1f}, loudest {dbfs(srt[-1]):.1f}; {nred} show red, {nloud} yellow or red; "
      f"meter / the mix itself: {min(ratio):.2f} .. {max(ratio):.2f}")
check("the factory sounds read as measured: a spread from quiet to near full scale, about a third of the 4-note chords red, over half yellow or more",
      dbfs(srt[0]) < -18 and -11 < dbfs(srt[22]) < -7 and dbfs(srt[-1]) > -2 and 8 <= nred <= 20 and 20 <= nloud <= 40, f"{srt}")
check("... the reading follows the mix itself (the track's own part of it): within -2.5 dB and +0.5 dB of the real peak",
      min(ratio) >= 0.75 and max(ratio) <= 1.06, f"{min(ratio):.2f} .. {max(ratio):.2f}")


def chord_run(name, extra, notes=(29, 36), wave=2, hold=1500, pre=()):
    """T1: VA (wave 2 = SUPER) two low notes held; extra editor frames / events; -> tk, mix peak, output peak, meter cells of T1 at the end"""
    ev = [(500, "btn 6"), (600, f"enc {KNOB1} {wave}")] + tap(700) + list(pre) + tap(800) + tap(850) + tap(900) + list(extra)   # (EDIT: the source; HOME, its knobs; then MIXER 1, 2, LEVELS)
    ev += [(2000, f"midi 0x90 {n} 100") for n in notes] + [(2000 + hold - 50, "probe"), (2000 + hold + 400, "probe")]
    pr, sl, _ = run(name, ev, 2000 + hold + 600)
    line = sl[2000 + hold + 400]
    return tk(line)[0][0], tk(line)[1], int(line.split(" outpk ")[1].split()[0]), cells(tk(line)[0][0])


# the DRIVE (V3.0: a send effect): two supersaw notes low down, T1's DRIVE send up; what is heard is the DRIVE page's (AMOUNT, TYPE, TONE, MIX = the
# return): the meter reads the track's part of the mix, the driven sound included; AMOUNT 0 or MIX 0 or no send: the dry sound alone
dry = chord_run("drv_dry", [fx_set(100, 29, 0)])
check("two low supersaw notes, no drive: about -12 dBFS, green only (4 segments)", -14 < dbfs(dry[0]) < -10 and dry[3] == LOUD_FROM, f"{dry}")
page = lambda ty, amt=127, mix=127: [fx_set(105, 20, amt), fx_set(110, 21, ty), fx_set(115, 22, 127), fx_set(120, 23, mix)]
for nm, extra in (("send 100 %, AMOUNT 0", [fx_set(125, 31, 127)] + page(0, 0)), ("send 100 %, AMOUNT 100 %, MIX 0", [fx_set(125, 31, 127)] + page(0, 127, 0)),
                  ("no send, AMOUNT 100 %", page(0))):
    q = chord_run("drv_none", [fx_set(100, 29, 0)] + extra)
    check(f"DRIVE: {nm}: nothing heard, the meter reads the dry sound", abs(q[0] - dry[0]) <= 0.03 * dry[0] and q[3] == dry[3], f"{q} dry {dry}")
for ty, nm in enumerate(("SOFT", "FUZZ", "CLIP", "FOLD")):
    for amt in (64, 127):
        q = chord_run(f"drv_{nm}{amt}", [fx_set(100, 29, 0), fx_set(125, 31, 127)] + page(ty, amt))
        check(f"DRIVE {nm}, AMOUNT {amt * 100 // 127} %, send 100 %: the track's part of the mix {dbfs(q[0]):.1f} dBFS ({q[3]} segments), as the mix itself {dbfs(q[1]):.1f}",
              0.9 <= q[0] / q[1] <= 1.05 and q[0] > 1.8 * dry[0] and q[3] >= {("SOFT", 64): 8, ("SOFT", 127): 8, ("FUZZ", 64): 8, ("FUZZ", 127): 7,
                                                                          ("CLIP", 64): 7, ("CLIP", 127): 6, ("FOLD", 64): 7, ("FOLD", 127): 8}[(nm, amt)], f"{q} dry {dry}")
half = chord_run("drv_half", [fx_set(100, 29, 0), fx_set(125, 31, 60)] + page(1))
full = chord_run("drv_full", [fx_set(100, 29, 0), fx_set(125, 31, 127)] + page(1))
check("... the send is how much of the track goes into the drive: half of it reads lower", dry[0] < half[0] < full[0] * 1.01, f"{dry[0]} {half[0]} {full[0]}")

# the measure works only while a meter is on screen (LEVELS, MIXER 1): HOME shows none, so the audio does none of it
ev = [fx_set(100, 29, 0)] + [(2000, f"midi 0x90 {n} 100") for n in (48, 55, 60, 64)] + [(2100, "probe"), (3300, "probe")]
pr, sl, _ = run("tk_off", ev, 3500)
check("on HOME (no meter shown) the meter's measure is off: the track sounds (the mix peaks) and the reading stays 0", tk(sl[3300])[1] > 4000 and tk(sl[3300])[0] == [0, 0, 0, 0],
      f"{tk(sl[3300])}")

# after its own chain: LEVEL, the filter
base_rd = chord_run("chain0", [fx_set(100, 29, 0)], notes=(48, 55, 60, 64), wave=0)
lvl_rd = chord_run("chain1", [fx_set(100, 29, 0)], notes=(48, 55, 60, 64), wave=0, pre=[(720, f"enc {KNOB1} -39")])
cut_rd = chord_run("chain2", [fx_set(100, 29, 0)], notes=(48, 55, 60, 64), wave=0, pre=[(720, f"enc {KNOB1 + 1} -127")])
check("after its LEVEL (HOME's VOLUME turned down to about half: the reading about half) and its filter (CUTOFF closed: almost nothing)",
      0.42 < lvl_rd[0] / base_rd[0] < 0.62 and cut_rd[0] < 0.1 * base_rd[0], f"open {base_rd} level down {lvl_rd} cutoff closed {cut_rd}")


def ppm(path):
    d = open(path, "rb").read()
    i = d.index(b"\n") + 1
    return d[i:]


def pix(img, x, y):
    o = (y * 240 + x) * 3
    return tuple(img[o:o + 3])


def near(a, b):
    return all(abs(p - q) <= 3 for p, q in zip(a, b))


# T1: four notes, its DRIVE send up and its LEVEL raised to the top (loud), T2: one note, T3: one note but muted (GLO + key), T4: nothing
mid = [(0, "midi 0x92 60 100"), (0, "midi 0x91 59 100"), (0, "midi 0x90 36 100"), (0, "midi 0x90 48 100"), (0, "midi 0x90 55 100"), (0, "midi 0x90 60 100")]
ev = base[:3] + [fx_set(1050, 31, 127), (1100, f"enc {KNOB1} 40")] + [(1200, "down 7"), (1300, "keys 16"), (1400, "keys 0"), (1500, "up 7")]
ev += [(2000 + k, e) for k, (_, e) in enumerate(mid)] + [(9500, "probe")]
pr, sl, _ = run("shot", ev, 10800)           # (host mode writes the screen at 15500 ms: 9500 on, the notes held)
img = ppm(str(OUT / "shot-15500.ppm"))
lvl, mt = levels(pr[9500]), meter(sl[9500])
muted = int(pr[9500].split(" mute ")[1].split()[0])
bad_px, seen = [], set()
for t in range(4):
    f = (lvl[t] * 8 + 63) // 127
    for j in range(8):
        y = 132 - 9 * j + 3
        left = pix(img, 12 + 54 * t + 7 + 10, y)
        want_l = C["UI_D"] if j >= f else C["UI_M"] if muted >> t & 1 else C["UI_L"]
        right = pix(img, 12 + 54 * t + 7 + 24 + 10, y)
        name = "UI_D" if j >= mt[t] else "UI_METER_HOT" if j >= HOT_FROM else "UI_METER_LOUD" if j >= LOUD_FROM else "UI_METER_NORMAL"
        if j < mt[t]:
            seen.add(name)
        if not near(left, want_l):
            bad_px.append(("L", t, j, left, want_l))
        if not near(right, C[name]):
            bad_px.append(("R", t, j, right, C[name]))
check("the screen: each track's column has the LEVEL set on the left (white, dim if muted) and the meter on the right, green / yellow / red by height",
      not bad_px and muted == 4 and mt[2] == 0 and mt[0] > mt[1] > 0, f"levels {lvl} meter {mt} muted {muted}; wrong: {bad_px[:3]}")
check("... every colour showed (green, yellow, red) in this picture", seen == {"UI_METER_NORMAL", "UI_METER_LOUD", "UI_METER_HOT"}, f"seen {sorted(seen)} meter {mt}")

# ---- the screen stays light: what the meter sends (only the meters that moved), against HOME and MIXER 1 with the same four sounds
def traffic(name, taps):
    ev = [x for k in range(taps) for x in tap(600 + 200 * k)]
    ev += [(2000, "midi 0x90 48 100"), (2000, "midi 0x91 52 100"), (2000, "midi 0x92 55 100"), (2000, "midi 0x93 60 100")]
    ev += [(3000, "probe"), (9000, "probe")]
    pr, sl, _ = run(name, ev, 9200)
    return (lcdpx(sl[9000]) - lcdpx(sl[3000])) / 6.0, sl[9000]


px_home, _ = traffic("tr_home", 0)
px_mix1, _ = traffic("tr_mix1", 1)
px_lev, sl_lev = traffic("tr_levels", 3)
check("four tracks sounding: LEVELS sends the screen under 60k pixels a second (a whole screen is 57.6k) and no more than HOME's moving picture does",
      px_lev < 60000 and px_lev <= px_home * 1.5 + 1000, f"LEVELS {px_lev:.0f} px/s, HOME {px_home:.0f}, MIXER 1 {px_mix1:.0f}")

print(f"levels test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
