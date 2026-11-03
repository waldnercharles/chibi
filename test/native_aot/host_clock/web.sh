#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the native LLVM tools directory}
emcc=${3:?pass emcc}
wasm_llvm=${4:?pass the Wasm LLVM tools directory}
node=${5:?pass Node}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'clock Wasm preparation requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-clock-web.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile only the pure module boundary, as in projection.sh. Importing the
# full driver would also check the public CLI and unrelated build commands.
{
    cat "$root/test/native_aot/host_clock/WebPrepare.chibi"
    for source in Catalog Compile Assertions; do
        sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/$source.chibi"
    done
} > "$work/Prepare.chibi"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/Prepare.chibi" --entry Main --output "$work/prepare" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 2
"$work/prepare" "$root" "$work"
for level in 0 2; do
    mkdir "$work/O$level"
    for source in "$work"/program-*.ll "$work/adapter.ll" "$work/factory.ll" "$work/format.ll" \
                  "$root/test/native_aot/host_clock/WebRuntime.ll"; do
        name=$(basename "$source" .ll)
        "$wasm_llvm/opt" -S -passes="default<O$level>,verify" "$source" -o "$work/O$level/$name.ll"
        "$wasm_llvm/llc" -O"$level" -filetype=obj "$work/O$level/$name.ll" -o "$work/O$level/$name.o"
    done
    "$emcc" -O"$level" "$work/O$level/"*.o -sENVIRONMENT=node -sMODULARIZE=1 \
        -sEXPORTED_FUNCTIONS=_clock_test -o "$work/O$level/clock.js"
    "$node" "$root/test/native_aot/host_clock/web.js" "$work/O$level/clock.js"
    printf 'PASS explicit Wasm clock ownership O%s\n' "$level"
done
