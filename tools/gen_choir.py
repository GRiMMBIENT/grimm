#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""GR!MM V2: VOICE's choir TYPEs' wavetables (firmware/gen/grimm_choir.h) from real singers: VocalSet.

  VocalSet: A Singing Voice Dataset. Julia Wilkins, Prem Seetharaman, Alison Wahl, Bryan Pardo (Northwestern
  University), 2018. https://doi.org/10.5281/zenodo.1442513 . CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).

Used: the straight long tones (FULL/<singer>/long_tones/straight/*_long_straight_<vowel>.wav: each singer holds three
notes on each vowel, without vibrato; men C3 C4 F4, women C4 C5 F5). Changes made (CC BY 4.0, 3(a)(1)(B)): of the
singers below, the two lower notes on A E I O U, their steady middle measured into the strengths of their first 64
harmonics (averaged over ~0.3 s windows), given one phase pattern a singer (their A's, so the vowels morph without
cancelling), each made as loud as the others, and drawn as one cycle at four sizes (256, 128, 64, 32 samples: 64, 32,
16, 8 harmonics, band-limited for higher notes), 8 bits a sample. E, I and U made clearer (A and O as measured): soft
straight tones keep a vowel's resonances low, and on the higher note singers bring the vowels together; so their
harmonics are read off the note's mean envelope (the singer's five vowels, dB between the harmonics) plus the vowel's
own shape from the lower note, where it is sung clearly (how its envelope differs from that note's mean, under ~4 kHz:
the first three resonances, their peaks twice as high).

  python3 tools/gen_choir.py fetch DIR      VocalSet's straight long tones (100 files, ~84 MB) into DIR, from Zenodo
                                            by HTTP ranges (not the whole 2 GB archive)
  python3 tools/gen_choir.py make DIR       the tables from DIR -> firmware/gen/grimm_choir.h (numpy)
"""
import io
import re
import sys
import urllib.request
import zipfile
import wave
from pathlib import Path

URL = "https://zenodo.org/api/records/1442513/files/VocalSet11.zip/content"   # (VocalSet 1.1)
OUT = Path(__file__).resolve().parent.parent / "firmware" / "gen" / "grimm_choir.h"
VOWELS = "aeiou"
NH = 64                                         # harmonics measured (the largest cycle's)
SIZES = (256, 128, 64, 32)                      # a cycle's sizes, harmonics: size / 4
ZONES = {"m": (130.8, 261.6), "f": (261.6, 523.3)}   # the notes used: men C3 C4, women C4 C5 (Hz)
NSING = 3                                       # singers a voice (men, women): the cleanest by their harmonics
CLEAR = "eiu"                                   # the vowels made clearer (A, O: as measured: the factory sounds' own)
PEAKS = 2.0                                     # their resonances' peaks over the singer's mean, x (1: as sung)


# ---- fetch: the archive's straight long tones, by HTTP ranges
class _Http(io.RawIOBase):
    def __init__(self, size):
        self.pos, self.size = 0, size

    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0:
            return 0
        req = urllib.request.Request(URL, headers={"Range": f"bytes={self.pos}-{self.pos + n - 1}"})
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
        b[:len(data)] = data
        self.pos += len(data)
        return len(data)


def fetch(d):
    with urllib.request.urlopen(urllib.request.Request(URL, method="HEAD"), timeout=60) as r:
        size = int(r.headers["Content-Length"])
    z = zipfile.ZipFile(io.BufferedReader(_Http(size), buffer_size=1 << 20))
    d.mkdir(parents=True, exist_ok=True)
    for i in z.infolist():
        if re.fullmatch(r"FULL/[a-z]+\d+/long_tones/straight/[^/]+\.wav", i.filename):
            p = d / Path(i.filename).name
            if not (p.exists() and p.stat().st_size == i.file_size):
                p.write_bytes(z.read(i))
                print("fetched", p.name)


# ---- make: measure, choose, draw
def load(p):
    import numpy as np
    w = wave.open(str(p))
    sr, n, ch, sw = w.getframerate(), w.getnframes(), w.getnchannels(), w.getsampwidth()
    x = np.frombuffer(w.readframes(n), dtype={2: np.int16, 4: np.int32}[sw]).astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return sr, x / (2 ** (8 * sw - 1))


def f0_at(x, sr, near):
    """the pitch of x (a steady stretch) near `near` Hz: autocorrelation, refined by the harmonics' peaks"""
    import numpy as np
    W = len(x)
    ac = np.fft.irfft(np.abs(np.fft.rfft(x * np.hanning(W), 2 * W)) ** 2)[:W]
    lo, hi = int(sr / (near * 1.25)), int(sr / (near * 0.8))
    k = lo + int(np.argmax(ac[lo:hi]))
    a, b, c = ac[k - 1], ac[k], ac[k + 1]
    return sr / (k + 0.5 * (a - c) / (a - 2 * b + c))


def stretch(x, sr, f):
    """the steady middle of the note sung at ~f Hz (within 80 cents): the pitch every 20 ms (autocorrelation over 70 ..
    1100 Hz), runs of it within 60 cents, the longest run near f, its onset and ending left out"""
    import numpy as np
    H, W = int(0.02 * sr), int(0.06 * sr)
    tr = []
    for i in range(0, len(x) - W, H):
        fr = x[i:i + W] * np.hanning(W)
        if np.sqrt((fr ** 2).mean()) < 1e-3:
            tr.append(0.0)
            continue
        ac = np.fft.irfft(np.abs(np.fft.rfft(fr, 2 * W)) ** 2)[:W]
        lo, hi = int(sr / 1100), int(sr / 70)
        k = lo + int(np.argmax(ac[lo:hi]))
        tr.append(sr / k if ac[k] > 0.3 * ac[0] else 0.0)
    runs, cur = [], []
    for j, p in enumerate(tr + [0.0]):
        if p and cur and abs(1200 * np.log2(p / np.median([tr[c] for c in cur]))) < 60:
            cur.append(j)
        else:
            if len(cur) >= 25:
                runs.append(cur)
            cur = [j] if p else []
    runs = [r for r in runs if abs(1200 * np.log2(np.median([tr[c] for c in r]) / f)) < 80]
    if not runs:
        return None
    r = max(runs, key=len)
    a, b, cut = r[0] * H, r[-1] * H + W, int(0.15 * sr)
    return x[a + cut:b - cut] if b - a > 2 * cut + int(0.4 * sr) else None


def harmonics(x, sr, near):
    """the first NH harmonics' strengths (averaged over ~0.3 s windows), the middle window's phases (the fundamental
    at 0), the harmonic-to-noise ratio (dB)"""
    import numpy as np
    W = int(0.3 * sr)
    starts = list(range(0, len(x) - W, W // 2))[:8]
    mags, phs, hnr = [], None, []
    for j, s in enumerate(starts):
        seg = x[s:s + W]
        f0 = f0_at(seg, sr, near)
        n = np.arange(W)
        w = np.hanning(W)
        ks = np.arange(1, NH + 1)
        z = np.exp(-2j * np.pi * np.outer(ks * f0 / sr, n)) @ (seg * w)
        z[ks * f0 >= sr / 2 * 0.95] = 0
        mags.append(np.abs(z))
        if j == len(starts) // 2:
            phs = np.angle(z) - ks * np.angle(z[0])
        h = np.real(np.exp(2j * np.pi * np.outer(n, ks * f0 / sr)) @ (2 * z / w.sum()))   # (the harmonics alone)
        hnr.append(10 * np.log10(((h * w) ** 2).sum() / max((((seg - h) * w) ** 2).sum(), 1e-20)))
    return np.mean(mags, axis=0), phs, float(np.median(hnr))


def make(d):
    import numpy as np
    sets = {}                                   # (singer, zone) -> {vowel: (mags, phases, hnr)}
    for p in sorted(d.glob("*_long_straight_*.wav")):
        m = re.fullmatch(r"([mf])(\d+)_long_straight_([aeiou])\.wav", p.name)
        if not m:
            continue
        sex, num, v = m.group(1), int(m.group(2)), m.group(3)
        sr, x = load(p)
        for z, f in enumerate(ZONES[sex]):
            seg = stretch(x, sr, f)
            if seg is None:
                print("  no steady", ["lower", "higher"][z], "note:", p.name)
                continue
            sets.setdefault((sex, num, z), {})[v] = harmonics(seg, sr, f)
    chosen = {}
    for sex in "mf":
        score = {}
        for num in sorted({k[1] for k in sets if k[0] == sex}):
            vs = [sets.get((sex, num, z), {}) for z in (0, 1)]
            if all(len(s) == 5 for s in vs):
                score[num] = float(np.median([h for s in vs for _, _, h in s.values()]))
        chosen[sex] = sorted(score, key=lambda n: -score[n])[:NSING]
        print(sex, "singers (their notes' median harmonic-to-noise ratio, dB):",
              ", ".join(f"{sex}{n} {score[n]:.1f}" for n in sorted(score, key=lambda n: -score[n])))
    def env(mags, f0, f):
        """the envelope (dB) of harmonics `mags` at f0, at frequencies f: between harmonics linear in dB; under the
        first, the first; over the last measured, falling 12 dB an octave"""
        kmax = int(min(NH, np.floor(22050 * 0.95 / f0)))
        hf = f0 * np.arange(1, kmax + 1)
        db = 20 * np.log10(np.maximum(mags[:kmax], 1e-6))
        out = np.interp(f, hf, db)
        hi = f > hf[-1]
        out[hi] = db[-1] - 12 * np.log2(f[hi] / hf[-1])
        return out

    def unit(m):
        return m / np.sqrt((m ** 2).sum() / 2)

    tabs, scale = [], []                        # [sex][singer][zone][vowel] -> (len(SIZES) cycles)
    for sex in "mf":
        tabs.append([])
        for num in chosen[sex]:
            tabs[-1].append([])
            lo = sets[(sex, num, 0)]
            for z in (0, 1):
                s = sets[(sex, num, z)]
                fz = ZONES[sex][z]
                ph = s["a"][1]                  # (one phase pattern a singer and note: its A's)
                f = fz * np.arange(1, NH + 1)
                mean_z = np.mean([env(unit(s[w][0]), fz, f) for w in VOWELS], axis=0)
                mean_lo = np.mean([env(unit(lo[w][0]), ZONES[sex][0], f) for w in VOWELS], axis=0)
                row = []
                for v in VOWELS:
                    if v in CLEAR:              # (the vowel's own shape from the lower note, its peaks raised, F1 .. F3)
                        dev = env(unit(lo[v][0]), ZONES[sex][0], f) - mean_lo
                        dev = np.where(dev > 0, dev * PEAKS, dev) * np.clip((5000.0 - f) / 1500.0, 0.0, 1.0)
                        mg = 10 ** ((mean_z + dev) / 20)
                        mg[f >= 22050 * 0.95] = 0
                        mg[s[v][0] == 0] = 0    # (as measured: over the recording's top, none)
                    else:
                        mg = s[v][0]
                    mg = unit(mg)               # (each as loud: RMS 1)
                    cyc = []
                    for size in SIZES:
                        t = np.arange(size) / size
                        nh = size // 4
                        cyc.append(sum(mg[k] * np.cos(2 * np.pi * (k + 1) * t + ph[k]) for k in range(nh)))
                    row.append(cyc)
                tabs[-1][-1].append(row)
    peak = 0.0                                  # (the 8-bit level as the measured vowels set it, all five: A and
    for sex in "mf":                            # O to the byte as before)
        for num in chosen[sex]:
            for z in (0, 1):
                s = sets[(sex, num, z)]
                for v in VOWELS:
                    mg = unit(s[v][0])
                    for size in SIZES:
                        t = np.arange(size) / size
                        peak = max(peak, np.abs(sum(mg[k] * np.cos(2 * np.pi * (k + 1) * t + s["a"][1][k])
                                                    for k in range(size // 4))).max())
    g = 127.0 / peak
    clip = sum(int((np.abs(c * g) > 127).sum()) for a in tabs for b in a for z in b for v in z for c in v)
    total = sum(SIZES)
    lines = ["/* GENERATED by tools/gen_choir.py: do not edit. GR!MM V2: VOICE's choir TYPEs' wavetables, from VocalSet",
             " * (Wilkins, Seetharaman, Wahl, Pardo, Northwestern University, 2018, https://doi.org/10.5281/zenodo.1442513,",
             " * CC BY 4.0: https://creativecommons.org/licenses/by/4.0/), changed as tools/gen_choir.py says: single cycles",
             f" * measured from the straight long tones of {', '.join('m' + str(n) for n in chosen['m'])} (men) and "
             f"{', '.join('f' + str(n) for n in chosen['f'])} (women),",
             " * men at C3 and C4, women at C4 and C5, on A E I O U; four sizes a cycle (256 128 64 32: 64 32 16 8",
             " * harmonics), 8 bits. CHOIR_T[voice 0 men, 1 women][singer][note 0 the lower, 1 the higher][vowel][the",
             " * four sizes, one after the other] */",
             f"#define CHOIR_NS {NSING}",
             f"#define CHOIR_LEN {total}",
             f"static const int8_t CHOIR_T[2][{NSING}][2][5][{total}] = {{"]
    for a in tabs:
        lines.append("    {")
        for b in a:
            lines.append("        {")
            for z in b:
                lines.append("            {")
                for v in z:
                    q = np.concatenate([np.clip(np.round(c * g), -127, 127) for c in v]).astype(int)
                    lines.append("                {" + ", ".join(str(int(k)) for k in q) + "},")
                lines.append("            },")
            lines.append("        },")
        lines.append("    },")
    lines.append("};")
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    print(f"{OUT}: {2 * NSING * 2 * 5 * total} bytes of tables; samples clipped at 127: {clip}")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("fetch", "make"):
        sys.exit(__doc__)
    (fetch if sys.argv[1] == "fetch" else make)(Path(sys.argv[2]))
