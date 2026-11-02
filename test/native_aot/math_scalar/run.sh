#!/bin/sh
# Execute scalar built-ins at both optimization levels through the native driver.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native scalar math: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-math-scalar.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for level in 0 2; do
    for source in SquareRoot Shadow; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/math_scalar/$source.chibi" --entry Main \
            --output "$work/$source-$level" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
        "$work/$source-$level"
    done
    printf 'PASS native scalar square root and authored shadow O%s\n' "$level"
done
sh "$root/test/native_aot/math_scalar/remainder.sh" "$compiler" "$llvm" "${3:-}"
