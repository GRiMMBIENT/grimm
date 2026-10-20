#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""Where the FM-1's audio time goes (GR!MM V2): the editor's command 24 (CPU), read over USB about every second.

  py tools\\fm1_cpu.py              every ~1 s until Ctrl+C
  py tools\\fm1_cpu.py --once       one reading

Each line: NOW and PEAK as SYSTEM 2 shows them (percent of the 5.8 ms a 256-frame block has), then NOW split:
VOICES (the sources), MIX (the tracks' filters, levels and sends), GRAIN, DELAY, SPACE, TAPE, PERFORM, SCAN (the key
scan's timer interrupts that land inside the audio) and REST (the rest of the audio callback). Each is the average of
the same ~0.5 s as NOW. Needs mido and python-rtmidi (py -m pip install mido python-rtmidi), as tools\\fm1_install.py.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_sample_upload import Link  # noqa: E402  (the FM-1's MIDI ports, GR!MM's identity checked)

CPU = 24
NAMES = ["VOICES", "MIX", "GRAIN", "DELAY", "SPACE", "TAPE", "PERFORM", "SCAN"]


def rd35(b):
    return sum(x << (7 * k) for k, x in enumerate(b[:5]))


def reading(link):
    r = link.req(CPU, [], 1.5)
    n = r[0]
    v = [rd35(r[1 + 5 * k:6 + 5 * k]) for k in range(3 + n)]
    budget, now, peak, st = v[0], v[1], v[2], v[3:]
    pct = lambda t: 100.0 * t / budget
    names = NAMES[:n] + [f"STAGE{k}" for k in range(len(NAMES), n)]
    rest = now - sum(st)
    parts = "  ".join(f"{nm} {pct(t):4.1f}" for nm, t in zip(names, st))
    return f"NOW {pct(now):5.1f}%  PEAK {pct(peak):5.1f}%  |  {parts}  REST {pct(rest):4.1f}"


def main():
    ap = argparse.ArgumentParser(description="Where the FM-1's audio time goes (GR!MM V2).",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--once", action="store_true", help="one reading, then stop")
    a = ap.parse_args()
    link = Link()
    for _ in range(3):                          # (the first message after opening a port can be lost)
        try:
            print(reading(link))
            break
        except TimeoutError:
            pass
    else:
        sys.exit("no answer to CPU (24): a GR!MM before V2?")
    while not a.once:
        time.sleep(1.0)
        print(reading(link))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except TimeoutError as e:
        sys.exit(f"{e}: is GR!MM running, and no other app using the FM-1's MIDI port?")
