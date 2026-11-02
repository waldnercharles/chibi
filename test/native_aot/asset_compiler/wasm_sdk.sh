#!/bin/sh
# Consume the public browser component after the real SDK prefix is relocated.
set -eu
prefix=${1:?usage: wasm_sdk.sh SDK_PREFIX EMCC NODE REQUEST_JSON}
emcc=${2:?usage: wasm_sdk.sh SDK_PREFIX EMCC NODE REQUEST_JSON}
node=${3:?usage: wasm_sdk.sh SDK_PREFIX EMCC NODE REQUEST_JSON}
request=${4:?usage: wasm_sdk.sh SDK_PREFIX EMCC NODE REQUEST_JSON}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-wasm-sdk.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/relocated SDK/include/chibi/authoring" "$work/relocated SDK/lib/chibi-wasm32" "$work/relocated SDK/lib/cmake"
# The supplied prefix was installed by SDK.Install or SDK.CompilerTools. Copy
# just its portable component so no desktop executable or source checkout can
# satisfy a missing header, library or imported-target setting.
cp "$prefix/include/chibi/authoring/asset_compiler_host.h" "$work/relocated SDK/include/chibi/authoring/"
cp "$prefix/lib/chibi-wasm32/"* "$work/relocated SDK/lib/chibi-wasm32/"
cp -R "$prefix/lib/cmake/ChibiAssetCompilerWasm" "$work/relocated SDK/lib/cmake/"
em_config=$(dirname "$emcc")/em-config
emscripten=$("$em_config" EMSCRIPTEN_ROOT)
cmake -S "$root/test/native_aot/asset_compiler/wasm_sdk" -B "$work/build" \
    "-DCMAKE_TOOLCHAIN_FILE=$emscripten/cmake/Modules/Platform/Emscripten.cmake" \
    "-DCMAKE_C_COMPILER=$emcc" "-DCMAKE_FIND_ROOT_PATH=$work/relocated SDK" -DCMAKE_BUILD_TYPE=Release
cmake --build "$work/build" --parallel 2
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$node" "$work/build/compiler.js" --reuse "$request" "$work/cache"
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$node" "$work/build/compiler_threads.js" "$request"
# A changed public header must fail during package discovery, before a consumer
# compile or link can accidentally use an ABI from another SDK revision.
printf '\n/* stale */\n' >> "$work/relocated SDK/include/chibi/authoring/asset_compiler_host.h"
if cmake -S "$root/test/native_aot/asset_compiler/wasm_sdk" -B "$work/stale" \
    "-DCMAKE_TOOLCHAIN_FILE=$emscripten/cmake/Modules/Platform/Emscripten.cmake" \
    "-DCMAKE_C_COMPILER=$emcc" "-DCMAKE_FIND_ROOT_PATH=$work/relocated SDK" > "$work/stale.log" 2>&1; then
    cat "$work/stale.log"; exit 1
fi
grep -F 'optional Chibi Wasm compiler or header is stale' "$work/stale.log"
printf '%s\n' 'relocated SDK browser compiler plain/threaded public APIs passed'
