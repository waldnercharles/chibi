#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native clock projection requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-clock-projection.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
{
    cat "$root/test/native_aot/host_clock/Projection.chibi"
    for source in Catalog Compile Assertions; do
        sed '/^package /d; /^import /d' "$root/tools/compiler/native_driver/$source.chibi"
    done
} > "$work/Projection.chibi"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$work/Projection.chibi" \
    --entry Main --output "$work/projection" --llvm "$llvm" --target "$target" --level 2
"$work/projection"
for level in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/host_clock/Default.chibi" --collection "host=$root/packages/host" \
        --entry Main --output "$work/default" --llvm "$llvm" --target "$target" --level "$level"
    "$work/default"
done
printf 'PASS private native clock projection, catalog and owner boundaries\n'
