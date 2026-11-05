#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0: the VA source's own filter (dsp.c tsvf_lp) no longer leaves a DC offset at a low cutoff. Its integrator piled up the
half step a floor takes off each sample into an offset (-658 of 16 bits at the lowest cutoff, with a sound of 376: more than the
sound), which a DRIVE multiplied into a lopsided, wandering clip: the bass crumbled as an LFO swept the VA's CUTOFF. Host mode:
  - SAW, SQR and SUPERSAW, two low notes, at 12 cutoffs from 0 to 127 and RESONANCE 0 and 60, no drive: the DC (the mean of 2 s)
    is under 10 LSB and under 5 % of the sound;
  - the same at CUTOFF 0 and 20 through the DRIVE page at 100 % (AMOUNT, TONE, MIX 127), each TYPE: SOFT, CLIP, FOLD keep under 6 %
    of the sound (FUZZ's lopsided curve has an offset of its own, under 30 %);
  - an LFO (DEPTH 100) on the VA's CUTOFF, three cycles, the DRIVE page at 100 %: the 50 ms means of the output wander by
    less than 0.45 (FUZZ, CLIP 0.7, FOLD 1.1) of its rms; before the fix 0.6 .. 1.75.
  python3 tests/va_dc_test.py [build/host/grimm]"""
import array
import os
import subprocess
import sys
import wave
import zlib
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/vadc")
OUT.mkdir(parents=True, exist_ok=True)
BOOT = 6000
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  va_dc: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def pack7(b):
    o = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        o.append(sum((x >> 7) << j for j, x in enumerate(g)))
        o += [x & 0x7F for x in g]
    return o


def sx(t, *b):
    return (t, "sx " + " ".join(f"{x:02X}" for x in (0x7D, 0x47, 0x52) + b))


def sound(wave_, cut, res, lfo=(60, 0, 5, 0)):
    r = bytearray(64)
    r[0], r[3] = 0x54, 0                                      # a sound, source VA
    r[4], r[5], r[6], r[7] = wave_, 64, cut, res              # WAVE, DETUNE, CUTOFF, RESONANCE
    r[8], r[9], r[10], r[11] = 0, 64, 127, 40                 # ENV
    r[14], r[15], r[16], r[17] = lfo                          # LFO: RATE, DEPTH, DESTINATION (5: the source's KNOB 3, CUTOFF), SHAPE
    r[60:64] = zlib.crc32(bytes(r[:60])).to_bytes(4, "little")
    return bytes(r)


def render(name, snd, drive=None, dur=4000, notes=(29, 36)):
    """T1 plays snd, two low notes held; drive: None, or the DRIVE page's TYPE at 100 %; -> the left channel (after 1.5 s)"""
    ev = [sx(1000, 23, 1, 0, *pack7(list(snd))), sx(1100, 25, 1, 29, 0)]          # (T1\'s REV send off)
    if drive is not None:
        ev += [sx(1110, 25, 1, 20, 127), sx(1120, 25, 1, 21, drive), sx(1130, 25, 1, 22, 127), sx(1140, 25, 1, 23, 127)]
    ev += [(2000 + 50 * k, f"midi 0x90 {n} 100") for k, n in enumerate(notes)]
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t + BOOT} {s}\n" for t, s in sorted(ev, key=lambda e: e[0])) + f"{2000 + dur + BOOT} end\n")
    subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, check=True)
    with wave.open(str(OUT / f"{name}.wav")) as w:
        a = array.array("h")
        a.frombytes(w.readframes(w.getnframes()))
    return a[0::2].tolist()


def dc_ac(x, t0, t1):
    seg = x[44100 * (t0 + BOOT) // 1000: 44100 * (t1 + BOOT) // 1000]
    m = sum(seg) / len(seg)
    return m, (sum((v - m) ** 2 for v in seg) / len(seg)) ** 0.5


# ---- no drive: the DC at every cutoff
CUTS = (0, 1, 2, 4, 8, 16, 24, 32, 48, 64, 96, 127)
worst = (0.0, "")
for wv, wn in ((0, "SAW"), (1, "SQR"), (2, "SUPER")):
    for res in (0, 60):
        for cut in CUTS:
            x = render("nd", sound(wv, cut, res))
            m, ac = dc_ac(x, 2800, 5800)
            lim = min(10.0, 0.05 * ac) if ac > 200 else 10.0
            if abs(m) > worst[0] / 1.0 and abs(m) > lim:
                worst = (abs(m), f"{wn} res {res} cut {cut}: dc {m:.1f} ac {ac:.1f}")
            elif abs(m) > lim:
                worst = worst
check("no drive: SAW, SQR, SUPERSAW x RESONANCE 0 / 60 x 12 cutoffs: the DC of the VA filter under 10 LSB and 5 % of the sound", worst[0] == 0.0, worst[1])

# ---- through the DRIVE page (each TYPE) at the lowest cutoffs
for ty, nm, lim in ((0, "SOFT", 0.06), (2, "CLIP", 0.06), (3, "FOLD", 0.06), (1, "FUZZ", 0.30)):
    bads = []
    for cut in (0, 20):
        x = render("dr", sound(2, cut, 0), drive=ty)
        m, ac = dc_ac(x, 2800, 5800)
        if abs(m) > lim * ac:
            bads.append(f"cutoff {cut}: dc {m:.0f} ac {ac:.0f}")
    check(f"DRIVE page 100 % ({nm}): the offset at VA CUTOFF 0 and 20 under {lim * 100:.0f} % of the sound", not bads, "; ".join(bads))

# ---- an LFO on the VA's CUTOFF, three cycles (2.1 s each), the DRIVE page at 100 %: the output's 50 ms means do not wander
for ty, nm, lim in ((0, "SOFT", 0.45), (1, "FUZZ", 0.7), (2, "CLIP", 0.7), (3, "FOLD", 1.1)):
    x = render("lfo", sound(2, 60, 0, lfo=(60, 100, 5, 0)), drive=ty, dur=12000)
    seg = x[44100 * (2000 + BOOT + 6000) // 1000: 44100 * (2000 + BOOT + 12200) // 1000]
    win = 2205
    ms = [sum(seg[i:i + win]) / win for i in range(0, len(seg) - win, win)]
    rms = (sum(v * v for v in seg) / len(seg)) ** 0.5
    rng = max(ms) - min(ms)
    check(f"LFO DEPTH 100 on the VA's CUTOFF, DRIVE page 100 % ({nm}): the 50 ms means wander by {rng / rms:.2f} of the rms (under {lim})", rng < lim * rms,
          f"range {rng:.0f}, rms {rms:.0f}")

print(f"va_dc test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
