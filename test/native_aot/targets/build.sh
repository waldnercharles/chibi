#!/bin/sh
# Cross-build the consumers once with the Linux job's current native compiler.
set -eu
compiler=$1
llvm=$2
bundle=$3
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$bundle"
for platform in macos-arm64 windows-x64; do
    case "$platform" in
        macos-arm64) triple=aarch64-apple-darwin; output=$bundle/runner-macos-arm64 ;;
        windows-x64) triple=x86_64-pc-windows-msvc; output=$bundle/runner-windows-x64.exe ;;
    esac
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/targets/Runner.chibi" \
        --collection "bundle=$root/test/native_aot/targets" \
        --entry Main --target "$triple" --llvm "$llvm" --level 2 --jobs 2 --output "$output"
done
