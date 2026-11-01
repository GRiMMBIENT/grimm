"""GR!MM V2: TAPE off is TAPE off (host mode, firmware/src/host.c). With TAPE's four knobs at 0 and every TAPE tile at 0, the
24-bit output (GRIMM_RAW_OUT) must be to the bit what GR!MM V2.0 RC made (tails3, tails4 alike; the reference:
tests/data/tapeoff.sha256), for a set that sounds most of it:
  T1 MONK DRONE (a pad, chords), T2 GREGORIAN (a choir), T3 a SAMPLE track (slot 1, chords and single keys, the DLY, REV
  and GRAIN sends up: DELAY, SPACE, GRAIN on), T4 CHOIR OF GHOSTS (VOICE) with GENERATE on (STORM), MASTER at 1022.
And (V2: TAPE a track's own) the same with TAPE's four knobs all up, every tile still 0.
A change meant to change this sound must make a new reference on purpose (--make), saying so in its commit.
  python3 tests/tapeoff_test.py BIN                 the check against the reference
  python3 tests/tapeoff_test.py BIN --make          the reference from BIN (tests/data/tapeoff.sha256)
  python3 tests/tapeoff_test.py BIN OTHER_BIN       the two against each other: the first frame they differ at"""
import hashlib
import os
import subprocess
import sys
from pathlib import Path

BIN = sys.argv[1] if len(sys.argv) > 1 else "build/host/grimm"
OUT = Path("build/host/tapeoff")
OUT.mkdir(parents=True, exist_ok=True)
REF = Path("tests/data/tapeoff.sha256")


def fx_key(t, bit):                             # FX held, a key tapped, FX let go
    return [(t, "down 2"), (t + 20, f"keys {1 << bit:#x}"), (t + 40, "keys 0"), (t + 60, "up 2")]


EV = [(300, "enc 6 1"),                                                   # T1: MONK DRONE (a pad)
      (400, "enc 1 1"), (500, "enc 6 36"),                                # T2: GREGORIAN
      (600, "enc 1 1"), (700, "btn 6"), (750, "btn 6"), (800, "enc 2 2"), (850, "btn 8"),   # T3: SOURCE -> SAMPLE
      (900, "btn 8"), (950, "enc 0 1"), (1000, "enc 2 80"), (1050, "enc 0 1"), (1100, "enc 2 90"),   # its DLY, REV,
      (1150, "enc 0 2"), (1200, "enc 2 70"), (1250, "btn 8"), (1280, "btn 8"),   # GRAIN sends; MIXER 2, HOME
      (1300, "enc 1 1"), (1400, "enc 6 31")]                              # T4: CHOIR OF GHOSTS
EV += fx_key(1600, 1) + fx_key(1800, 10) + fx_key(1900, 10)              # T4: GENERATE on (STORM)
EV += [(2000, "enc 1 -1"), (2100, "keys 0x4880"), (3600, "keys 0")]       # T3: a SAMPLE chord,
EV += sum(([(3700 + 300 * i, f"keys {1 << k:#x}"), (3900 + 300 * i, "keys 0")] for i, k in enumerate((0, 4, 7, 11, 14, 2))), [])
EV += [(5600, "enc 1 -2"), (5700, "keys 0x890"), (7700, "keys 0")]        # T1: a pad chord
EV += [(7800, "enc 1 1"), (7900, "keys 0x1090"), (9900, "keys 0")]        # T2: the choir
END = 13000


def render(b, name):
    open(OUT / f"{name}.txt", "w").write("".join(f"{t} {e}\n" for t, e in sorted(EV, key=lambda x: x[0])) + f"{END} end\n")
    env = dict(os.environ, GRIMM_MASTER="1022", GRIMM_RAW_OUT=str(OUT / f"{name}.raw"))
    subprocess.run([b, str(OUT / name), str(OUT / f"{name}.txt")], capture_output=True, env=env, check=True)
    return (OUT / f"{name}.raw").read_bytes()


a = render(BIN, "new")
if len(sys.argv) > 2 and sys.argv[2] == "--make":
    REF.write_text(hashlib.sha256(a).hexdigest() + "\n")
    print(f"tapeoff test: reference made from {BIN} ({len(a) // 8} frames)")
    sys.exit(0)
if len(sys.argv) > 2:
    b = render(sys.argv[2], "other")
    if a == b:
        print(f"tapeoff test: {BIN} and {sys.argv[2]} to the bit the same ({len(a) // 8} frames)")
        sys.exit(0)
    i = next(k for k in range(0, min(len(a), len(b)), 4) if a[k:k + 4] != b[k:k + 4])
    print(f"tapeoff test: they differ from frame {i // 8} ({i // 8 / 44100:.3f} s), {'R' if i // 4 % 2 else 'L'}")
    sys.exit(1)
ok = hashlib.sha256(a).hexdigest() == REF.read_text().split()[0]
print(f"  {'ok  ' if ok else 'FAIL'}  tapeoff: TAPE off, every TAPE tile 0: the output to the bit as the reference "
      f"(a pad, a choir, a SAMPLE track, VOICE, GENERATE, DELAY, SPACE, GRAIN; {len(a) // 8} frames)")
# (V2: TAPE a track's own) TAPE's four knobs all the way up (FX: TAPE, before the rest), every tile still 0: the same
EV += [(100, "btn 2")] + [(120 + 20 * k, f"enc {2 + k} 127") for k in range(4)] + [(220, "btn 6"), (240, "btn 8")]
k = hashlib.sha256(render(BIN, "knobs")).hexdigest() == REF.read_text().split()[0]
print(f"  {'ok  ' if k else 'FAIL'}  tapeoff: TAPE's WOW, SATURATION, TONE, HISS all up, every TAPE tile 0: the output "
      f"to the bit as the reference")
ok = ok and k
print(f"tapeoff test: {'passed' if ok else 'FAILED'}")
sys.exit(0 if ok else 1)
