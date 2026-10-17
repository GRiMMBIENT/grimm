#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""Test GR!MM's editor mailbox on the FM-1 (docs/EDITOR_PROTOCOL.md): read INFO, back up all user sounds, slots A-D
and the settings to a file, write it all back, read it again and check that it matches.

  py tools\\fm1_editor_test.py                     the whole test (backup, write back, compare)
  py tools\\fm1_editor_test.py --backup FILE       only a backup into FILE
  py tools\\fm1_editor_test.py --restore FILE      write a backup back (checked first)
  py tools\\fm1_editor_test.py --store N           a factory sound into user slot N, read back, then N as it was
  py tools\\fm1_editor_test.py --live              TRACK (protocol 2): each track read, one set live and back, SELECT
  py tools\\fm1_editor_test.py --record FILE       the FM-1's own frames (requests and replies) for web/test_web.mjs

Writing needs the FM-1 stopped and silent (no note, no release, no DELAY or SPACE tail). The backup file is JSON:
{"format": "grimm-backup", "version": 1, "proto", "fmt", "firmware", "objects": [{"id", "len", "crc", "data"
(base64)}]}. Needs mido and python-rtmidi (py -m pip install mido python-rtmidi), as tools\\fm1_install.py.
"""
import argparse
import base64
import json
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_sample_upload import HDR, Link, pack7  # noqa: E402  (the FM-1's MIDI ports, GR!MM's identity checked)

INFO, GET, PUT, TRACK = 20, 21, 22, 23
BEGIN, DATA, COMMIT, ABORT = 0, 1, 2, 3
O_SET, O_SLOT, O_SND, O_FACT = 0, 1, 16, 80
RC = {1: "bad arguments", 2: "refused (CRC or contents)", 3: "stop the sequencer and let every sound end first",
      4: "the flash write failed", 5: "no BEGIN (or it timed out)"}


def u35(v):
    return [(v >> (7 * k)) & 0x7F for k in range(5)]


def rd35(b):
    return sum(x << (7 * k) for k, x in enumerate(b[:5]))


def unpack7(b):
    out, i = [], 0
    while i < len(b):
        top, g = b[i], b[i + 1:i + 8]
        out += [x | ((top >> j) & 1) << 7 for j, x in enumerate(g)]
        i += 1 + len(g)
    return bytes(out)


def info(link):
    for _ in range(3):                          # (the first message after opening a port can be lost)
        try:
            r = link.req(INFO, [], 1.5)
            break
        except TimeoutError:
            pass
    else:
        raise TimeoutError("no answer from GR!MM to INFO (a firmware before step 26a?)")
    j = r.index(0, 2)
    i = j + 1
    d = dict(proto=r[0], fmt=r[1], firmware=bytes(r[2:j]).decode("ascii", "replace"), sounds=r[i],
             sound_size=r[i + 1], factory=r[i + 2], slot_size=rd35(r[i + 3:i + 8]), set_size=rd35(r[i + 8:i + 13]),
             sample_slots=r[i + 13], samples_used=r[i + 14])
    if d["proto"] not in (1, 2):
        sys.exit(f"the FM-1 speaks editor protocol {d['proto']}; this tool knows 1 and 2")
    return d


def objects(inf):
    """(id, size, what) of everything a backup holds"""
    out = [(O_SET, inf["set_size"], "settings")]
    out += [(O_SLOT + k, inf["slot_size"], f"slot {'ABCD'[k]}") for k in range(4)]
    out += [(O_SND + k, inf["sound_size"], f"sound {k + 1}") for k in range(inf["sounds"])]
    return out


def get(link, o, size):
    data = b""
    for off in range(0, size, 256):
        c = min(256, size - off)
        r = link.req(GET, [o] + u35(off) + [c & 0x7F, c >> 7])
        if r[0] != o or r[1]:
            sys.exit(f"GET {o} at {off}: {RC.get(r[1], f'rc {r[1]}')}")
        chunk = unpack7(r[9:])
        if len(chunk) != c:
            sys.exit(f"GET {o} at {off}: {len(chunk)} bytes back, not {c}")
        data += chunk
    return data


def put(link, o, data, fmt, what):
    def step(args, timeout=3.0):
        r = link.req(PUT, args, timeout)
        if r[2] == 3:
            link.req(PUT, [ABORT, o])
            sys.exit(f"{what}: {RC[3]}")
        if r[2]:
            link.req(PUT, [ABORT, o])
            sys.exit(f"{what}: {RC.get(r[2], f'rc {r[2]}')}")
    step([BEGIN, o, fmt] + u35(len(data)) + u35(zlib.crc32(data)))
    for off in range(0, len(data), 256):
        step([DATA, o] + u35(off) + pack7(data[off:off + 256]))
    step([COMMIT, o], 10.0)


def backup(link, inf):
    objs = []
    for o, size, what in objects(inf):
        print(f"\r  reading {what:<12}", end="", flush=True)
        d = get(link, o, size)
        objs.append(dict(id=o, len=size, crc=zlib.crc32(d), data=base64.b64encode(d).decode()))
    print("\r  read: settings, slots A-D, " + f"{inf['sounds']} sounds          ")
    return dict(format="grimm-backup", version=1, proto=inf["proto"], fmt=inf["fmt"], firmware=inf["firmware"],
                date=time.strftime("%Y-%m-%d %H:%M:%S"), objects=objs)


def load(path, inf):
    b = json.loads(Path(path).read_text())
    if b.get("format") != "grimm-backup" or b.get("version") != 1:
        sys.exit(f"{path}: not a GR!MM backup")
    if b.get("fmt") != inf["fmt"]:
        sys.exit(f"{path}: object format {b.get('fmt')}, the FM-1's is {inf['fmt']}")
    sizes = {o: s for o, s, _ in objects(inf)}
    for ob in b["objects"]:
        d = base64.b64decode(ob["data"])
        if ob["id"] not in sizes or len(d) != sizes[ob["id"]] or zlib.crc32(d) != ob["crc"]:
            sys.exit(f"{path}: object {ob['id']} is damaged or not this firmware's")
    return b


def restore(link, b, inf):
    whats = {o: w for o, _, w in objects(inf)}
    for ob in b["objects"]:
        print(f"\r  writing {whats[ob['id']]:<12}", end="", flush=True)
        put(link, ob["id"], base64.b64decode(ob["data"]), inf["fmt"], whats[ob["id"]])
    print("\r  written: settings, slots A-D, " + f"{inf['sounds']} sounds          ")


def store(link, inf, n):
    """a factory sound into user slot n (1..64), read back; then the slot as it was, read back"""
    o = O_SND + n - 1
    was, fact = get(link, o, inf["sound_size"]), get(link, O_FACT, inf["sound_size"])
    for data, what in ((fact, "factory sound 1"), (was, "as it was")):
        put(link, o, data, inf["fmt"], f"sound {n}")
        if get(link, o, inf["sound_size"]) != data:
            sys.exit(f"sound {n} did not read back as written ({what})")
        print(f"  sound {n}: {what}, written and read back")


def track(link, op, t, rec=None):
    r = link.req(TRACK, [op, t] + (pack7(rec) if rec else []))
    if r[0] != op or r[1] != t or r[3]:
        sys.exit(f"TRACK op {op} track {t + 1}: {RC.get(r[3], f'rc {r[3]}')}")
    return r[2], unpack7(r[4:78]) if op == 0 else None   # (READ: then the track's name as its HOME shows it)


def live(link, inf):
    """TRACK: the four tracks read; the selected one's ENV ATTACK set live and back; another track selected and back"""
    if inf["proto"] < 2:
        sys.exit("the FM-1's GR!MM has no TRACK yet (editor protocol 1): install step 26b2 or later")
    recs = [track(link, 0, t)[1] for t in range(4)]
    sel = track(link, 0, 0)[0]
    for t, r in enumerate(recs):
        nm = bytes(link.req(TRACK, [0, t])[78:]).decode("ascii", "replace")
        print(f"  track {t + 1}: {nm or '(no name)'}, source {r[3]}, knobs {list(r[4:8])}, ENV {list(r[8:12])}")
    r = bytearray(recs[sel])
    r[8] = (r[8] + 5) & 127
    r[60:64] = zlib.crc32(bytes(r[:60])).to_bytes(4, "little")
    track(link, 1, sel, bytes(r))
    if track(link, 0, sel)[1] != bytes(r):
        sys.exit("the track did not read back as set")
    track(link, 1, sel, recs[sel])
    if track(link, 0, sel)[1] != recs[sel]:
        sys.exit("the track is not back as it was")
    print(f"  track {sel + 1}: ENV ATTACK {recs[sel][8]} -> {r[8]} -> {recs[sel][8]}, live, read back")
    if track(link, 2, (sel + 1) % 4)[0] != (sel + 1) % 4 or track(link, 2, sel)[0] != sel:
        sys.exit("SELECT did not select")
    print(f"  SELECT: track {(sel + 1) % 4 + 1}, then track {sel + 1} again")


