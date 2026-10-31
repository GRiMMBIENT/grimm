#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2's effects in host mode (tests/run_host.sh): the track filter's TYPE (LP, HP, BP) and POS (PRE, POST: the
sends unfiltered), MASTER (a filter at PRE FX, POST REVERB, END; open: as without it, to the byte), DRIVE (its four
TYPEs, its limiter; AMOUNT 0: as without it, to the byte), and their saves (a V1.0 project: LP PRE, MASTER open, DRIVE
off; V2's: as set). A held saw chord on T1 through each; the sound measured: its level (RMS), its highs (the RMS of
its slope) and its lows (the RMS of a 64-frame average). Prints a line a check, exit 1 on a failure.
  python3 tests/fx_test.py [build/host/grimm]"""
import array
import os
import subprocess
import sys
import wave
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/fx")
OUT.mkdir(parents=True, exist_ok=True)
CHORD = 0x880                                   # (two keys, an octave apart: T1's VA saw)
bad = 0


def run(name, ev, end=4200, env=None):
    ev = [e for e in ev if not (e[1].startswith("enc ") and e[1].endswith(" 0"))]   # (an encoder turned 0 steps:
    ev = [(1000, f"keys {CHORD:#x}")] + ev + [(end - 100, "probe")]                 # no event: the host reads keys)
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=env).stdout
    w = wave.open(str(OUT / f"{name}.wav"))
    a = array.array("h", w.readframes(w.getnframes()))
    probe = [ln for ln in out.splitlines() if ln.startswith("probe ")][-1]
    return a, probe, (OUT / f"{name}.wav").read_bytes()


def measure(a, t0=2500, t1=4000):               # the left channel from t0 to t1 ms: level, highs, lows, peak
    x = a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000:2]
    n = len(x)
    rms = (sum(v * v for v in x) / n) ** .5
    hi = (sum((x[i] - x[i - 1]) ** 2 for i in range(1, n)) / n) ** .5
    lo = (sum((sum(x[i:i + 64]) / 64) ** 2 for i in range(0, n - 64, 64)) / (n // 64)) ** .5
    pk = max(abs(v) for v in a[2 * 44100 * t0 // 1000:2 * 44100 * t1 // 1000])
    return rms, hi, lo, pk


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  fx: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


MIX2 = [(300, "btn 8"), (350, "btn 8")]         # HOME: MIXER 1, MIXER 2
HOME = [(600, "btn 6"), (650, "btn 8")]         # (EDIT, then HOME: HOME)
FX = lambda k: [(700 + 50 * i, "btn 2") for i in range(k)]   # FX tapped k times: TAPE .. DRIVE (6)

base, _, base_b = run("base", [])
rb, hb, lb, pb = measure(base)
# the track filter's TYPE (the chord: C4 E4, 262 and 330 Hz): LP at ~1 kHz (HOME's CUTOFF 127 -> 72), HP and BP at
# ~710 Hz (64)
lp, _, _ = run("lp", HOME + [(800, "enc 3 -55")])
hp, _, _ = run("hp", MIX2 + [(400, "enc 3 1")] + HOME + [(800, "enc 3 -63")])
# (BP with RESONANCE 80: at 0 a band-pass's Q is 0.5, wide and mild)
bp, _, _ = run("bp", MIX2 + [(400, "enc 3 2")] + HOME + [(800, "enc 3 -63"), (850, "enc 4 80")])
r, h, lo_, _ = measure(lp)
check("track LP: the highs down, the lows kept", h < hb * 0.6 and lo_ > lb * 0.8, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
r, h, lo_, _ = measure(hp)
check("track HP: the lows down, the highs kept", lo_ < lb * 0.5 and h > hb * 0.6, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
r, h, lo_, _ = measure(bp)
check("track BP (RESONANCE 80): both ends down", lo_ < lb * 0.7 and h < hb * 0.8, f"hi {h:.0f}/{hb:.0f} lo {lo_:.0f}/{lb:.0f}")
# POS: LP closed (CUTOFF 15), the REV send full, the sound let go at 3000: the reverb's tail measured
SEND = [(300, "btn 8"), (310, "enc 0 2"), (320, "enc 2 127")]   # (MIXER 1: SELECT REVERB, KNOB 1: REV SEND 0 -> 127)
pre, _, _ = run("pre", SEND + [(350, "btn 8"), (400, "enc 4 0")] + HOME + [(800, "enc 3 -112"), (3000, "keys 0")], 5000)
post, pp, _ = run("post", SEND + [(350, "btn 8"), (400, "enc 4 1")] + HOME + [(800, "enc 3 -112"), (3000, "keys 0")], 5000)
_, h_pre, _, _ = measure(pre, 3300, 4800)
_, h_post, _, _ = measure(post, 3300, 4800)
check("track POS POST: the reverb hears it unfiltered (its tail brighter than PRE's)", h_post > h_pre * 1.5,
      f"tail highs POST {h_post:.0f}, PRE {h_pre:.0f}")
# MASTER: open (as set out): the same bytes; LP at 30, at each POSITION: darker
mo, _, mo_b = run("m_open", FX(5) + [(1200, "enc 5 -1")])   # (POSITION END -> POST REVERB, still open)
check("MASTER open: the sound as without it, to the byte", mo_b == base_b)
for pos, steps in (("PRE FX", -2), ("POST REVERB", -1), ("END", 0)):
    m, _, _ = run(f"m_{steps}", FX(5) + [(1200, "enc 2 -97"), (1300, f"enc 5 {steps}")])
    r, h, lo_, _ = measure(m)
    check(f"MASTER LP at {pos}: the highs down", h < hb * 0.5, f"hi {h:.0f}/{hb:.0f}")
mh, _, _ = run("m_hp", FX(5) + [(1200, "enc 4 1"), (1300, "enc 2 -40")])   # (HP at 87)
r, h, lo_, _ = measure(mh)
check("MASTER HP: the lows down", lo_ < lb * 0.5, f"lo {lo_:.0f}/{lb:.0f}")
# DRIVE: AMOUNT 0 (TYPE turned, still off): the same bytes; each TYPE its own sound; a loud chord at full: limited
d0, _, d0_b = run("d_off", FX(6) + [(1200, "enc 3 2"), (1300, "enc 4 -30")])
check("DRIVE off (AMOUNT 0): the sound as without it, to the byte", d0_b == base_b)
outs = []
for ty in range(4):
    d, _, db = run(f"d_{ty}", FX(6) + [(1200, "enc 2 100"), (1300, f"enc 3 {ty}")])
    outs.append(db)
    r, h, lo_, pk = measure(d)
    check(f"DRIVE {['SOFT', 'FUZZ', 'CLIP', 'FOLD'][ty]}: driven (louder, changed), within the limiter",
          r > rb * 1.2 and db != base_b and pk <= 30200, f"rms {r:.0f}/{rb:.0f} peak {pk}")
check("DRIVE: the four TYPEs sound different", len(set(outs)) == 4)
loud, _, _ = run("d_loud", [(1000, "keys 0x1f0ff")] + FX(6) + [(1200, "enc 2 127"), (1300, "enc 4 -127")])
_, _, _, pk = measure(loud)
check("DRIVE full on a big chord, TONE closed: the limiter holds it (no clipping)", pk <= 30200, f"peak {pk}")
# DRIVE's levels: at one AMOUNT the four TYPEs within ~1.5 dB of each other; SOFT the gentlest (its highs the least)
lv = []
for ty in range(4):
    d, _, _ = run(f"dl_{ty}", FX(6) + [(1200, "enc 2 80"), (1300, f"enc 3 {ty}")])
    lv.append(measure(d)[:2])
rs = [r for r, _ in lv]
check("DRIVE at AMOUNT 80: SOFT, FUZZ, CLIP, FOLD about as loud (within 1.5 dB)", max(rs) / min(rs) < 1.19,
      " ".join(f"{r:.0f}" for r in rs))
check("DRIVE: SOFT the gentlest (the least highs of the four)", lv[0][1] == min(h for _, h in lv),
      " ".join(f"{h:.0f}" for _, h in lv))


def onset(a, b):                                # how suddenly render b leaves render a: the largest difference in
    d = [abs(p - q) for p, q in zip(a[::2], b[::2])]   # its first 5 ms against 40..80 ms on (a switch at once: a step,
    i = next((k for k, v in enumerate(d) if v), 0)     # POS: 0.15, MASTER: 0.68; their crossfades 0.02, ~0.1)
    return max(d[i:i + 220]) / max(1, max(d[i + 1764:i + 3528]))


# POS switched while it plays (REV SEND full, CUTOFF low): the reverb hears the change come in, not at once
SETP = [(300, "btn 8"), (310, "enc 0 2"), (320, "enc 2 127")] + HOME + [(800, "enc 3 -100"), (2300, "btn 8"), (2350, "btn 8")]
p0, _, _ = run("pos_steady", SETP, 3200)
p1, _, _ = run("pos_switch", SETP + [(2500, "enc 4 1")], 3200)
o = onset(p0, p1)
check("track POS PRE -> POST while playing: no click (the change comes in over ~46 ms)", o < 0.06, f"{o:.2f}")
# MASTER's POSITION switched while it plays (REV SEND full, LP at 50): PRE FX -> END comes in over ~46 ms
SETM = [(300, "btn 8"), (310, "enc 0 2"), (320, "enc 2 127")] + FX(5) + [(1200, "enc 2 -77"), (1250, "enc 5 -2")]
m0, _, _ = run("mpos_steady", SETM, 3200)
m1, _, _ = run("mpos_switch", SETM + [(2500, "enc 5 2")], 3200)
o = onset(m0, m1)
check("MASTER POSITION PRE FX -> END while playing: no click (the change comes in over ~46 ms)", o < 0.2, f"{o:.2f}")
# saves: V2's as set (autosaved, loaded again), V1.0's project: the defaults
SAVE = MIX2 + [(400, "enc 3 2"), (420, "enc 4 1")] + FX(5) + [(1200, "enc 2 -60"), (1250, "enc 3 40"), (1300, "enc 4 1"),
                                                             (1350, "enc 5 -2"), (1400, "btn 2"), (1500, "enc 2 50"),
                                                             (1550, "enc 3 3"), (1600, "enc 4 -27"), (1650, "enc 5 -60"),
                                                             (1700, "keys 0")]
_, p1, _ = run("save", SAVE, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "saves.bin")))
_, p2, _ = run("load", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "saves.bin")))
want = "fx mf 67,40,1,0 dr 50,3,100,67 ft 6 0 0 0"
check("saved and loaded: MASTER, DRIVE, the track filter as set", want in p1 and want in p2,
      f"set: {p1.split(' fx ')[-1]}; loaded: {p2.split(' fx ')[-1]}")
_, p3, _ = run("v10", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("a GR!MM 1.0 project: LP PRE, MASTER open (END), DRIVE off", "fx mf 127,0,0,2 dr 0,0,127,127 ft 0 0 0 0" in p3,
      p3.split(" fx ")[-1])
# (V2) the tracks' DRIVE sends (MIXER 1: SELECT to DRIVE, KNOB 1): 0, the sound as without; up, T1's chord driven
# beside its clean sound, by DRIVE's TYPE (the whole mix's DRIVE left off); saved, loaded; a 1.0 project: none
DSEL = [(300, "btn 8"), (310, "enc 0 5")]      # (MIXER 1, DRIVE's tile)
_, _, z_b = run("dv_zero", DSEL + [(400, "enc 2 60"), (450, "enc 2 -60")])
check("a DRIVE send turned up and back to 0: the sound as without it, to the byte (after its ramp: the block it went)",
      measure(array.array("h", z_b[44:]), 2500, 4000) == measure(base, 2500, 4000))
dv, dvp, dv_b = run("dv_up", DSEL + [(400, "enc 2 127")])
r, h, lo_, pk = measure(dv)
check("T1's DRIVE send up: the chord driven beside its clean sound (louder, brighter, changed; in range)",
      r > rb * 1.1 and h > hb * 1.2 and dv_b != base_b and pk <= 32767, f"rms {r:.0f}/{rb:.0f} hi {h:.0f}/{hb:.0f}")
_, _, dvf_b = run("dv_fold", DSEL + [(400, "enc 2 127")] + FX(6) + [(1200, "enc 3 3")])
check("the DRIVE send by DRIVE's TYPE (FOLD: another sound), its AMOUNT left at 0", dvf_b != dv_b)
SV = DSEL + [(400, "enc 2 90"), (1700, "keys 0")]
_, p1, _ = run("dv_save", SV, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "dsaves.bin")))
_, p2, _ = run("dv_load", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "dsaves.bin")))
_, p3, _ = run("dv_v10", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("the DRIVE send saved, loaded (90); a GR!MM 1.0 project: none", " dv 90,0,0,0" in p1 and " dv 90,0,0,0" in p2 and
      " dv 0,0,0,0" in p3, f"{p1.split(' dv ')[-1]} {p2.split(' dv ')[-1]} {p3.split(' dv ')[-1]}")
# MIDI LEARN: links to MIXER 1 made before (1.0: its knobs LEVEL, DLY, REV, GRAIN: x 0) as they were; the DRIVE send's
# (x 4). Settings (170 B) with three links: CC 20 -> LEVEL, CC 21 -> REV SEND, CC 22 -> DRIVE SEND, all T1
st = bytearray(170)
st[6], st[167] = 3, 3
st[7:12], st[12:17], st[17:22] = bytes([20, 8, 0, 0, 0]), bytes([21, 8, 2, 0, 0]), bytes([22, 8, 0, 0, 4])
(OUT / "links.bin").write_bytes(bytes(st))
_, pl, _ = run("links", [(1200, "midi 0xB0 20 64"), (1250, "midi 0xB0 21 127"), (1300, "midi 0xB0 22 100")], 1500,
               dict(os.environ, GRIMM_SETTINGS_IN=str(OUT / "links.bin")))
mixv = pl.split(" t1mix ")[1].split()[0]
check("MIDI LEARN: 1.0's MIXER 1 links (LEVEL, REV SEND) as they were; the DRIVE send's", mixv.startswith("64,") and
      mixv.split(",")[2] == "127" and " dv 100,0,0,0" in pl, f"T1 level,dly,rev,grn {mixv}; {pl.split(' dv ')[1][:12]}")
# (V2) TAPE a track's own (MIXER 1: SELECT to TAPE, KNOB 1: how far the track goes through it): 0, the sound as without;
# up, T1's chord through the tape as TAPE has it (WOW, SATURATION, TONE), crossfaded from its clean sound (never both);
# TAPE's knobs up with every tile 0: nothing; saved (beside ARP OCTAVES), loaded; 1.0: none
TSEL = [(300, "btn 8"), (310, "enc 0 3")]      # (MIXER 1, TAPE's tile)
_, _, tz_b = run("tp_zero", TSEL + [(400, "enc 2 60"), (450, "enc 2 -60")])
check("a TAPE tile turned up and back to 0: the sound as without it, to the byte (after its ramp)",
      measure(array.array("h", tz_b[44:]), 2500, 4000) == measure(base, 2500, 4000))
_, _, tp_b = run("tp_clean", TSEL + [(400, "enc 2 127")])
check("T1's TAPE tile up, TAPE all 0: the sound as without it, to the byte (no copy beside it)", tp_b == base_b)
_, _, tk_b = run("tp_knobs", FX(1) + [(1200, "enc 2 127"), (1250, "enc 3 110"), (1300, "enc 4 90"), (1350, "enc 5 127")])
check("TAPE's four knobs up, every TAPE tile 0: the sound as without it, to the byte (no bend, no hiss)", tk_b == base_b)
tps, _, tps_b = run("tp_sat", TSEL + [(400, "enc 2 127")] + FX(1) + [(1200, "enc 3 110")])
r, h, lo_, pk = measure(tps)
check("T1's tile up, SATURATION: the chord saturated in its place (not louder than a copy beside it would make it; "
      "in range)", tps_b != base_b and r < rb * 1.3 and pk <= 32767, f"rms {r:.0f}/{rb:.0f} peak {pk}")
_, _, tph_b = run("tp_half", TSEL + [(400, "enc 2 64")] + FX(1) + [(1200, "enc 3 110")])
check("T1's tile at 50 %: another sound than at 100 % and than without", tph_b != tps_b and tph_b != base_b)
tpw, _, tpw_b = run("tp_wow", TSEL + [(400, "enc 2 127")] + FX(1) + [(1200, "enc 2 127")], 9000)   # (a warble: 3 s on)
_, _, nw_b = run("tp_nowow", TSEL + [(400, "enc 2 127")], 9000)
r, h, lo_, pk = measure(tpw, 2500, 8800)
check("T1's tile up, WOW: the chord bent (another sound; in range)", tpw_b != nw_b and pk <= 32767, f"peak {pk}")
_, _, tfx_b = run("tp_fx0", TSEL + [(400, "enc 2 127"), (500, "btn 6"), (550, "btn 8"), (600, "enc 5 -100")] + FX(1) +
                 [(900, "enc 3 110")])         # (T1's tile up, HOME: FX 0; then TAPE: SATURATION)
_, _, fx0_b = run("fx0", HOME + [(800, "enc 5 -100")])
check("HOME's FX at 0: T1's TAPE tile too (SATURATION up: the sound as with FX 0 and no tape, to the byte)",
      tfx_b == fx0_b)
SV = TSEL + [(400, "enc 2 90"), (1700, "keys 0")]
_, p1, _ = run("tp_save", SV + [(1800, "btn 10"), (1850, "enc 4 2")], 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "tsaves.bin")))
_, p2, _ = run("tp_load", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "tsaves.bin")))
_, p3, _ = run("tp_v10", [(1050, "keys 0")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
ao = p1.split(" ao ")[1].split()[0]
check("the TAPE send saved, loaded (90), ARP OCTAVES beside it kept; a GR!MM 1.0 project: none",
      " tp 90,0,0,0" in p1 and " tp 90,0,0,0" in p2 and ao == "3" and " ao 3" in p2 and " tp 0,0,0,0" in p3,
      f"{p1.split(' tp ')[-1]} | {p2.split(' tp ')[-1]} | {p3.split(' tp ')[-1]}")
st[22:27] = bytes([23, 8, 0, 0, 5])
st[6] = 4
(OUT / "links2.bin").write_bytes(bytes(st))
_, pl, _ = run("links2", [(1200, "midi 0xB0 23 127")], 1500, dict(os.environ, GRIMM_SETTINGS_IN=str(OUT / "links2.bin")))
check("MIDI LEARN: a link to the TAPE send (x 5)", " tp 127,0,0,0" in pl, pl.split(" tp ")[1][:16])
# (V2) VOICE's TYPE (SOURCE, KNOB 3): the four choirs load theirs; a choir another sound than SOLO, in range; saved
# beside SHIFT (one byte), loaded; a GR!MM 1.0 project: SOLO
want = {36: "GREGORIAN 1", 37: "PLAINCHANT 1", 38: "CATHEDRAL_CHOIR 3", 39: "NUNS 2"}
got = {k: run(f"vt{k}", [(300, f"enc 6 {k}"), (500, "probe")], 600)[1].split(" sname ")[1].split(" vsh ")[0].replace(" vty", "")
       for k in want}
check("the four choir sounds load their TYPEs (GREGORIAN, GREGORIAN, CATHEDRAL, NUNS)", got == want, got)
SRC = [(300, "enc 6 31"), (400, "btn 6"), (450, "btn 6")]   # (CHOIR OF GHOSTS: VOICE, SOLO; EDIT: SOURCE)
_, ps, solo_b = run("vt_solo", SRC + [(600, "probe")])
for ty, name in ((1, "GREGORIAN"), (2, "NUNS"), (3, "CATHEDRAL")):
    w, pv, ch_b = run(f"vt_{ty}", SRC + [(500, f"enc 4 {ty}"), (600, "probe")])
    r, h, lo_, pk = measure(w)
    check(f"TYPE {name}: a choir, another sound than SOLO's, in range", f" vty {ty} " in pv and ch_b != solo_b and
          pk <= 32767 and r > 100, f"{pv.split(' vty ')[1][:12]} rms {r:.0f} peak {pk}")
SVV = SRC + [(500, "enc 4 3"), (550, "btn 6"), (600, "enc 3 5"), (1700, "keys 0")]   # (CATHEDRAL; SOUND: SHIFT +5)
_, p1, _ = run("vt_save", SVV, 9000, dict(os.environ, GRIMM_SAVE_OUT=str(OUT / "vsaves.bin")))
_, p2, _ = run("vt_load", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN=str(OUT / "vsaves.bin")))
_, p3, _ = run("vt_v10", [(1050, "keys 0"), (1100, "probe")], 1500, dict(os.environ, GRIMM_SAVE_IN="tests/data/v1.0-saves.bin"))
check("VOICE's TYPE saved beside its SHIFT and loaded (CATHEDRAL, SHIFT as set); a GR!MM 1.0 project: SOLO",
      p1.split(" vty ")[1].startswith("3 vsh 19") and p2.split(" vty ")[1].startswith("3 vsh 19") and
      p3.split(" vty ")[1].startswith("0 "), f"{p1.split(' vty ')[1][:10]} | {p2.split(' vty ')[1][:10]} | {p3.split(' vty ')[1][:10]}")
# (V2) the choirs clean through their range: NUNS's and CATHEDRAL CHOIR's chords (the women's cycles, the hottest),
# A3 .. G5 (keys, then OCT+): no sample at full scale, no sudden jump (a sample's second difference past 40 x its
# neighbourhood's, 46 ms around it: a click, a pop; before the fix NUNS had dozens here: its sums wrapped in 32 bits)
KEYS = [4, 8, 12, 16, 20, 24]                   # (A3, C#4, F4, A4, C#5, F5; then an octave up)


def clean(name, preset):
    ev, t = [(300, f"enc 6 {preset}")], 1000
    for o in range(2):
        if o:
            ev.append((t, "btn 1"))
            t += 200
        for k in KEYS:
            ev += [(t, f"keys {1 << k:#x}"), (t + 900, "keys 0")]
            t += 1000
    _, _, raw = run(name, ev, t + 1500)
    a = array.array("h", raw[44:])
    m = [(a[2 * i] + a[2 * i + 1]) // 2 for i in range(len(a) // 2)]
    d = [abs(m[i + 2] - 2 * m[i + 1] + m[i]) for i in range(len(m) - 2)]
    blk = [sum(d[j:j + 2048]) / 2048 for j in range(0, len(d), 2048)]
    jumps = sum(1 for i, x in enumerate(d) if x > 40 * blk[i // 2048] + 600)
    return jumps, max(abs(x) for x in a)


for nm, pr in (("NUNS", 39), ("CATHEDRAL CHOIR", 38)):
    j, pk = clean(f"clean{pr}", pr)
    check(f"{nm}: chords A3 .. G5 clean (no sudden jumps, nothing at full scale)", j == 0 and pk < 32000,
          f"jumps {j}, peak {pk}")
# (V2) the choirs' VOWEL: NUNS, LATCH, A4 held, VOWEL turned A .. U a step every 94 ms: no jump while it turns; the
# vowels' harmonics apart (A open, E bright: its second resonance, harmonics 3 .. 6): E from A by 8 dB and more (before
# E, I, U were made clearer: 5.8)


def harmonics_db(m, t, f0, n=8):                # harmonics 1 .. n at t s (8192 samples, Hann), dB under the loudest
    import math
    a0 = int(t * 44100)
    seg = [m[a0 + i] * (0.5 - 0.5 * math.cos(2 * math.pi * i / 8191)) for i in range(8192)]
    out = []
    for k in range(1, n + 1):                   # (Goertzel)
        w = 2 * math.pi * k * f0 / 44100
        c, s1, s2 = 2 * math.cos(w), 0.0, 0.0
        for x in seg:
            s1, s2 = x + c * s1 - s2, s1
        out.append(math.sqrt(max(s1 * s1 + s2 * s2 - c * s1 * s2, 1e-9)))
    top = max(out)
    return [max(20 * math.log10(v / top), -40) for v in out]


ev = [(300, "enc 6 39"), (350, "down 3"), (360, "btn 1"), (370, "up 3"), (600, f"keys {1 << 16:#x}"), (700, "keys 0"),
      (800, "btn 6"), (900, "enc 2 -127")] + [(3000 + 94 * i, "enc 2 1") for i in range(127)]
_, _, raw = run("vowels", ev, 3000 + 94 * 127 + 2500)
a = array.array("h", raw[44:])
m = [(a[2 * i] + a[2 * i + 1]) / 2 for i in range(len(a) // 2)]
w = m[int(2.5 * 44100):int(15.5 * 44100)]
d = [abs(w[i + 2] - 2 * w[i + 1] + w[i]) for i in range(len(w) - 2)]
blk = [sum(d[j:j + 2048]) / 2048 for j in range(0, len(d), 2048)]
jumps = sum(1 for i, x in enumerate(d) if x > 40 * blk[i // 2048] + 600)
hA, hE = harmonics_db(m, 3.35, 440.0), harmonics_db(m, 3.0 + 32 * 0.094 + 0.35, 440.0)
apart = sum(abs(x - y) for x, y in zip(hA, hE)) / len(hA)
check("the choirs' VOWEL: turned A .. U without a jump; E from A by 8 dB and more", jumps == 0 and apart >= 8.0,
      f"jumps {jumps}, E from A {apart:.1f} dB")
print("fx test: passed" if not bad else "fx test: FAILED")
sys.exit(1 if bad else 0)
