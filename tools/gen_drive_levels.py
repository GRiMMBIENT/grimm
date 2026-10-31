#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""DRIVE's level table (firmware/src/master.c DR_LEVEL): each TYPE's loudness (RMS) at AMOUNT 0, 16 .. 128 on a chord of
two saws at three levels (RMS 2000, 4000, 8000: ~-24 .. -12 dBFS), computed as the firmware does it (integers: softclip on dsp.c's TANH table, the TYPEs' curves, the
gain), and a gain (Q12) a TYPE an AMOUNT that brings it to the target, SOFT's own level but never past twice the clean
sound's (+6 dB): the three levels' gains' geometric mean. All four as loud as each other at an AMOUNT; turning AMOUNT up never more than ~6 dB louder.

  python3 tools/gen_drive_levels.py      (prints the table for master.c)
"""
import math

TANH = [int(32767 * math.tanh(i / 256 * 2)) for i in range(257)]
TOP = 26000


def softclip(x):
    a = -x if x < 0 else x
    i = a >> 8
    y = TANH[256] if i >= 256 else TANH[i] + (((TANH[i + 1] - TANH[i]) * (a & 255)) >> 8)
    return -y if x < 0 else y


def pow2_q16(d):
    return int(65536 * 2 ** (d / 192))


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def shape(ty, y):
    if ty == 1:
        return softclip(y + TOP // 3) - softclip(TOP // 3)
    if ty == 2:
        return clamp(y, -TOP, TOP)
    if ty == 3:
        u = (y + TOP) % (4 * TOP)
        return u - TOP if u < 2 * TOP else 3 * TOP - u
    return softclip(y)


def gain(ty, a):                                 # (Q8: SOFT to ~8x, the others to ~62x)
    return pow2_q16(a * 9 // (2 if ty == 0 else 1)) >> 8


CHORD = [((i * 262 / 44100) % 1 - .5) + ((i * 330 / 44100) % 1 - .5) for i in range(8820)]   # (two saws: C4 E4)
CR = (sum(x * x for x in CHORD) / len(CHORD)) ** .5
LEVELS = (2000, 4000, 8000)                      # (its RMS: ~-24, -18, -12 dBFS, a track's to a full mix)


def rms(ty, a, lvl):
    g = gain(ty, a)
    v = [shape(ty, (clamp(int(x * lvl / CR), -131071, 131071) * g) >> 8) for x in CHORD]
    m = sum(v) / len(v)                          # (FUZZ's lean: not loudness)
    return (sum((x - m) ** 2 for x in v) / len(v)) ** .5


AMTS = [0, 16, 32, 48, 64, 80, 96, 112, 128]
tab = []
for ty in range(4):
    row = []
    for a in AMTS:
        a = min(a, 127)
        k = 1.0
        for lvl in LEVELS:                       # (the gain each level would want, their geometric mean)
            k *= min(rms(0, a, lvl), 2 * lvl) / rms(ty, a, lvl)
        row.append(int(round(4096 * k ** (1 / len(LEVELS)))))
    tab.append(row)
for ty, row in enumerate(tab):
    print(["SOFT", "FUZZ", "CLIP", "FOLD"][ty], [f"{rms(ty, min(a, 127), 4000) / 4000:.2f}" for a in AMTS], row)
print("static const uint16_t DR_LEVEL[NDTYPE][9] = {" + ", ".join("{" + ", ".join(map(str, r)) + "}" for r in tab) + "};")