def hexs(b):
    return " ".join(f"{x:02X}" for x in b)


def record(link, inf, path):
    """the FM-1's frames for INFO, GETs, the samples' INFO (and TRACK READ), as web/test_web.mjs replays them"""
    log, req = [], link.req

    def rec(cmd, args, timeout=3.0):
        r = req(cmd, args, timeout)
        log.append(dict(tx=hexs([0xF0] + HDR + [cmd] + list(args) + [0xF7]), rx=hexs([0xF0] + HDR + [cmd] + list(r) + [0xF7])))
        return r
    link.req = rec
    info(link)
    for o, size in ((O_SET, inf["set_size"]), (O_SND, inf["sound_size"]), (O_FACT, inf["sound_size"]),
                    (O_FACT + 2, inf["sound_size"])):
        get(link, o, size)
    try:
        link.info()
    except ValueError:                          # (before step 26b2: cut short with four long sample names)
        print("  (the samples' INFO came back cut short: recorded as it is)")
    if inf["proto"] >= 2:
        for t in range(4):
            track(link, 0, t)
    link.req = req
    with open(path, "w", newline="\n") as f:    # (LF: the file goes into the repository)
        json.dump(dict(format="grimm-frames", firmware=inf["firmware"], proto=inf["proto"],
                       date=time.strftime("%Y-%m-%d %H:%M:%S"), frames=log), f, indent=1)
        f.write("\n")
    print(f"  {len(log)} requests and their replies: {path}")


