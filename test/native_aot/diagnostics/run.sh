#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: run.sh NATIVE_DRIVER LLVM_BIN' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
driver=$1
llvm=$2
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-diagnostics.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
{
    cat "$root/test/native_aot/diagnostics/Probe.chibi"
    sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/Catalog.chibi"
    sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/Compile.chibi"
} > "$scratch/Main.chibi"
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$scratch/Main.chibi" --output "$scratch/probe" --llvm "$llvm" --jobs 2
"$scratch/probe"
echo 'native diagnostics: passed'
