#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V3.0: the editor's object "song" (11; docs/EDITOR_PROTOCOL.md) in host mode (the script's "sx" lines put an editor
frame in as from USB, the replies are printed): read (an empty chain at first), written (applies at once), refused when
its bytes are out of range or its CRC is wrong, refused while the sequencer plays (rc 3; the PUT stays open: stop,
COMMIT again), readable while playing, kept by the autosave (a restart with the saved sectors reads it back), and the
mutes and solos (bytes 17..19) left alone. A MODE written is the one PLAY follows (SONG: one pass, then stopped).
  python3 tests/song_test.py [build/host/grimm]"""
import os
import subprocess
import sys
import zlib
from pathlib import Path

EXE = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/song")
OUT.mkdir(parents=True, exist_ok=True)
BTN_PLAY = 12
OBJ_SONG, SIZE = 11, 20
bad = 0


def check(what, ok, detail=""):
    global bad
    print(f"  {'ok' if ok else 'FAIL':4s}  song: {what}" + (f" ({detail})" if detail and not ok else ""))
    bad += not ok


def pack7(b):
    o = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        o.append(sum((x >> 7) << j for j, x in enumerate(g)))
        o += [x & 0x7F for x in g]
    return o


def unpack7(b):
    o = []
    for i in range(0, len(b), 8):
        g = b[i:i + 8]
        o += [(g[1 + j] | (g[0] >> j & 1) << 7) for j in range(len(g) - 1)]
    return o


def u35(v):
    return [(v >> (7 * k)) & 0x7F for k in range(5)]


def song(steps, mode):
    """steps: [(slot 0..3 or 4 END, repeats 1..16)] (the rest END), mode -> the object's 20 bytes"""
    s = list(steps) + [(4, 1)] * (16 - len(steps))
    return bytes([p | (r - 1) << 3 for p, r in s] + [mode, 0, 0, 0])


class Script:
    def __init__(self):
        self.ev, self.t, self.n = [], 1000, 0

    def sx(self, *b):                           # -> the index of its reply
        self.t += 20
        self.ev.append((self.t, "sx " + " ".join(f"{x:02X}" for x in (0x7D, 0x47, 0x52) + b)))
        self.n += 1
        return self.n - 1

    def get(self):
        return self.sx(21, OBJ_SONG, *u35(0), SIZE, 0)

    def put(self, data, crc=None, fmt=1, length=None, commit=True):
        data = bytes(data)
        crc = zlib.crc32(data) if crc is None else crc
        i = self.sx(22, 0, OBJ_SONG, fmt, *u35(len(data) if length is None else length), *u35(crc))
        self.sx(22, 1, OBJ_SONG, *u35(0), *pack7(list(data)))
        if commit:
            self.commit()
        return i, i + 1, i + 2                 # (BEGIN, DATA, COMMIT)

    def commit(self):
        return self.sx(22, 2, OBJ_SONG)

    def at(self, t, what):
        self.ev.append((t, what))

    def wait(self, ms):
        self.t += ms

    def tap_play(self):
        self.t += 50
        self.ev.append((self.t, f"btn {BTN_PLAY}"))
        self.t += 800                           # (PLAY on or off: let it settle)


def run(name, sc, end, env=None):
    path = OUT / f"{name}.txt"
    path.write_text("".join(f"{t} {s}\n" for t, s in sorted(sc.ev, key=lambda e: e[0])) + f"{end} end\n")
    out = subprocess.run([EXE, str(OUT / name), str(path)], capture_output=True, text=True, check=True,
                         env=dict(os.environ, **(env or {}))).stdout
    rep = [[int(x, 16) for x in ln.split(" sysex out ")[1].split()] for ln in out.splitlines() if " sysex out " in ln]
    plays = {int(ln.split()[1]): int(ln.split(" play ")[1].split()[0]) for ln in out.splitlines() if ln.startswith("probe ")}
    return rep, plays


def rc_put(r):                                  # F0 7D 47 52 22 op obj rc F7
    return r[7]


def got(r):                                     # a GET's reply: (rc, the object's bytes)
    return r[6], bytes(unpack7(r[14:-1])) if r[6] == 0 else b""


EMPTY = song([], 0)
C1 = song([(0, 2), (1, 1), (2, 4), (3, 16)], 1)
C2 = song([(3, 1), (0, 16), (1, 3)] + [(2, 5)] * 4, 2)
C3 = song([(1, 3), (2, 7)], 1)