def main():
    ap = argparse.ArgumentParser(description="Test GR!MM's editor mailbox on the FM-1 (USB-MIDI SysEx).",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--backup", type=Path, metavar="FILE", help="only back up into FILE")
    ap.add_argument("--restore", type=Path, metavar="FILE", help="write a backup back")
    ap.add_argument("--store", type=int, metavar="N", help="a factory sound into user slot N, then N as it was")
    ap.add_argument("--live", action="store_true", help="TRACK: read, set live and back, SELECT")
    ap.add_argument("--record", type=Path, metavar="FILE", help="the FM-1's frames for web/test_web.mjs")
    a = ap.parse_args()
    link = Link()
    inf = info(link)
    print(f"GR!MM {inf['firmware']}: editor protocol {inf['proto']}, format {inf['fmt']}; {inf['sounds']} sound slots, "
          f"{inf['factory']} factory sounds; a slot {inf['slot_size']} B, the settings {inf['set_size']} B; samples: "
          f"{inf['samples_used']} of {inf['sample_slots']} slots in use")
    if a.restore:
        restore(link, load(a.restore, inf), inf)
        return
    if a.store:
        if not 1 <= a.store <= inf["sounds"]:
            sys.exit(f"--store: a slot 1..{inf['sounds']}")
        return store(link, inf, a.store)
    if a.live:
        return live(link, inf)
    if a.record:
        return record(link, inf, a.record)
    t0 = time.time()
    b = backup(link, inf)
    path = a.backup or Path(f"grimm-backup-{time.strftime('%Y%m%d-%H%M%S')}.json")
    path.write_text(json.dumps(b, indent=1))
    print(f"  backup: {path} ({time.time() - t0:.1f} s)")
    if a.backup:
        return
    b = load(path, inf)                         # (read back from the file: what a restore would write)
    t0 = time.time()
    restore(link, b, inf)
    print(f"  ({time.time() - t0:.1f} s)")
    again = backup(link, inf)
    bad = [ob["id"] for ob, nb in zip(b["objects"], again["objects"]) if ob["data"] != nb["data"]]
    if bad:
        sys.exit(f"MISMATCH after writing back: objects {bad}")
    print("OK: everything read back as it was written")


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is GR!MM running, and no other app using the FM-1's MIDI port?")
