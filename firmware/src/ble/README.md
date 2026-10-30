# BLE-MIDI: Optimist's stack

The `ble_*` files, `rf_share.h` and `../../hal/fm1_ble.h`, `fm1_ble_rf.h`, `ble_rf_tables.h` (fm1-dock's
`ble_rf_tables_v15.h`) are [Optimist](https://github.com/w0ts/optimist)'s (w0ts, GPL-3.0-only), as
[fm1-dock](https://github.com/joakimeriksson/fm1-dock) carries them (commit `e4c99bf`, 2026-10-27), unchanged. Their
design, the hardware facts and the provenance are Optimist's `docs/BLE-STACK.md` and `docs/BLE-HW-FACTS.md`.

`hx.c` / `hx.h` are fm1-dock's glue; GR!MM's changes: the name, and it is linked into the image (`ble.o`, its `.bss`
in RAMTEXT: `app.ld`), not at a place of its own. `platform.c` (`GRIMM_BLE`) calls it as fm1-dock's `dock.c` does.

Built only with `tools/build.py --ble`; off at run time until `ble_set(1)`.
