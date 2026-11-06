#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The ASCII part of Terminus Font's regular (medium) cuts 6x12 and 8x14 -> assets/fonts/terminus-regular/ter-u12n-ascii.bdf, ter-u14n-ascii.bdf (a cell a glyph,
as the bold ones in assets/fonts/terminus-bold: tools/gen_brutalfont.py reads them), from the PCF files of Terminus Font 4.49 (SIL OFL 1.1, Dimitar Toshkov Zhekov:
assets/fonts/Terminus-LICENSE.txt; Debian's xfonts-terminus). Run once, with the folder the PCFs are in (ter-u12n_iso-8859-1.pcf.gz ..):
  python3 tools/gen_terminus_regular.py /usr/share/fonts/X11/misc
"""
import gzip
import io
import sys
from pathlib import Path

from PIL import PcfFontFile

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "fonts" / "terminus-regular"
OUT.mkdir(parents=True, exist_ok=True)
src = Path(sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/X11/misc")
for size, cw in ((12, 6), (14, 8)):
    pf = PcfFontFile.PcfFontFile(io.BytesIO(gzip.decompress((src / f"ter-u{size}n_iso-8859-1.pcf.gz").read_bytes())))
    lines = ["STARTFONT 2.1", f"FONT -xos4-Terminus-Medium-R-Normal--{size}-{size * 10}-72-72-C-{cw * 10}-ISO10646-1", f"SIZE {size} 72 72",
             f"FONTBOUNDINGBOX {cw} {size} 0 0", "STARTPROPERTIES 3", 'FAMILY_NAME "Terminus"', 'COPYRIGHT "Copyright (C) 2020 Dimitar Toshkov Zhekov"',
             f"FONT_ASCENT {size}", "ENDPROPERTIES", "CHARS 95"]
    for c in range(32, 127):
        im = pf.glyph[c][3].convert("1")
        assert im.size == (cw, size), (c, im.size)
        rows = []
        for y in range(size):
            v = 0
            for x in range(8):
                v = v << 1 | (1 if x < cw and im.getpixel((x, y)) else 0)
            rows.append(f"{v:02X}")
        lines += [f"STARTCHAR U+{c:04X}", f"ENCODING {c}", "SWIDTH 500 0", f"DWIDTH {cw} 0", f"BBX {cw} {size} 0 0", "BITMAP"] + rows + ["ENDCHAR"]
    lines.append("ENDFONT")
    (OUT / f"ter-u{size}n-ascii.bdf").write_text("\n".join(lines) + "\n", encoding="latin-1")
    print(f"ter-u{size}n-ascii.bdf")
