#!/bin/sh
set -eu
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN [REFERENCE_COMPILER]}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN [REFERENCE_COMPILER]}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native Windows decimal: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-windows-decimal.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/source"
# Exercise the exact private implementation without requiring Win32 on this host.
cat "$root/packages/core/sys/windows/Decimal.chibi" > "$work/source/Values.chibi"
sed '1d' "$root/test/native_aot/windows_decimal/Values.chibi" >> "$work/source/Values.chibi"
for level in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/source/Values.chibi" --entry Main --output "$work/values-$level" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    "$work/values-$level"
    printf 'PASS full-width Win32 decimal words O%s\n' "$level"
done
if [ -n "$reference" ]; then
    printf '%s\n' 'package windows' '@test' 'Values :: () { result := Main(); assert(result == 0); }' > "$work/source/Tests.chibi"
    "$reference" --project-root "$work/source" --test
fi
