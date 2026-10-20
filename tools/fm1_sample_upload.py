#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
# After Felucca's tools/fm1_sample_upload.py (hugelton/Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
# Hügelton Instruments, GPL-3.0): its protocol and its sample helpers (tools/sampleio.py, Felucca's, unchanged).
"""Load a WAV into one of GR!MM's four sample slots on the FM-1 (the SAMPLE source plays them), over USB-MIDI.

  py tools\\fm1_sample_upload.py SLOT file.wav [--name NAME] [--root NOTE]    SLOT 1..4
  py tools\\fm1_sample_upload.py --list                                        what each slot holds
  py tools\\fm1_sample_upload.py --erase SLOT
  py tools\\fm1_sample_upload.py SLOT file.wav --dry-run OUT.bin               no FM-1: the slot's bytes into OUT.bin

Any WAV (PCM 8/16/24/32 bit or float, any rate, mono or stereo) becomes mono, 22050 Hz, normalised, IMA ADPCM, as
GR!MM stores it (firmware/src/samples.c). A slot holds 6.64 s: a longer file is cut there (with a 20 ms fade). The
name is the file's (8 characters, A-Z 0-9 and a few signs) unless --name; the root note (the key that plays it at its
own pitch) is C4 = 60, or a note in the file name ("drone_A2.wav"), or --root.

It talks only to an FM-1 running GR!MM (it asks for the identity first: FM-1_966), and the FM-1 only writes while
nothing sounds: unlatch every note and let DELAY's and SPACE's tails end first. Needs mido and python-rtmidi
(py -m pip install mido python-rtmidi), as tools\\fm1_install.py.
"""
import argparse
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402  (Felucca's: read_any_wav, resample, to_int16, ima_encode, user_slot, note_from_name)

HDR = [0x7D, 0x47, 0x52]                        # F0 7D 47 52 ... F7: GR!MM's (Felucca's is 7D 46 4C)
SLOTS, RATE = 4, 22050
SLOT_SIZE, DATA_OFF, HDR_LEN = 72 * 1024, 512, 480
MAX_DATA = SLOT_SIZE - DATA_OFF                 # 73216 bytes of ADPCM
MAX_N = MAX_DATA * 2                            # 146432 samples: 6.64 s
MAGIC = 0x504D5347                              # "GSMP"
CMD_BEGIN, CMD_WRITE, CMD_END, CMD_ERASE, CMD_INFO = 11, 12, 13, 14, 15
RC = {1: "bad arguments", 2: "flash erase failed", 3: "flash write failed", 4: "out of order (begin again)",
      5: "header or data CRC wrong", 7: "no flash"}
BUSY = 6
BUSY_TEXT = ("the FM-1 is playing (a note, a release, DELAY's or SPACE's tail), and it only writes its flash in silence "
             "(a flash write stops the sound). Unlatch every note, wait for the tails to end, and run this again.")


def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits (Felucca's)"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def printable(b):
    return "".join(chr(c) if 32 <= c < 127 else "?" for c in b)


def slot_name(s):
    """a slot's name: 8 characters (the header's, and what the FM-1 shows), A-Z 0-9 and a few signs; longer, its
    number at the end kept ("Sample-002" -> "SAMP-002"), else its start"""
    s = re.sub(r"[^A-Z0-9 _\-.#!+]", "", s.upper()).strip()
    if len(s) > 8:
        m = re.search(r"[ _\-.]?\d+$", s)
        tail = m.group(0) if m and len(m.group(0)) < 8 else ""
        s = s[:8 - len(tail)].rstrip() + tail
    return s or "SAMPLE"


def build(path, name, root, look=True):
    """a WAV -> (header, ADPCM data, seconds, cut?) as a GR!MM slot holds it (look: sampleio.ima_encode's)"""
    sr, x = sio.read_any_wav(path)
    x = sio.resample(x, sr, RATE)
    if not x or sio.peak(x) <= 1e-6:
        raise ValueError(f"{path}: no audio (silent or empty)")
    cut = len(x) > MAX_N
    if cut:                                     # what fits, with a 20 ms fade at the cut
        x = x[:MAX_N]
        f = int(0.02 * RATE)
        for i in range(f):
            x[MAX_N - f + i] *= (f - i) / f
    s = sio.to_int16(x)
    hdr, data = sio.user_slot(name, [(s, root, 0, 127)], look)   # Felucca's slot header (one zone), then GR!MM's magic
    hdr = struct.pack("<I", MAGIC) + hdr[4:]
    assert len(hdr) == HDR_LEN and len(data) <= MAX_DATA
    return hdr, data, len(s) / RATE, cut


class Link:
    """the FM-1's MIDI ports, once its identity says GR!MM"""

    def __init__(self):
        try:
            import mido
            import fm1_install as fi
        except ImportError:
            sys.exit("needs mido: py -m pip install mido python-rtmidi")
        self.mido = mido
        backend = fi.MidoBackend()
        dev = fi.Updater(backend).find()
        if not dev:
            ins = backend.input_names()
            sys.exit("FM-1 not found (USB data cable? another app using its MIDI port?)\n  MIDI inputs: " +
                     (", ".join(ins) if ins else "(none)"))
        ident, name = dev.id.text, dev.name
        dev.link.close()
        if dev.id.loader:
            sys.exit(f"the FM-1 is in update mode ({ident}): finish the install first (tools\\fm1_install.py)")
        if not re.fullmatch(r"FM-1_966|FM-1_95\d", ident):   # (GR!MM 1.x; 95N: its builds before the release)
            sys.exit(f"the FM-1 runs {ident}, not GR!MM (FM-1_966): install GR!MM first (tools\\fm1_install.py)")
        out = fi.pair_output(name, backend.output_names())
        if not out:
            sys.exit(f"no MIDI output beside {name}")
        self.i = mido.open_input(name)
        self.o = mido.open_output(out)
        time.sleep(0.3)
        print(f"FM-1: {ident} (GR!MM) on {name}")

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no answer from GR!MM (cmd {cmd})")

    def info(self):
        for _ in range(3):                      # (the first message after opening a port can be lost)
            try:
                r = self.req(CMD_INFO, [], 1.5)
                break
            except TimeoutError:
                pass
        else:
            raise TimeoutError("no answer from GR!MM to INFO")
        try:
            n, i, out = r[0], 1, []
            for _ in range(n):
                st = r[i]
                j = r.index(0, i + 1)
                nm = printable(r[i + 1:j])
                cnt = r[j + 1] | r[j + 2] << 7 | r[j + 3] << 14
                cap = r[j + 4] | r[j + 5] << 7 | r[j + 6] << 14
                out.append((st, nm, cnt, cap))
                i = j + 7
            return out
        except (IndexError, ValueError):
            raise ValueError("malformed INFO answer from the FM-1") from None


def show(slots):
    for k, (st, nm, cnt, cap) in enumerate(slots):
        what = {0: "(empty)", 2: "(being checked)"}.get(st, f"{nm or '(no name)'}  {cnt / RATE:.2f} s")
        print(f"  slot {k + 1}: {what}")
    if slots:
        print(f"  (a slot holds {slots[0][3] / RATE:.2f} s; {len(slots)} slots)")


def check(r, what):
    rc = r[-1]
    if rc == BUSY:
        sys.exit(f"{what}: {BUSY_TEXT}")
    if rc:
        sys.exit(f"{what} failed: {RC.get(rc, f'rc {rc}')}")


def main():
    ap = argparse.ArgumentParser(description="Load a WAV into a GR!MM sample slot on the FM-1 (USB-MIDI).",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("slot", nargs="?", type=int, help="1..4")
    ap.add_argument("wav", nargs="?", type=Path)
    ap.add_argument("--name", help="the slot's name (8 characters)")
    ap.add_argument("--root", type=int, help="the root note (MIDI, C4 = 60)")
    ap.add_argument("--list", action="store_true", help="what each slot holds")
    ap.add_argument("--erase", type=int, metavar="SLOT", help="empty a slot")
    ap.add_argument("--dry-run", type=Path, metavar="OUT", help="no FM-1: write the slot's bytes (header, data) to OUT")
    a = ap.parse_args()

    if a.list or a.erase:
        link = Link()
        if a.erase:
            if not 1 <= a.erase <= SLOTS:
                sys.exit("SLOT is 1..4")
            check(link.req(CMD_ERASE, [a.erase - 1], 10.0), "erase")
            print(f"slot {a.erase}: erased")
        show(link.info())
        return
    if a.slot is None or a.wav is None:
        ap.error("give SLOT and file.wav (or --list / --erase SLOT)")
    if not 1 <= a.slot <= SLOTS:
        sys.exit("SLOT is 1..4")
    name = slot_name(a.name if a.name is not None else a.wav.stem)
    root = a.root if a.root is not None else sio.note_from_name(a.wav.stem)
    if not 0 <= root <= 127:
        sys.exit("--root is a MIDI note, 0..127")
    try:
        hdr, data, secs, cut = build(a.wav, name, root)
    except (OSError, ValueError, struct.error) as e:
        sys.exit(f"cannot read {a.wav}: {e}")
    print(f"{a.wav.name}: {secs:.2f} s at 22050 Hz mono -> {len(data)} B ADPCM, name {name}, root {root}" +
          (f"  (cut to {MAX_N / RATE:.2f} s: what a slot holds)" if cut else ""))
    if a.dry_run:
        a.dry_run.write_bytes(hdr + b"\xFF" * (DATA_OFF - HDR_LEN) + data)
        print(f"dry run: the slot's bytes in {a.dry_run}")
        return

    link = Link()
    check(link.req(CMD_BEGIN, [a.slot - 1]), "begin")
    t0 = time.time()
    for off in range(0, len(data), 256):
        o = DATA_OFF + off
        r = link.req(CMD_WRITE, [a.slot - 1, o & 0x7F, (o >> 7) & 0x7F, (o >> 14) & 0x7F] + pack7(data[off:off + 256]))
        check(r, f"write at {o:#x}")
        print(f"\r  {min(off + 256, len(data))} / {len(data)} B", end="", flush=True)
    print()
    check(link.req(CMD_END, [a.slot - 1] + pack7(hdr), 5.0), "end")
    for _ in range(50):                         # the FM-1 checks and indexes it (well under a second)
        slots = link.info()
        if slots[a.slot - 1][0] != 2:
            break
        time.sleep(0.1)
    print(f"  {time.time() - t0:.1f} s")
    show(slots)
    if slots[a.slot - 1][0] != 1:
        sys.exit(f"slot {a.slot} did not check out: upload it again")


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is GR!MM running, and no other app using the FM-1's MIDI port?")
