#!/bin/sh
# The same catalog requests execute through LLVM's Wasm backend without a VM.
set -eu
driver=${1:?usage: wasm.sh NATIVE_DRIVER LLVM_BIN EMCC NODE}
llvm=${2:?usage: wasm.sh NATIVE_DRIVER LLVM_BIN EMCC NODE}
emcc=${3:?usage: wasm.sh NATIVE_DRIVER LLVM_BIN EMCC NODE}
node=${4:?usage: wasm.sh NATIVE_DRIVER LLVM_BIN EMCC NODE}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-asset-wasm.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'package main\nMain :: () -> int { return missing; }\n' > "$work/rejected.chibi"
printf 'unchanged' > "$work/rejected.ll"
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target unknown-target \
    --emit-llvm --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 1 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'unsupported native target' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target wasm32-unknown-emscripten \
    --llvm "$llvm" --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 2 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'WebAssembly requires --emit-llvm' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
printf 'package main\nMain :: () -> int { yield; return 0; }\n' > "$work/rejected.chibi"
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target wasm32-unknown-emscripten \
    --emit-llvm --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 1 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'native WebAssembly tasks have no runtime yet' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
