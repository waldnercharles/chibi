#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native value equality requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-value-equality.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/remove_item/Prepare.chibi" --entry Main \
    --output "$work/prepare" --llvm "$llvm" --target "$target" --level 2 --jobs 2
"$work/prepare" "$work/values.ll" "$root/test/native_aot/remove_item/Probe.ll"
for level in 0 2; do
    "$llvm/opt" -S -passes="default<O$level>" "$work/values.ll" -o "$work/optimized.ll"
    "$llvm/llc" -O="$level" -mtriple="$target" -relocation-model=pic -filetype=obj \
        "$work/optimized.ll" -o "$work/values.o"
    "$llvm/llvm-ar" rcs "$work/values.a" "$work/values.o"
    archive=$(printf '%s' "$work/values.a" | sed 's/[&|\\]/\\&/g')
    sed "s|@ARCHIVE@|$archive|g" "$root/test/native_aot/remove_item/Runtime.chibi" > "$work/Runtime.chibi"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Runtime.chibi" --entry Main --output "$work/probe" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    "$work/probe"
    printf 'PASS borrowed value equality, IEEE/union rules, scratch reuse and depth 10000 O%s\n' "$level"
done
