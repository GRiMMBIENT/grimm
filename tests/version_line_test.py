#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The version in the boot intro and in the SYSTEM pages' header, in all three themes (tests/run_host.sh): a host binary with a build stamp like the device's (APP_VER "3.0-dabc1+": the
version, a dash, d and four hex digits of the commit, a + for a dirty tree):
  - the boot intro's line shows just "V3.0": EYE's "FM-1 / DEATHDREAM / V3.0" (8 px a character, centred: x 24 .. 214), BRUTAL's the same in Terminus Bold (6 px: x 48 .. 190), MINIMAL's "V3.0"
  - the SYSTEM pages show the whole stamp, in each theme: EYE's header ("V3.0-dabc1+" at its right end), BRUTAL's red tag in two lines ("V3.0", "-dabc1+": 6 px a character, Terminus
    Bold, white), MINIMAL's tag ("VER 3.0-dabc1+")
  python3 tests/version_line_test.py"""
import os
import re
import subprocess
import sys
from pathlib import Path

OUT = Path("build/host/version")
OUT.mkdir(parents=True, exist_ok=True)
EXE = OUT / "grimm_stamp"
subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="3.0-dabc1+"', "-Ifirmware/src", "-Ifirmware/gen", "-o", str(EXE), "firmware/src/grimm.c"], check=True)
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  version: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def shot(name, theme, ev, t):
    p = OUT / f"{name}.txt"
    p.write_text("".join(f"{a} {b}\n" for a, b in sorted(ev + [(t, "shot")])) + f"{t + 300} end\n")
    for f in OUT.glob(f"{name}-*.ppm"):
        f.unlink()
    subprocess.run([str(EXE), str(OUT / name), str(p)], capture_output=True, check=True, env=dict(os.environ, GRIMM_THEME=str(theme)))
    return (OUT / f"{name}-{t}.ppm").read_bytes()


def px(ppm):
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", ppm)
    return int(m.group(1)), ppm[m.end():]


def ext(ppm, cols, box):
    """-> (min x, max x) of the pixels of these colours in the box (x0, y0, x1, y1)"""
    w, d = px(ppm)
    xs = [x for y in range(box[1], box[3]) for x in range(box[0], box[2]) if tuple(d[3 * (y * w + x):3 * (y * w + x) + 3]) in cols]
    return (min(xs), max(xs)) if xs else None


def c888(r, g, b):
    return ((r >> 3) * 255 // 31, (g >> 2) * 255 // 63, (b >> 3) * 255 // 31)


def rgbs(text):
    return [c888(*(int(x, 16) for x in m)) for m in re.findall(r"RGB\(0x(..), 0x(..), 0x(..)\)", text)]


ui = Path("firmware/src/ui.c").read_text(encoding="utf-8")
br = Path("firmware/src/brutal.c").read_text(encoding="utf-8")
mn = Path("firmware/src/minimal.c").read_text(encoding="utf-8")
EYEP = rgbs(ui[ui.index("UI_PAL_EYE = "):ui.index("static const ui_pal_t *ui_pal")])
BRUP = rgbs(br[br.index("UI_PAL_BRUTAL = "):br.index("/* ---- the font")])
MINP = rgbs(mn[mn.index("UI_PAL_MINIMAL = "):mn.index("static uint16_t mn_mix")])
SYS = int(re.findall(r"PG_\w+", re.search(r"enum \{ (PG_SOUND.*?)NPAGE \}", Path("firmware/src/grimm.c").read_text(encoding="utf-8"), re.S).group(1)).index("PG_SYSTEM"))

# the boot intro: "V3.0" only (V, the version, no stamp)
e = shot("bootE", 0, [], 4800)
check("EYE's boot line is FM-1 / DEATHDREAM / V3.0 (24 characters of the pixel font, centred: x 24 .. 214), the stamp left off", ext(e, {EYEP[2]}, (0, 208, 240, 220)) == (24, 214), str(ext(e, {EYEP[2]}, (0, 208, 240, 220))))
b = shot("bootB", 1, [], 4400)
check("BRUTAL's boot line is the same in Terminus Bold, black on the door (24 x 6 px, centred: x 48 .. 190)", ext(b, {BRUP[0]}, (0, 184, 240, 198)) == (48, 190), str(ext(b, {BRUP[0]}, (0, 184, 240, 198))))
m = shot("bootM", 2, [], 5100)
x = ext(m, {MINP[3], c888(0x7c, 0x80, 0x86)}, (0, 144, 240, 160))
check("MINIMAL's boot line is just V3.0 (Terminus Regular, centred: 4 x 6 px)", x is not None and 106 <= x[0] and x[1] <= 134, str(x))

# the SYSTEM pages: the whole stamp
WHITE_B = BRUP[4]
bs = shot("sysB", 1, [(6000, f"page {SYS}")], 6500)
l1, l2 = ext(bs, {WHITE_B}, (183, 5, 237, 14)), ext(bs, {WHITE_B}, (183, 16, 237, 27))
check(f"BRUTAL's SYSTEM page: the red tag has the whole stamp in two lines of Terminus Bold, white: V3.0 (x 187 .. 210) over -dabc1+ (x 187 .. 227)", l1 and l2 and l1[0] == 187 and 207 <= l1[1] <= 211 and l2[0] == 187 and 224 <= l2[1] <= 229,
      f"{l1} {l2}")
es = shot("sysE", 0, [(6000, f"page {SYS}")], 6500)
h = ext(es, {EYEP[3], EYEP[4], EYEP[2]}, (150, 4, 240, 20))
check("EYE's SYSTEM page: the header's right end has the whole stamp (V3.0-dabc1+: 11 characters, 8 px, ends at the right)", h is not None and h[1] >= 230 and h[0] <= 150 + 4, str(h))
ms = shot("sysM", 2, [(6000, f"page {SYS}")], 6500)
t = ext(ms, {MINP[3]}, (150, 4, 240, 24))
check("MINIMAL's SYSTEM page: the tag's value is the whole stamp (3.0-dabc1+: 10 characters of 8 px, ending at 236)", t is not None and t[1] >= 232 and 154 <= t[0] <= 158, str(t))
sys.exit(1 if bad else 0)
