# GR!MM

**Dark ambient, dungeon synth and deathdream firmware for the M-VAVE FM-1.**

**[Try it in your browser](https://grimmbient.github.io/grimm/)** (the real firmware, no FM-1 needed) ·
**[Open the editor](https://grimmbient.github.io/grimm/editor.html)** (edit sounds live, load samples, back up your FM-1)

![GR!MM on the FM-1: the HOME screen](docs/screens/home.png)

GR!MM turns the FM-1 into a four-track drone and atmosphere machine: seven sound sources, long dark effects, a 64-step sequencer per track, one-key chords, an arpeggiator, song mode and live performance tricks. Everything is drawn in a pixel-brutalist style, with an eye that watches the music and candles that burn with each track.

Free and open source (GPL-3.0). Unofficial firmware, not made or endorsed by M-VAVE. **Install at your own risk.**

---

## What's inside

- **4 tracks**, 6 voices shared between them
- **7 sources:** VA (analog-style, with supersaw), FM (6-operator, fully editable), SAMPLE (a sample played like a tape loop), VOICE (formant choir), WHEEL (tonewheel organ), PHYS (bowed and struck physical models), PHASE (CZ-style phase distortion)
- **40 factory sounds** in 8 categories (PAD, DRONE, BELL, VOICE, ORGAN, KEYS, OTHER, TEXTURE), plus 64 slots for your own, with names you give them in the editor
- **4 factory samples** (URCHIN, LEAVE, NIKOLAI, M-SOFT; CC0) in the four sample slots, which you can replace with your own (6.6 s each)
- **Effects:** TAPE (wow, saturation, hiss over the whole mix), GRAIN (a granular cloud with FREEZE), DELAY (tape echo) and SPACE (reverb up to ~32 s, and infinite)
- **Per track:** envelope, LFO, filter, scale snapping, one-key chords, arpeggiator, latch for drones
- **Sequencer:** 64 steps per track, chance, ratchets, note lengths, step locks, chord steps, tools (random, mutate, shift, double, copy/paste)
- **Song mode:** four slots A–D, chained by hand or recorded live
- **Performance layer:** freeze, reverb and echo throws, tape stop, reverse, filter sweeps, stutter
- **Autosave:** the FM-1 comes back exactly as you left it
- **MIDI:** in and out over USB, MIDI learn, sequencer and arp out; the FM-1 is also a USB audio input for your computer
- **Battery level** in the HOME header

## Install

1. Open the **[FM-1 Firmware Hub](https://thefirmwarehub.com/)** in Chrome or Edge.
2. Connect the FM-1 over USB.
3. Pick **GR!MM** and press install.

Updates appear on the hub automatically when a new release is published here.

If GR!MM isn't on the hub yet, or you want to install a release file by hand: download `grimm.fwsc` from
[Releases](../../releases), then, with Python 3 and `py -m pip install mido python-rtmidi`:

```
py tools\fm1_install.py grimm.fwsc
```

Your sounds, slots A–D, settings and samples stay on the FM-1 when you update. If anything goes wrong, the hub's
**Rescue** page brings a stuck FM-1 back.

## Learn it

- **[The manual](MANUAL.md)**: every button, page, knob and combo, in plain English.
- **Quick start:** switch on, wait for the eye to open. Turn **ALGORITHM** to pick a track, turn **PRESETS** to pick a sound, play the keys. Hold **SEL** for one-key chords. Press **FX** for the effects.

## The editor and the demo

Both run in Chrome or Edge on a computer, nothing to install.

- **[The editor](https://grimmbient.github.io/grimm/editor.html):** connect the FM-1 over USB and edit the four
  tracks' sounds with big sliders: every move plays on the FM-1 at once, like turning its knobs. Store sounds into
  your 64 slots with a name, browse the factory sounds, load WAV files into the sample slots, and back up or restore
  everything. Without an FM-1 it runs GR!MM's own code in the page (TRY WITHOUT AN FM-1).
- **[The demo](https://grimmbient.github.io/grimm/):** the FM-1's panel on screen, running the same firmware. Play it
  with the mouse or the computer keyboard (the keys are listed under the panel and in the [manual](MANUAL.md#16-the-demo)).

## Your own samples

GR!MM has four sample slots that survive firmware updates. They come with the four factory samples; replace any of
them with your own:

1. Open the **[editor](https://grimmbient.github.io/grimm/editor.html)**, connect the FM-1, and go to **SAMPLES**.
2. Drop a WAV on a slot (any rate, mono or stereo; up to 6.6 s are kept), check its name and root note, press **UPLOAD**.

Then set a track's source to **SAMPLE** and pick the slot. **RESTORE FACTORY SAMPLES** (in the editor, or SYSTEM 2 on
the FM-1) brings the four factory samples back.

Optionally, from a command line with Python 3:

```
py tools\fm1_sample_upload.py 2 "your-sound.wav"
py tools\fm1_sample_upload.py --list
```

## Screens

| | | |
|---|---|---|
| ![Boot](docs/screens/boot.png) | ![Sequencer](docs/screens/seq.png) | ![FM algorithm](docs/screens/fm-algo.png) |
| ![Arpeggiator](docs/screens/arp.png) | ![Performance](docs/screens/perform.png) | ![Chords](docs/screens/chord.png) |

## Credits

GR!MM stands on the shoulders of other people's open work. Full details and licence texts are in [LICENSING.md](LICENSING.md) and [LICENSES/](LICENSES/).

- **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita / Hügelton Instruments (GPL-3.0): the platform, and the ANALOG, FM6, GRAIN, HALL, formant, tonewheel, physical-modelling and phase-distortion engines GR!MM builds on
- **[Dinghy](https://github.com/zvenson/dinghy)** by zvenson (GPL-3.0): the firmware template
- **[SLOOP](https://github.com/isod89/sloop-fm1)** by isod89 (GPL-3.0): the FM-1 platform work Felucca and Dinghy share; its editor's layout and live editing inspired GR!MM's editor
- **[X0X](https://github.com/charlesvestal/fm1-x0x)** by charlesvestal (GPL-3.0): the tape delay's design and the browser build's approach
- **msfa**, the FM core from Dexed (Apache-2.0)
- **DaisySP** and **Mutable Instruments Rings** (MIT), via Felucca's physical models
- **CrispyZebra** by Leo Kuroshita (GPL-3.0), via Felucca's phase distortion
- **Press Start 2P** font by CodeMan38 and **Terminus** font by Dimitar Zhekov (SIL Open Font License 1.1)

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md).

## Licence

GPL-3.0. See [LICENSE](LICENSE). The four factory samples are CC0 1.0: free to use for anything. Bugs and questions:
open an issue on this repository.
