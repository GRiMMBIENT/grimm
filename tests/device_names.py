# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The stress test's device check (tests/stress.sh): on the FM-1 grimm.c, platform.c, usb.c and the rest are one file,
and C quietly makes two file-scope variables of the same name and type one variable (GR!MM's key-echo ring and usb.c's
MIDI-out ring once shared mo_w / mo_r: the main loop chased itself, the watchdog restarted the FM-1). Host mode has no
usb.c, so only the device build shows it: the toolchain's preprocessor makes the device's file, gcc lists every name
declared twice (-Wredundant-decls), and a variable among them fails (a function's prototype, an extern: fine).

    python3 tests/device_names.py        (prints nothing; exit 1 and the names on a clash)"""
import os
import re
import subprocess
import sys

tc = os.environ.get("JIELI_TOOLCHAIN", os.path.expanduser("~/.jieli/toolchain"))
clang = os.path.join(tc, "pi32v2", "bin", "clang")
if not os.path.exists(clang):
    print("device names: no toolchain (" + clang + "): not checked")
    sys.exit(1)
os.makedirs("build/stress", exist_ok=True)
pre = "build/stress/device.i"
subprocess.run([clang, "-target", "pi32v2", "-E", "-Os", "-fno-builtin", "-Ifirmware/hal", "-Ifirmware/src",
                "-Ifirmware/gen", '-DFELUCCA_ID="FM-1_966"', '-DAPP_VER="0.0"', "firmware/src/grimm.c", "-o", pre],
               check=True)
out = subprocess.run(["gcc", "-fsyntax-only", "-m32", "-w", "-Wredundant-decls", "-x", "c", pre],
                     capture_output=True, text=True).stderr
bad = []
for m in re.finditer(r"^(\S+?):(\d+):(\d+): warning: redundant redeclaration of .(\w+).", out, re.M):
    path, line, col, name = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
    try:
        text = open(path, encoding="utf-8", errors="replace").read().split("\n")[line - 1]
    except OSError:
        text = ""
    rest = text[col - 1 + len(name):].lstrip()
    if rest.startswith("(") or "extern" in text:
        continue                                # (a function declared ahead, an extern: one thing, as meant)
    bad.append(f"{path}:{line}: {name}")
if bad:
    print("device names: one variable under two declarations (the device's single file):")
    print("\n".join("  " + b for b in bad))
    sys.exit(1)
