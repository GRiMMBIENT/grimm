# Building GR!MM

GR!MM is one C program, `firmware/src/grimm.c` (it includes the other `.c` files), on the FM-1 platform of
[Felucca](https://github.com/hugelton/Felucca) as [SLOOP](https://github.com/isod89/sloop-fm1) and
[Dinghy](https://github.com/zvenson/dinghy) carry it. The same file builds three ways: for the FM-1, for your
computer (host mode: the screen as images, the sound as a WAV) and for the browser (the demo and the editor).

## Set up once

Linux on x86-64 (or Windows with WSL, or macOS with Docker), Python 3 with Pillow, the JieLi toolchain and three
files of the JieLi AC79 SDK:

```sh
pip3 install Pillow mido python-rtmidi
tools/get_toolchain.sh                      # JieLi's clang for the chip, to ~/.jieli/toolchain
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
    https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
```

On macOS the build runs the toolchain in a `linux/amd64` Docker container by itself (have Docker running).
`JIELI_TOOLCHAIN` and `AC79_SDK` point elsewhere if your copies live somewhere else.

For the browser build: the [Emscripten SDK](https://emscripten.org) in `~/emsdk`, and Node.js for its tests.

## The firmware

```sh
./build.sh                                  # -> build/grimm.fwsc (identity FM-1_966, version 1.0)
./build.sh --release 1.1                    # -> build/grimm-1.1.fwsc, the version "1.1" on the screen
py tools/fm1_install.py build/grimm.fwsc    # install it over USB (asks first); --info: what the FM-1 runs
```

The build fails on what would brick or break an FM-1 firmware: a 64-bit division, a call into the chip ROM, too
much RAM, a register touched outside `firmware/hal/`. [RULES.md](RULES.md) says why each one matters.

## Host mode and the tests

```sh
tests/run_host.sh                           # -> build/host/grimm.wav and screen PNGs
tests/stress.sh 1                           # the stress test (seeds 1, 2, 3), with address and undefined-behaviour checks
```

## The browser: demo and editor

```sh
web/build_web.sh                            # -> web/dist/ (index.html: the demo, editor.html: the editor, grimm.wasm)
node web/test_web.mjs                       # after tests/run_host.sh: the browser build against host mode, the editor
cd web/dist && python3 -m http.server 8000  # then open http://localhost:8000/ (or /editor.html)
```

`APP_VER=1.0 web/build_web.sh` puts the version on the browser build's screen.

## The factory samples

`assets/samples/*.wav` become `firmware/gen/grimm_samples.h` with `py tools/gen_samples.py`, encoded exactly as
`tools/fm1_sample_upload.py` uploads a WAV.

## More

- [API.md](API.md): everything `platform.c` gives the app: the panel's ids, the screen calls, the audio contract,
  MIDI, settings, the flash map, timing.
- [RULES.md](RULES.md): the chip's rules (no FPU, no libgcc, the audio interrupt's budget).
- [EDITOR_PROTOCOL.md](EDITOR_PROTOCOL.md): how the editor talks to the FM-1 over USB SysEx.
