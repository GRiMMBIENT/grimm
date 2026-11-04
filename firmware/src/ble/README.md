# BLE-MIDI: Optimist's stack

The `ble_*` files, `rf_share.h` and `../../hal/fm1_ble.h`, `fm1_ble_rf.h`, `ble_rf_tables.h` (fm1-dock's
`ble_rf_tables_v15.h`) are [Optimist](https://github.com/w0ts/optimist)'s (w0ts, GPL-3.0-only), as
[fm1-dock](https://github.com/joakimeriksson/fm1-dock) carries them (commit `e4c99bf`, 2026-10-27). Their design, the
hardware facts and the provenance are Optimist's `docs/BLE-STACK.md` and `docs/BLE-HW-FACTS.md`.

`hx.c` / `hx.h` are fm1-dock's glue; GR!MM's changes: the name, and it is linked into the image (`ble.o`, its `.bss`
in RAMTEXT: `app.ld`), not at a place of its own; `hx_dump`, the BLE report over USB (`tools/fm1_ble_diag.py`).
`platform.c` (`GRIMM_BLE`) calls it as fm1-dock's `dock.c` does.

## GR!MM's changes to the stack, each found on an FM-1

- `ble_hw_wl82.c` `rx_valid`: the supervision timer (and a link's establishment) only on a packet with a good CRC.
  On EVTCOUNT, as before, a link whose central had gone stayed "up" for good.
- `ble_hw_wl82.c` `hw_instants`: a connection update's WinOffset as a long receive window from the old schedule's
  anchor, not column 4. With column 4 the link was lost at every update with a WinOffset (a Mac's, interval kept).
  Large WinOffsets close to the interval still lose it.
- `ble_hw_wl82.c` window autozoom (`HW_AUTOZOOM`, `hw_widen_drift`) and widening at +500 ppm for our own clock.
- `ble_ll.c`: a channel map that comes after its instant is taken at the next event instead of closing with 0x28.
- `../../hal/fm1_ble_rf.h` `fm1_ble_pll_cal` and `fm1_lut_word` (`FM1_BLE_PLL_CAL=0`: Optimist's as captured): the
  PLL's band edges scanned on this chip where stock's start-up scans them (§16.1 group 7: JieLi's `wfa_pll_bank_scan`),
  and each FEQ entry of the RF-die LUT (0..127, command 13) given the band for its own frequency in bits [15:9], as
  JieLi's `wl_rfd_actl_ram_init` does. The captured start-up has the bands of stock's run in the emulator there (60
  down to 38); every other bit of the 128 FEQ entries equals the SDK's `wl_rfd_ram_lut` (`apps/common/net/wifi_conf.c`).
  The BLE report shows the scan's result.

The last three follow the behaviour of JieLi's own code in the JieLi AC79 SDK (Apache-2.0), read from its
`cpu/wl82/liba/btctrler.a` and `wl_rf_common.a` (LLVM bitcode with debug info: `opt -mtriple=x86_64-unknown-linux
-disable-verify -S` gives readable IR; `RF_ble.c` is its BLE baseband driver): `ble_window_autozoom` /
`__hw_set_widen_drift`, `ll_slave_cal_widening`, the instant cleared with a `#`, `wfa_pll_bank_scan`. Written anew in the driver's terms, no SDK code copied
(LICENSING.md). Unlike the rest of Optimist's stack, these are not clean-room: Optimist may not want them upstream.

## The deaf channels (solved)

Before the band calibration a connected FM-1 heard only ~20-35 % of a central's connection events, with a Mac and a
CME WIDI alike, at 2 cm, on every channel map and interval: counting good packets by channel (LASTCHMAP, in the BLE
report) showed 13 of the 37 data channels heard, the same set in every link, a comb every ~6-8 MHz in the upper band.
The cause: the VCO band each frequency's RF-die LUT entry carries, written as Optimist captured it from stock in an
emulator (`tools/ble_rf_lut_check.py`: every other bit equals the SDK's table). With `fm1_ble_pll_cal` /
`fm1_lut_word` (above) the same FM-1 hears 99-100 % of the events with the WIDI and with a Mac (whose quick channel
re-maps had lost links on maps landing on the deaf channels), autozoom idle.

## Still open

A connection update whose WinOffset is close to the interval loses the link (above).

Built only with `tools/build.py --ble`; off at run time until `ble_set(1)`.
