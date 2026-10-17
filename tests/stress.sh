#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
# The stress test: firmware/src/grimm.c in host mode (tests/host.c) with address and undefined-behaviour checking and
# the checks of tests/stress_check.c, played by tests/stress_gen.py's script (every source on every track, chords,
# voicings, SCALE, LATCH, the octaves, fast keys, MIDI in, every page and hold combo while notes sound and the sequencer
# plays and records, slots, undo / redo, PRESETS; first a project come back from as at power-on), after
# tests/device_names.py (no two variables made one in the device build). Prints "stress test: passed", or what failed (exit 1).
#   tests/stress.sh [SEED]
set -e
cd "$(dirname "$0")/.."
mkdir -p build/stress
python3 tests/device_names.py || { echo "stress test: FAILED (device names)"; exit 1; }
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
   -Wall -Wno-unused-function -DDINGHY_HOST -DGRIMM_STRESS -DHOST_MATRIX -DAPP_VER="\"stress\"" -Ifirmware/src -Ifirmware/gen \
   -o build/stress/grimm firmware/src/grimm.c
python3 tests/stress_gen.py "${1:-1}" build/stress/script.txt build/stress/checks.txt
set +e
UBSAN_OPTIONS=print_stacktrace=1 STRESS_CHECKS=build/stress/checks.txt build/stress/grimm build/stress/out build/stress/script.txt \
    > build/stress/log.txt 2>&1
rc=$?
set -e
if [ $rc -eq 0 ] && grep -q "^STRESS OK" build/stress/log.txt; then
    echo "stress test: passed"
else
    echo "stress test: FAILED (exit $rc; build/stress/log.txt)"
    grep -m 5 -E "STRESS FAIL|ERROR|runtime error|SUMMARY" build/stress/log.txt || tail -5 build/stress/log.txt
    exit 1
fi
