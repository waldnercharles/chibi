#!/bin/sh
# Build a C consumer with no compiler-host/VM objects, then execute it and the
# relocated native compiler with an empty PATH and disabled C compilers.
# Usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER
set -eu
asset=${1:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
driver=${2:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
llvm=${3:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
cc=${4:?usage: bridge.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN C_COMPILER}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-asset-bridge.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/SDK/bin" "$work/SDK/libexec/chibi"
cp "$asset" "$work/SDK/libexec/chibi/chibi-asset-compiler"
"$cc" -std=c11 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE -Wall -Wextra -Werror \
    -I"$root/src" -o "$work/SDK/bin/asset-consumer" \
    "$root/test/native_aot/asset_compiler/Bridge.c" \
    "$root/src/authoring/asset_compiler_host.c" "$root/src/authoring/asset_compiler_native.c" \
    "$root/src/host/process.c" "$root/src/support/binary_cache.c" \
    "$root/src/support/file_io.c" "$root/src/support/sha256.c" "$root/src/support/xalloc.c"
mv "$work/SDK" "$work/relocated SDK"
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Verify.chibi" --entry Main \
    --output "$work/verify" --llvm "$llvm" --target "$target" --level 0 --jobs 2
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Crash.chibi" --entry Main \
    --output "$work/crash" --llvm "$llvm" --target "$target" --level 0 --jobs 2
program=$work/relocated\ SDK/libexec/chibi/chibi-asset-compiler
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$work/verify" \
    "$program" "$root/test/native_aot/asset_compiler/catalog.json" "$work" "" ""
mkdir "$work/cache" "$work/relative-temp"
(
    cd "$work"
    env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false TMPDIR=relative-temp \
        "$work/relocated SDK/bin/asset-consumer" \
        "$program" "$work/simple.json" "$work/source-error.json" "$work/cache" "$work/crash"
)
[ -z "$(find "$work/relative-temp" -mindepth 1 -print -quit)" ]