sc = Script()
g0 = sc.get()                                   # at power-on: no chain, MODE OFF
p1 = sc.put(C1)
g1 = sc.get()
p_slot = sc.put(song([(5, 1)], 0)[:16] + bytes([0, 0, 0, 0]))    # a step with a slot past END
p_mode = sc.put(song([(0, 1)], 3))              # MODE 3
p_crc = sc.put(C2, crc=zlib.crc32(C2) ^ 1)      # the wrong CRC
p_fmt = sc.put(C2, fmt=2)                       # another format
p_len = sc.put(C2, length=19)                   # the wrong length
p_hi = sc.put(bytes([0x80] + list(C2[1:])))     # a step byte past 7 bits
g2 = sc.get()                                   # unchanged by all of them
sc.tap_play()                                   # PLAY: the sequencer plays
sc.at(sc.t, "probe")
p_play = sc.put(C2, commit=False)               # BEGIN refused while playing (rc 3); DATA: no BEGIN
c_play = sc.commit()                            # no PUT open
g3 = sc.get()                                   # readable while playing, as it was
sc.tap_play()                                   # STOP
sc.at(sc.t, "probe")
p_open = sc.put(C2, commit=False)               # BEGIN, DATA
sc.tap_play()                                   # PLAY with a PUT open ..
c_ref = sc.commit()                             # .. COMMIT refused while it plays ..
sc.tap_play()                                   # .. STOP
c_ok = sc.commit()                              # .. COMMIT again: taken
g4 = sc.get()                                   # C2
end = sc.t + 9000
rep, _ = run("a", sc, end, env={"GRIMM_SAVE_OUT": str(OUT / "saves.bin")})
n = sc.n
check("every request got one reply", len(rep) == n, f"{len(rep)} replies for {n} requests")
if len(rep) == n:
    r = rep
    rcs = lambda p: [rc_put(r[i]) for i in p]
    check("power-on: GET reads 20 bytes, an empty chain (END, REPEATS 1), MODE OFF", got(r[g0]) == (0, EMPTY), str(got(r[g0])))
    check("PUT of a chain (A x2, B, C x4, D x16, END; MODE SONG): BEGIN, DATA, COMMIT all rc 0", rcs(p1) == [0, 0, 0], str(rcs(p1)))
    check("... and GET reads exactly it back", got(r[g1]) == (0, C1), str(got(r[g1])))
    check("a slot past END, MODE 3, a bad CRC, a step past 7 bits: the COMMIT is refused (rc 2); another format and a wrong "
          "length: the BEGIN is (rc 1); nothing changes",
          rc_put(r[p_slot[2]]) == 2 and rc_put(r[p_mode[2]]) == 2 and rc_put(r[p_crc[2]]) == 2 and rc_put(r[p_hi[2]]) == 2 and
          rc_put(r[p_fmt[0]]) == 1 and rc_put(r[p_len[0]]) == 1 and got(r[g2]) == (0, C1),
          f"{rc_put(r[p_slot[2]])} {rc_put(r[p_mode[2]])} {rc_put(r[p_crc[2]])} {rc_put(r[p_hi[2]])} {rc_put(r[p_fmt[0]])} "
          f"{rc_put(r[p_len[0]])} {got(r[g2])}")
    check("while the sequencer plays: a PUT's BEGIN is refused (rc 3), the DATA and COMMIT after it have no BEGIN (rc 5)",
          rc_put(r[p_play[0]]) == 3 and rc_put(r[p_play[1]]) == 5 and rc_put(r[c_play]) == 5,
          f"{rcs(p_play[:2])} {rc_put(r[c_play])}")
    check("... GET still reads the song while it plays", got(r[g3]) == (0, C1), str(got(r[g3])))
    check("a PUT open (stopped), then PLAY: COMMIT is refused (rc 3), the PUT stays open: STOP, COMMIT again: taken",
          rcs(p_open[:2]) == [0, 0] and rc_put(r[c_ref]) == 3 and rc_put(r[c_ok]) == 0,
          f"{rcs(p_open[:2])} {rc_put(r[c_ref])} {rc_put(r[c_ok])}")
    check("... and the song is the new chain (MODE SONG LOOP) at once", got(r[g4]) == (0, C2), str(got(r[g4])))
else:
    for i, x in enumerate(rep):
        print(i, x)

# the autosave kept it: a restart on the saved sectors reads it back
sc = Script()
sc.get()
rep2, _ = run("b", sc, 4000, env={"GRIMM_SAVE_IN": str(OUT / "saves.bin")})
check("the autosave keeps the song: a restart on the saved sectors reads the chain and MODE back",
      len(rep2) == 1 and got(rep2[0]) == (0, C2), str(got(rep2[0])) if rep2 else "no reply")

# the mutes and solos (bytes 17..19) are the FM-1's own: a write leaves them alone
sc = Script()
sc.put(bytes(C3[:17]) + bytes([0xFF, 0x7F, 0x7F]))
sc.get()
rep3, _ = run("c", sc, 4000)
check("a write takes the chain and MODE; bytes 17..19 (mutes and solos) it leaves as they are",
      len(rep3) == 4 and rc_put(rep3[2]) == 0 and got(rep3[3]) == (0, C3), str(got(rep3[3])) if len(rep3) == 4 else "no reply")

# MODE is what PLAY follows: SONG plays the chain once (A x1, END) and stops by itself; OFF plays on
for name, mode, want in (("d", 1, (1, 0)), ("e", 0, (1, 1))):
    sc = Script()
    sc.put(song([(0, 1)], mode))
    sc.at(sc.t + 100, f"btn {BTN_PLAY}")
    for t in (1000, 6000):
        sc.at(sc.t + t, "probe")
    _, p = run(name, sc, sc.t + 6500)
    check({1: "written MODE SONG, chain A x1: PLAY runs, and stops by itself after the pass",
           0: "written MODE OFF: PLAY plays on"}[mode], tuple(p[t] for t in sorted(p)) == want, str(p))

print(f"song test: {'FAILED' if bad else 'passed'}")
sys.exit(1 if bad else 0)
