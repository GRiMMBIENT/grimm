#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2.0.2: the sample slots after another firmware, or a cut upload (host mode, firmware/src/host.c, compat.c).
At boot a slot that is all 0xFF gets its factory sample (as before); then, once the slots have been checked and nothing
sounds, every slot that still does not play (junk, data without a header, a wrong CRC) gets it too. A slot that plays is
never touched. The header says which ("SAMPLES RESTORED: 1 2"). A write that fails is not tried again.
    python3 tests/samplefix_test.py build/host/grimm"""
import os
import re
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/samplefixtest")
OUT.mkdir(parents=True, exist_ok=True)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  samplefix: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def run(name, pre, end=20000, fail=False):
    """-> ([(playing, name, factory, same) a slot], erases tried, [(ms, header message)])"""
    path, res = OUT / f"{name}.txt", OUT / f"{name}.out"
    path.write_text("".join(f"{t} probe\n" for t in range(500, end, 500)) + f"{end} end\n")
    env = dict(os.environ, GRIMM_SMP_PRE=pre, GRIMM_SMP_OUT=str(res))
    if fail:
        env["GRIMM_SMP_FAIL"] = "1"
    out = subprocess.run([BIN, str(OUT / name), str(path)], capture_output=True, text=True, check=True, env=env).stdout
    slots = [(int(m[0]), m[1], int(m[2]), int(m[3])) for m in
             re.findall(r"^slot \d playing (\d) name (\S+) factory (\d) same (\d)$", res.read_text(), re.M)]
    erases = int(re.search(r"^erases (\d+)$", res.read_text(), re.M)[1])
    hd = [(int(m[1]), m[2].replace("_", " ")) for m in re.finditer(r"^probe (\d+) .* hd (\S+) ", out, re.M)]
    return slots, erases, hd


def said(hd):
    return [m for t, m in hd if m != "-"]


# junk in slots 1-3 (another firmware's data), slot 4 blank: all four play the factory samples, the header says 1 2 3 4
s, e, hd = run("junk", "JJJ-")
check("junk in slots 1-3, slot 4 blank: all four are the factory samples, playing",
      [x[:3] for x in s] == [(1, "URCHIN", 1), (1, "LEAVE", 1), (1, "NIKOLAI", 1), (1, "M-SOFT", 1)], str(s))
check("... and the header says so: SAMPLES RESTORED: 123", set(said(hd)) == {"SAMPLES RESTORED: 123"}, str(said(hd)[:2]))
check("... for a few seconds, then it goes", hd and hd[-1][1] == "-" and any(m != "-" for t, m in hd), str(hd[-2:]))

# the three ways a slot does not play: junk, data without a header (a cut upload), a good header with a wrong CRC
s, e, hd = run("kinds", "JPCJ")
check("junk, a cut upload (no header), a wrong CRC: each one the factory sample",
      [x[:3] for x in s] == [(1, "URCHIN", 1), (1, "LEAVE", 1), (1, "NIKOLAI", 1), (1, "M-SOFT", 1)], str(s))
check("... the header says SAMPLES RESTORED: 1234", set(said(hd)) == {"SAMPLES RESTORED: 1234"}, str(said(hd)[:2]))

# a good sample of the user's is never touched; the others around it are filled
s, e, hd = run("user", "JUC-")
check("a user's good sample in slot 2: not touched (byte for byte), still playing as USERTEST",
      s[1] == (1, "USERTEST", 0, 1), str(s[1]))
check("... its neighbours: junk, a wrong CRC and a blank slot are the factory samples",
      [s[0][:3], s[2][:3], s[3][:3]] == [(1, "URCHIN", 1), (1, "NIKOLAI", 1), (1, "M-SOFT", 1)], str(s))
check("... the header names the slots the second pass wrote (not the user's, not the blank one the boot screen filled): SAMPLES RESTORED: 13", set(said(hd)) == {"SAMPLES RESTORED: 13"},
      str(said(hd)[:2]))

# every slot good (a user's four, or the factory's): nothing is written, nothing said
s, e, hd = run("good", "UFUF")
check("slots that play (a user's, the factory's): untouched, nothing said", all(x[0] == 1 and x[3] == 1 for x in s) and not said(hd),
      f"{s} {said(hd)[:2]}")

# blank slots, a new FM-1: as before, filled at boot; the boot screen said so; the second pass has nothing to do
s, e, hd = run("blank", "----")
check("blank slots: the four factory samples, as before", [x[:3] for x in s] == [(1, "URCHIN", 1), (1, "LEAVE", 1), (1, "NIKOLAI", 1),
                                                                                  (1, "M-SOFT", 1)], str(s))
check("... written once each (four sectors-by-18 erases = 72), the second pass writes nothing, the header says nothing", e == 72 and not said(hd),
      f"{e} erases, {said(hd)[:2]}")

# the writes cannot be done: junk stays, and it is tried once (4 slots, the first erase of each fails), not every pass
s, e, hd = run("fail", "JJJJ", end=30000, fail=True)
check("writes that fail: the slots stay as they were, tried once each (4 erases in 30 s), not in a loop", e == 4 and all(x[3] == 1 for x in s),
      f"{e} erases, {s}")
check("... the header says SAMPLE RESTORE FAILED", set(said(hd)) == {"SAMPLE RESTORE FAILED"}, str(said(hd)[:2]))

print(f"samplefix test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
