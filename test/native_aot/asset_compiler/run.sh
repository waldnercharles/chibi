#!/bin/sh
# Usage: run.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN [LEGACY_HOST LEGACY_ASSET_ARTIFACT]
set -eu
compiler=${1:?usage: run.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN}
driver=${2:?usage: run.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN}
llvm=${3:?usage: run.sh NATIVE_ASSET_COMPILER NATIVE_DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-asset.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir -p "$work/SDK/libexec/chibi"
cp "$compiler" "$work/SDK/libexec/chibi/chibi-asset-compiler"
mv "$work/SDK" "$work/relocated SDK"
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Verify.chibi" --entry Main \
    --output "$work/verify" --llvm "$llvm" --target "$target" --level 0 --jobs 2
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$work/verify" \
    "$work/relocated SDK/libexec/chibi/chibi-asset-compiler" \
    "$root/test/native_aot/asset_compiler/catalog.json" "$work" "${4:-}" "${5:-}"
