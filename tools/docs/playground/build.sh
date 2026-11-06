#!/bin/sh
# Build the Chibi-authored formatter and its static playground. No tools install.
set -eu
if [ "$#" -ne 5 ]; then
    echo 'usage: build.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN OUTPUT_DIRECTORY' >&2
    exit 2
fi
driver=$1
llvm=$2
emcc=$3
wasm_llvm=$4
output=$5
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
node "$root/tools/docs/playground/generate-options.mjs" --check
if [ -e "$output" ] || [ -L "$output" ]; then
    echo 'formatter: output must be a new directory' >&2
    exit 2
fi
case "$output" in
    /*) ;;
    *) output=$PWD/$output ;;
esac
case "$(uname -s)" in
    Darwin) target=aarch64-apple-darwin ;;
    Linux) target=x86_64-unknown-linux-gnu ;;
    *) echo 'formatter: build host must be Linux or macOS' >&2; exit 2 ;;
esac
mkdir -p "$root/.chibi"
work=$(mktemp -d "$root/.chibi/formatter-builder.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/graph/formatter_wasm/Build.chibi" --collection "graph=$root/tools/graph" \
    --entry Main --target "$target" --llvm "$llvm" --level 2 --output "$work/build"
"$work/build" "$root" "$driver" "$emcc" "$wasm_llvm" "$output"
