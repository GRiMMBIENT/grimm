#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
# GR!MM in the browser: firmware/src/grimm.c, unchanged, on the fake FM-1 web/fm1_web.c, compiled to WebAssembly
# with Emscripten, then Asyncify (Binaryen's wasm-opt) so the app's main loop can pause between audio blocks.
# After Felucca's web/emu/build.sh (hugelton/Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
# Instruments) and X0X's (charlesvestal/fm1-x0x), both GPL-3.0.
#   web/build_web.sh   ->  web/dist/{index.html, panel.js, worklet.js, grimm.wasm, Terminus-LICENSE.txt; the editor:
#                          editor.html, editor.js, editor_core.js, PressStart2P-Regular.ttf, OFL-PressStart2P.txt}
#   node web/test_web.mjs   (after tests/run_host.sh: the browser build against host mode)
#   EMSDK    the Emscripten SDK (default ~/emsdk; not needed when emcc is on the PATH)
#   APP_VER  the version on the screen (default "1.0", as host mode and the release)
set -e
cd "$(dirname "$0")/.."
EMSDK="${EMSDK:-$HOME/emsdk}"
EMCC="$EMSDK/upstream/emscripten/emcc"       # (the SDK's own tools by path: emsdk_env.sh needs bash)
WASM_OPT="$EMSDK/upstream/bin/wasm-opt"
[ -x "$EMCC" ] || EMCC="$(command -v emcc)" || true
[ -x "$WASM_OPT" ] || WASM_OPT="$(command -v wasm-opt)" || true
[ -x "$EMCC" ] || { echo "build_web: no Emscripten (emcc) in $EMSDK or on the PATH"; exit 1; }
[ -x "$WASM_OPT" ] || { echo "build_web: no wasm-opt (Binaryen) in $EMSDK or on the PATH"; exit 1; }
mkdir -p build/web web/dist
"$EMCC" -O2 -Wall -Wno-unused-function -DGRIMM_WEB -DAPP_VER="\"${APP_VER:-1.0}\"" -Ifirmware/src -Ifirmware/gen \
    --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=262144 -sINITIAL_MEMORY=4194304 \
    -o build/web/grimm.wasm firmware/src/grimm.c
"$WASM_OPT" build/web/grimm.wasm --enable-bulk-memory --enable-nontrapping-float-to-int --enable-sign-ext \
    --enable-mutable-globals --asyncify --pass-arg=asyncify-imports@env.web_yield -O2 -o web/dist/grimm.wasm
cp web/index.html web/panel.js web/worklet.js assets/fonts/Terminus-LICENSE.txt web/dist/   # (the screen font: SIL OFL 1.1)
cp web/editor.html web/editor.js web/editor_core.js assets/fonts/PressStart2P-Regular.ttf web/dist/   # (the editor page)
mkdir -p web/dist/LICENSES                       # (the pages ship with their licences: linked from their footers)
cp LICENSE LICENSING.md web/dist/ && cp LICENSES/*.txt web/dist/LICENSES/ && touch web/dist/.nojekyll
echo "web: web/dist ($(wc -c < web/dist/grimm.wasm | tr -d ' ') B wasm)"
