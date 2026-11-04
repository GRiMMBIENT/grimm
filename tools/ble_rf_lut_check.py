#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The BLE start-up's captured RF-die LUT (firmware/hal/ble_rf_tables.h, ble_rf_prog: ops 0x8A / 0x91) against the
JieLi AC79 SDK's wl_rfd_ram_lut (apps/common/net/wifi_conf.c, the active table). Read only.

  python3 tools/ble_rf_lut_check.py [SDK]      (SDK: default ~/fw-AC79_AIoT_SDK)

The FEQ entries (0..127) should equal the SDK's but for bits [15:9] of their second word (command 13): the VCO band
stock wrote there for each frequency, which firmware/hal/fm1_ble_rf.h fm1_lut_word replaces with this chip's.
"""
import re
import sys
from pathlib import Path

HDR = Path(__file__).resolve().parent.parent / "firmware" / "hal" / "ble_rf_tables.h"
LEN = {0x88: 3, 0x89: 2, 0x8B: 3, 0x8C: 3, 0x8D: 5, 0x8E: 5, 0x8F: 3, 0x90: 2}   # (fm1_ble_rf.h fm1_ble_rf_exec)


def captured():
    src = HDR.read_text()
    body = src[src.index("ble_rf_prog["):]
    p = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body[body.index("{") + 1:body.index("};")])]
    u32 = lambda i: p[i] | p[i + 1] << 8 | p[i + 2] << 16 | p[i + 3] << 24
    lut, pc = {}, 0
    while pc < len(p):
        op = p[pc]
        if op < 0x80:
            pc += 5
        elif op & 0xF8 == 0x80:                     # window write (3) / read (2)
            pc += 3 if op & 0xFC == 0x80 else 2
        elif op == 0x8A:
            for i in range(p[pc + 3]):
                lut[(p[pc + 2] + i) & 0xFF, p[pc + 1]] = u32(pc + 4 + 4 * i)
            pc += 4 + 4 * p[pc + 3]
        elif op == 0x91:
            n, na, nb = p[pc + 4] + 1, p[pc + 5], p[pc + 6]
            da, db = pc + 7, pc + 7 + 4 * na
            ix, wa, wb = db + 4 * nb, 0, 0
            for i in range(n):
                wa = (wa + u32(da + 4 * p[ix + 2 * i])) & 0xFFFFFFFF
                wb = (wb + u32(db + 4 * p[ix + 2 * i + 1])) & 0xFFFFFFFF
                lut[(p[pc + 3] + i) & 0xFF, p[pc + 1]] = wa
                lut[(p[pc + 3] + i) & 0xFF, p[pc + 2]] = wb
            pc = ix + 2 * n
        elif op == 0x92:
            pc += 4 + p[pc + 2] + p[pc + 3 + p[pc + 2]] * p[pc + 2]
        elif op in LEN:
            pc += LEN[op]
        else:
            break
    return lut


def sdk_table(sdk):
    text = (Path(sdk) / "apps" / "common" / "net" / "wifi_conf.c").read_text().splitlines()
    start = [i for i, ln in enumerate(text) if "wl_rfd_ram_lut [256][2]" in ln][-1]   # the #else one: the active
    rows = []
    for ln in text[start + 1:]:
        if ln.strip().startswith("};"):
            break
        m = re.findall(r"0x[0-9A-Fa-f]+", ln.split("//")[0])
        if len(m) >= 2:
            rows.append((int(m[0], 16), int(m[1], 16)))
    return rows


def main():
    sdk = sys.argv[1] if len(sys.argv) > 1 else str(Path.home() / "fw-AC79_AIoT_SDK")
    lut, rows = captured(), sdk_table(sdk)
    h = sum(lut[e, 14] == rows[e][0] for e in range(128))
    l = sum(lut[e, 13] & ~(0x7F << 9) == rows[e][1] for e in range(128))
    bands = [lut[e, 13] >> 9 & 0x7F for e in range(128)]
    print(f"FEQ entries: first words equal {h}/128, second words equal but for bits [15:9] {l}/128")
    print(f"captured bands (stock's run in the emulator): {bands[0]} .. {bands[-1]}")
    sys.exit(0 if h == l == 128 else 1)


if __name__ == "__main__":
    main()
