#!/bin/sh
# Link a C consumer with the authored facade archive, then execute it and the
# relocated native compiler with an empty PATH and disabled C compilers.
# Usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER FACADE_ARCHIVE
set -eu
asset=${1:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
driver=${2:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
llvm=${3:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
cc=${4:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
archive=${5:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER FACADE_ARCHIVE}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-asset-bridge.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/SDK/bin" "$work/SDK/libexec/chibi"
cp "$asset" "$work/SDK/libexec/chibi/chibi-asset-compiler"
"$cc" -std=c11 -pthread -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
    -I"$root/src" -o "$work/SDK/bin/asset-consumer" \
    "$root/test/native_aot/asset_compiler/Bridge.c" \
    "$archive"
mv "$work/SDK" "$work/relocated SDK"
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Verify.chibi" --entry Main \
    --output "$work/verify" --llvm "$llvm" --target "$target" --level 0 --jobs 2
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Crash.chibi" --entry Main \
    --output "$work/crash" --llvm "$llvm" --target "$target" --level 0 --jobs 2
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Processes.chibi" --entry Main \
    --output "$work/processes" --llvm "$llvm" --target "$target" --level 0 --jobs 2
mkdir "$work/process-work"
"$work/processes" "$work/process-work"
program=$work/relocated\ SDK/libexec/chibi/chibi-asset-compiler
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$work/verify" \
    "$program" "$root/test/native_aot/asset_compiler/catalog.json" "$work" "" ""
mkdir "$work/cache" "$work/relative-temp"
(
    cd "$work"
    env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false TMPDIR=relative-temp \
        "$work/relocated SDK/bin/asset-consumer" \
        "$program" "$work/simple.json" "$work/source-error.json" "$work/cache" "$work/crash" "$work/simple.chibip"
)
[ -z "$(find "$work/relative-temp" -mindepth 1 -print -quit)" ]
