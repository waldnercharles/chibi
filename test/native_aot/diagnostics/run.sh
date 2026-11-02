#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
    echo 'usage: run.sh NATIVE_DRIVER LLVM_BIN' >&2
    exit 2
fi
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
driver=$1
llvm=$2
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native diagnostics: unsupported test host' >&2; exit 2 ;;
esac
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-diagnostics.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
{
    cat "$root/test/native_aot/diagnostics/Probe.chibi"
    sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/Catalog.chibi"
    sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/Compile.chibi"
} > "$scratch/Main.chibi"
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$scratch/Main.chibi" --output "$scratch/probe" --llvm "$llvm" --target "$target" --jobs 2
"$scratch/probe"
echo 'native diagnostics: passed'
