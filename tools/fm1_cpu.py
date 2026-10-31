#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""Where the FM-1's audio time goes (GR!MM V2): the editor's command 24 (CPU), read over USB about every second.

  py tools\\fm1_cpu.py              every ~1 s until Ctrl+C
  py tools\\fm1_cpu.py --once       one reading
  py tools\\fm1_cpu.py --health     also the health line (rc11): the watchdog's margin, the key scan's longest gap, the
                                  flash's longest interrupts-off time, USB resets, the supply, how it last started
  py tools\\fm1_cpu.py --clear      the peaks and counts start over (then as --health)

Each line: NOW and PEAK as SYSTEM 2 shows them (percent of the 5.8 ms a 256-frame block has), then NOW split:
VOICES (the sources), MIX (the tracks' filters, levels and sends), GRAIN, DELAY, SPACE, TAPE, PERFORM, MASTER (its
filter), DRIVE (its distortion and limiter), SCAN (the key scan's timer interrupts that land inside the audio) and REST
(the rest of the audio callback). Each is the average of the same ~0.5 s as NOW. Needs mido and python-rtmidi (py -m pip install mido python-rtmidi), as tools\\fm1_install.py.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_sample_upload import Link  # noqa: E402  (the FM-1's MIDI ports, GR!MM's identity checked)

CPU = 24
NAMES = ["VOICES", "MIX", "GRAIN", "DELAY", "SPACE", "TAPE", "PERFORM", "MASTER", "DRIVE", "SCAN"]


def rd35(b):
    return sum(x << (7 * k) for k, x in enumerate(b[:5]))


HL = ["uac_under", "uac_over", "uac_missed", "uac_stalls", "usb_resets", "usb_suspends", "usb_frame_stalls", "wdt_gap",
      "tick_gap", "late", "stall", "lost", "tick_dur", "irq_off", "irq_off_slow", "bat_lo", "bat_hi", "boots", "unexpected",
      "wdt_resets", "low_resets", "why", "boot", "faults", "uptime_ms"]
US = ("wdt_gap", "tick_gap", "tick_dur", "irq_off")                 # (TIMER4 ticks, 24 a us)


def health(r, n):
    """The rc11 health values after the USB drops, as a dict (empty: an older GR!MM)."""
    at = 1 + 5 * (4 + n)
    if len(r) < at + 5 * len(HL):
        return {}
    h = dict(zip(HL, (rd35(r[at + 5 * k:at + 5 * k + 5]) for k in range(len(HL)))))
    for k in US:
        h[k] /= 24.0                                                # (now microseconds)
    return h


def health_line(h):
    if not h:
        return "health: this GR!MM has none (before rc11)"
    why = " ".join(f"{(h['why'] >> s) & 255:02x}" for s in (24, 16, 8, 0))
    return (f"watchdog: longest gap between feeds {h['wdt_gap'] / 1000:.1f} ms (it bites at ~8000)  |  key scan tick: "
            f"longest gap {h['tick_gap']:.0f} us (10 kHz = 100), past 0.2 ms {h['late']}, past 1 ms {h['stall']}, past 10 ms "
            f"{h['lost']}, longest tick {h['tick_dur']:.0f} us  |  flash: interrupts off at most {h['irq_off'] / 1000:.1f} ms, "
            f"{h['irq_off_slow']} times past 1 ms  |  USB: bus resets {h['usb_resets']}, suspends {h['usb_suspends']}, frozen "
            f"frames {h['usb_frame_stalls']}, host stopped reading {h['uac_stalls']}, ring dry {h['uac_under']}, full "
            f"{h['uac_over']}, no packet {h['uac_missed']}  |  battery divider {h['bat_lo']} .. {h['bat_hi']}  |  starts since "
            f"power-on {h['boots']}, resets nobody asked for {h['unexpected']} (watchdog {h['wdt_resets']}, supply low "
            f"{h['low_resets']}), faults {h['faults']}; this start: {['asked for', 'WATCHDOG', 'LOW VOLTAGE'][min(h['boot'] >> 8, 2)]}, "
            f"chip's flags {h['boot'] & 255:02x} (last four {why}; 01 power-on, 02 VDDIO low, 04 watchdog, 08 VCM, 10 long press, "
            f"20 1.2 V low, 40 soft: the chip keeps the watchdog's after a restart of ours too)")


def reading(link, clear=False, hl=False):
    r = link.req(CPU, [1] if clear else [], 1.5)
    n = r[0]
    v = [rd35(r[1 + 5 * k:6 + 5 * k]) for k in range(3 + n)]
    budget, now, peak, st = v[0], v[1], v[2], v[3:]
    pct = lambda t: 100.0 * t / budget
    names = NAMES[:n] + [f"STAGE{k}" for k in range(len(NAMES), n)]
    rest = now - sum(st)
    parts = "  ".join(f"{nm} {pct(t):4.1f}" for nm, t in zip(names, st))
    drops = rd35(r[1 + 5 * (3 + n):6 + 5 * (3 + n)]) if len(r) >= 6 + 5 * (3 + n) else None   # (rc9's)
    return (f"NOW {pct(now):5.1f}%  PEAK {pct(peak):5.1f}%  |  {parts}  REST {pct(rest):4.1f}" +
            (f"  |  USB REC DROPS {drops}" if drops is not None else "") +
            (f"\n{health_line(health(r, n))}" if hl else ""))


def main():
    ap = argparse.ArgumentParser(description="Where the FM-1's audio time goes (GR!MM V2).",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--once", action="store_true", help="one reading, then stop")
    ap.add_argument("--health", action="store_true", help="also the health line (rc11)")
    ap.add_argument("--clear", action="store_true", help="the health peaks and counts start over, then as --health")
    a = ap.parse_args()
    link = Link()
    for _ in range(3):                          # (the first message after opening a port can be lost)
        try:
            print(reading(link, a.clear, a.health or a.clear))
            break
        except TimeoutError:
            pass
    else:
        sys.exit("no answer to CPU (24): a GR!MM before V2?")
    while not a.once:
        time.sleep(1.0)
        print(reading(link, False, a.health or a.clear))


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
    except TimeoutError as e:
        sys.exit(f"{e}: is GR!MM running, and no other app using the FM-1's MIDI port?")
