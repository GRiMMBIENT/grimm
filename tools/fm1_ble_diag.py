#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM's BLE report over USB (a --ble build: editor command 26, firmware/src/ble/hx.c hx_dump): the link's state and
counters, the central's CONNECT_IND and last update, its last LL control and ATT opcodes, and the driver's ring of the
last 32 radio events. For debugging BLE-MIDI; paste what it prints.

  py tools/fm1_ble_diag.py              one report
  py tools/fm1_ble_diag.py --watch      a line each 2 s while it changes; a whole report each time a link ends
  py tools/fm1_ble_diag.py --selftest   the decoder against a made-up report (no FM-1 needed)

Needs mido and python-rtmidi, as tools/fm1_install.py; the FM-1 on USB (BT MIDI may be on or off).
"""
import argparse
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

BLE = 26
FIELDS = ["state", "evts", "rx_evts", "miss_max", "crc_bad", "repeats", "rx_irqs", "closes", "sup_timeouts",
          "close_reason", "close_by", "close_since_rx_us", "close_since_start_us", "close_evt", "cind_interval",
          "cind_win_off", "cind_win_size", "cind_latency", "cind_timeout", "cind_sca", "cind_hop", "chm_used",
          "upd_interval", "upd_win_off", "upd_win_size", "upd_instant", "chm_instant", "rx_last_evt", "late_instants",
          "clk_step_max_us", "first_evt", "first_rx_evt", "first_rx_us", "col7", "col16", "zoom",
          "pll_mid", "pll_bank", "pll_steps"]   # (hx.c hx_dump)
STATE = ["OFF", "NO CAL", "HELD", "ADV", "LINK", "READY"]
BY = ["local", "peer", "supervision", "establish", "procedure timeout", "protocol", "terminate unacked"]
REASON = {0x08: "connection timeout", 0x13: "peer ended it", 0x16: "we ended it", 0x22: "LL response timeout",
          0x28: "instant passed", 0x3B: "unacceptable parameters", 0x3D: "MIC failure", 0x3E: "failed to establish"}
EVENT = ["NONE", "ENABLE", "ADV_START", "ADV_STOP", "ADV_DROP", "CIND_RX", "CIND_OK", "CIND_REJ", "CONN_SET",
         "FIRST_EVT", "FIRST_RX", "RX_BAD", "RX_DESYNC", "C3_ZERO", "CTRL_RX", "CTRL_TX", "INSTANT", "CLOSE", "BUSY",
         "INIT_START", "MASTER", "INIT_HIT"]   # (ble_diag.h BDE_*)
LL_OP = {0x00: "CONN_UPDATE", 0x01: "CHANNEL_MAP", 0x02: "TERMINATE", 0x03: "ENC_REQ", 0x07: "UNKNOWN_RSP",
         0x08: "FEATURE_REQ", 0x0C: "VERSION", 0x0F: "PARAM_REQ", 0x14: "LENGTH_REQ", 0x15: "LENGTH_RSP",
         0x16: "PHY_REQ", 0x18: "PHY_UPDATE"}
ATT_OP = {0x02: "MTU", 0x04: "FIND_INFO", 0x08: "READ_BY_TYPE", 0x0A: "READ", 0x10: "READ_BY_GROUP", 0x12: "WRITE",
          0x52: "WRITE_CMD"}


def unpack7(b):
    out, i = [], 0
    while i < len(b):
        top, g = b[i], b[i + 1:i + 8]
        out += [x | ((top >> j) & 1) << 7 for j, x in enumerate(g)]
        i += 1 + len(g)
    return bytes(out)


def decode(raw):
    n = len(FIELDS)
    f = dict(zip(FIELDS, struct.unpack_from(f"<{n}I", raw)))
    o = 4 * n
    ctl, att = list(raw[o:o + 8]), list(raw[o + 8:o + 16])
    now_us, ev_n = struct.unpack_from("<II", raw, o + 16)
    ring = [struct.unpack_from("<IBBH", raw, o + 24 + 8 * k) for k in range(32)]
    k = min(ev_n, 32)
    evs = [(t, c, a) for t, c, _, a in (ring[(ev_n - k + i) % 32] for i in range(k))]   # (oldest first)
    o += 24 + 8 * 32
    f["rx_ch"] = list(struct.unpack_from("<37H", raw, o)) if len(raw) >= o + 74 else []   # (older firmware: none)
    return f, ctl, att, now_us, evs


def bitoff(f):                                  # column 7: bit 15 the drift enable, a 12-bit signed bit offset (us)
    if f["col7"] == 0xFFFFFFFF:
        return "-"
    v = f["col7"] & 0xFFF
    return f"{v - 0x1000 if v & 0x800 else v:+d}{'*' if f['col7'] & 0x8000 else ''}"


def ops(lst, names):
    return " ".join(f"{x:02X}:{names.get(x, '?')}" for x in lst if x != 0xFF) or "-"


def report(raw):
    f, ctl, att, now_us, evs = decode(raw)
    q = 100 * f["rx_evts"] // f["evts"] if f["evts"] else 0
    lines = [f"state {STATE[f['state']] if f['state'] < len(STATE) else f['state']}   bit offset {bitoff(f)} us, "
             f"slot drift {'-' if f['col16'] == 0xFFFFFFFF else f['col16']}, zoom {f['zoom']} us",
             f"PLL calibration: middle band {f['pll_mid']}, band set {f['pll_bank']}, {f['pll_steps']} measurements"
             f"{'' if f['pll_steps'] else ' (not run: the radio not started yet)'}",
             f"this link: events {f['evts']}, with a good packet {f['rx_evts']} ({q}%), longest miss run "
             f"{f['miss_max']}; bad CRC {f['crc_bad']}, repeats {f['repeats']}, RX irqs {f['rx_irqs']}",
             f"connect: interval {f['cind_interval']} ({f['cind_interval'] * 1.25:g} ms), win offset "
             f"{f['cind_win_off']}, win size {f['cind_win_size']}, latency {f['cind_latency']}, timeout "
             f"{f['cind_timeout'] * 10} ms, SCA {f['cind_sca']}, hop {f['cind_hop']}; channels used {f['chm_used']}/37",
             f"first event {f['first_evt']}, first RX event {f['first_rx_evt']} after {f['first_rx_us']} us",
             f"last update: interval {f['upd_interval']}, win offset {f['upd_win_off']}, win size "
             f"{f['upd_win_size']}, instant {f['upd_instant']}; last channel map instant {f['chm_instant']}; last good "
             f"packet at event {f['rx_last_evt']}; late instants {f['late_instants']}; clock step max "
             f"{f['clk_step_max_us']} us"]
    if f["closes"]:
        r = f["close_reason"]
        by = BY[f["close_by"]] if f["close_by"] < len(BY) else f["close_by"]
        lines.append(f"links ended {f['closes']} (supervision {f['sup_timeouts']}); the last: 0x{r:02X} "
                     f"{REASON.get(r, '')}, by {by}, at event {f['close_evt']}, {f['close_since_rx_us'] // 1000} ms "
                     f"after its last packet, {f['close_since_start_us'] // 1000} ms after it began")
    if f["rx_ch"] and sum(f["rx_ch"]):
        ch = f["rx_ch"]
        mid = sorted(c for c in ch if c)[len([c for c in ch if c]) // 2]
        lines.append("good packets by channel: " + " ".join(f"{i}:{c}" for i, c in enumerate(ch)))
        deaf = [i for i, c in enumerate(ch) if c < mid / 5]
        lines.append(f"channels hearing under a fifth of the median ({mid}): {deaf or 'none'}")
    lines.append(f"LL control in (newest first): {ops(ctl, LL_OP)}")
    lines.append(f"ATT in (newest first): {ops(att, ATT_OP)}")
    lines.append("radio events (ms before the newest one, oldest first):")
    for t, c, a in evs:
        lines.append(f"  -{(now_us - t) / 1000:9.1f}  {EVENT[c] if c < len(EVENT) else c:<10} 0x{a:04X}")
    return "\n".join(lines)


def fetch(link):
    for _ in range(3):                          # (the first message after opening a port can be lost)
        try:
            return unpack7(link.req(BLE, [], 1.5))
        except TimeoutError:
            pass
    sys.exit("no answer to the BLE report: is the FM-1 running a ./build.sh --ble build?")


def selftest():
    vals = list(range(len(FIELDS)))
    vals[0], vals[1], vals[2], vals[9], vals[10], vals[7] = 4, 200, 74, 0x08, 2, 1
    raw = struct.pack(f"<{len(FIELDS)}I", *vals) + bytes([0x01, 0x08] + [0xFF] * 6) + bytes([0x02] + [0xFF] * 7)
    raw += struct.pack("<II", 50000, 33) + b"".join(struct.pack("<IBBH", 1000 * k, k % 22, 0, k) for k in range(32))
    raw += struct.pack("<37H", *[0 if i in (5, 30) else 100 for i in range(37)])
    seven = []
    for i in range(0, len(raw), 7):             # (pack7, as editor.c ed_send)
        g = raw[i:i + 7]
        seven += [sum((b >> 7) << j for j, b in enumerate(g))] + [b & 0x7F for b in g]
    f, ctl, att, now_us, evs = decode(unpack7(seven))
    assert f["state"] == 4 and f["rx_evts"] == 74 and f["close_reason"] == 8 and f["pll_steps"] == len(FIELDS) - 1
    assert bitoff(dict(col7=0x8F9C)) == "-100*" and bitoff(dict(col7=0x0010)) == "+16" and bitoff(dict(col7=-1 & 0xFFFFFFFF)) == "-"
    assert ctl[:3] == [0x01, 0x08, 0xFF] and att[0] == 0x02 and now_us == 50000
    assert len(evs) == 32 and evs[-1] == (0, 0, 0) and evs[0] == (1000, 1, 1)   # (ev_n 33: newest slot 0, oldest 1)
    assert "37%" in report(unpack7(seven)) and "connection timeout" in report(unpack7(seven))
    assert f["rx_ch"][5] == 0 and "[5, 30]" in report(unpack7(seven))
    print("fm1_ble_diag: selftest ok")


def main():
    ap = argparse.ArgumentParser(description="GR!MM's BLE report over USB.", epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--watch", action="store_true", help="a line each 2 s while it changes; a report when a link ends")
    ap.add_argument("--selftest", action="store_true", help="check the decoder (no FM-1 needed)")
    a = ap.parse_args()
    if a.selftest:
        selftest()
        return
    from fm1_sample_upload import Link       # (mido: only with an FM-1)
    link = Link()
    raw = fetch(link)
    print(report(raw))
    if not a.watch:
        return
    f = decode(raw)[0]
    closes, last = f["closes"], None
    print("\nwatching (Ctrl-C to stop) ...")
    try:
        while True:
            time.sleep(2)
            raw = fetch(link)
            f = decode(raw)[0]
            q = 100 * f["rx_evts"] // f["evts"] if f["evts"] else 0
            line = (f"{time.strftime('%H:%M:%S')}  {STATE[f['state']] if f['state'] < len(STATE) else f['state']:<6} "
                    f"good {q:3d}%  miss run {f['miss_max']:3d}  channels {f['chm_used']:2d}  links ended {f['closes']}  "
                    f"bitoff {bitoff(f)}  zoom {f['zoom']}")
            if line[10:] != last:
                print(line)
                last = line[10:]
            if f["closes"] != closes:
                closes = f["closes"]
                print("\n--- a link ended ---\n" + report(raw) + "\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
