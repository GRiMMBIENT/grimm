#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# GR!MM changes (file names): Copyright (C) 2026 GR!MM
# Run firmware/src/grimm.c on this computer (firmware/src/host.c): build/host/grimm.wav and the screens as PNG.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER="\"${APP_VER:-2.0}\"" -Ifirmware/src -Ifirmware/gen -o build/host/grimm firmware/src/grimm.c
GRIMM_MASTER=1022 GRIMM_RAW_OUT=build/host/grimm.raw build/host/grimm build/host/grimm   # (V2: MASTER as web/test_web.mjs turns it: 1022, Q12 4080)
python3 - <<'PY'
from pathlib import Path
from PIL import Image
for p in sorted(Path("build/host").glob("grimm-*.ppm")):
    Image.open(p).resize((480, 480), Image.NEAREST).save(p.with_suffix(".png"))
    p.unlink()
    print("screen ->", p.with_suffix(".png"))
PY
# saves across versions (firmware/src/compat.c): the project GR!MM 1.0 saved (tests/data/v1.0-saves.bin) loads as 1.0 loaded it
mkdir -p build/compat
GRIMM_SAVE_IN=tests/data/v1.0-saves.bin GRIMM_STATE_OUT=build/compat/v1.0-state.txt build/host/grimm build/compat/run \
    tests/data/compat.txt > /dev/null
if cmp -s tests/data/v1.0-state.txt build/compat/v1.0-state.txt; then
    echo "saves  GR!MM 1.0's project loads as in 1.0"
else
    echo "saves  FAILED: GR!MM 1.0's project loads otherwise (diff tests/data/v1.0-state.txt build/compat/v1.0-state.txt)"
    exit 1
fi
# GR!MM V2: MIDI. The TRS jack's parser (tests/midi_uart_test.c); the clock in and out, MIDI CHANNEL's CH n > SEL
# (tests/clock_test.py: host mode's "clock", "trs" and "probe")
cc -O2 -Wall -Wno-unused-function -Ifirmware/hal -o build/host/midi_uart_test tests/midi_uart_test.c
build/host/midi_uart_test
python3 tests/clock_test.py build/host/grimm
# GR!MM V2: the effects (tests/fx_test.py): the track filter's TYPE and POS, MASTER, DRIVE, their saves
python3 tests/fx_test.py build/host/grimm
# GR!MM V2: the quick wins (tests/quick_test.py)
python3 tests/quick_test.py build/host/grimm
# LATCH: every factory sound, SYSTEM 3 both ways, EDIT, SEL + OCT+, slots, the project, PHYS (tests/latch_test.py)
python3 tests/latch_test.py build/host/grimm
# GR!MM V2: GENERATE (FX held + the black keys): in the SCALE, the pattern unheard and unchanged (tests/gen_test.py)
python3 tests/gen_test.py build/host/grimm
# GR!MM V2: VELOCITY, PHASE's LINE 2 and envelopes, GLIDE (tests/play_test.py)
python3 tests/play_test.py build/host/grimm
# GR!MM V2: the gain staging: no clip at any MASTER, SPACE and DELAY under their knee (tests/level_test.py)
python3 tests/level_test.py build/host/grimm
# GR!MM V2: TAPE off is TAPE off: TAPE's knobs and every TAPE tile at 0, the output to the bit as the reference
# (tests/tapeoff_test.py, tests/data/tapeoff.sha256: a pad, a choir, a SAMPLE track, VOICE, GENERATE, the FX)
python3 tests/tapeoff_test.py build/host/grimm
# GR!MM V2 (rc9): PRESETS through every sound on T1 and T2 while it plays, FX off and on: every one sounds, no
# track's mix moved but by its knob, the sends still work (tests/presets_test.py; the rc8 no-sound report)
python3 tests/presets_test.py build/host/grimm
# GR!MM V2 rc9's fixes (build/bugs/AUDIT-rc8.md): NEW, HOME's CUT / DRY, the LFO on VOLUME, a sound past its
# ranges, the overload guard, the saves under a tail that never ends (tests/rc9_test.py)
python3 tests/rc9_test.py build/host/grimm
# GR!MM V2 rc9: the USB audio ring (firmware/src/uac_ring.h) in time: no dropout at any CPU load
cc -O2 -Wall -Ifirmware/src -o build/host/uac_ring_test tests/uac_ring_test.c
build/host/uac_ring_test
# GR!MM V2.0 rc11: chord mode is per track: a track's chord on / off, CHORD TYPE and VOICING apply to that track only,
# through every way a note plays (tests/chord_test.py)
python3 tests/chord_test.py build/host/grimm
# GR!MM V2.0 rc11: no flash step (autosave, the settings' save) while a computer reads the USB audio: it stopped the key scan,
# the LEDs and the stream ~50 ms (tests/rc11_test.py)
python3 tests/rc11_test.py build/host/grimm
# GR!MM V2.0.2: the sample slots after another firmware or a cut upload: any slot that does not play gets its factory
# sample once the boot checks are done, one that plays is never touched, the header says which (tests/samplefix_test.py)
python3 tests/samplefix_test.py build/host/grimm
