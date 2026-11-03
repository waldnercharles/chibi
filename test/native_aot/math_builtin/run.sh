#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native built-in math: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-builtin-math.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cp -R "$root/test/native_aot/math_builtin/failures" "$work/failures"
for level in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/math_builtin/Deterministic.chibi" --entry Main \
        --output "$work/trig-$level" --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    "$work/trig-$level"
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/math" "$root/test/compiler/samples/math"
    status=0
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        "$work/failures" > "$work/failures-$level.log" 2>&1 || status=$?
    [ "$status" -eq 1 ] || { cat "$work/failures-$level.log"; exit 1; }
    awk '/^(PASS|FAIL) facts[.]/ { print }' "$work/failures-$level.log" | LC_ALL=C sort > "$work/failures.results"
    diff -u "$root/test/native_aot/math_builtin/failures/expected.txt" "$work/failures.results"
    grep -F '16 tests, 15 failed' "$work/failures-$level.log" > /dev/null
    grep -F 'Referenced array element is unavailable' "$work/failures-$level.log" > /dev/null
    [ "$(grep -c 'but it failed with another failure' "$work/failures-$level.log")" -eq 15 ]
    printf 'PASS deterministic trigonometry and original math sample inventory O%s\n' "$level"
done
if [ -n "$reference" ]; then
    status=0
    "$reference" --project-root "$work/failures" --test > "$work/reference.log" 2>&1 || status=$?
    [ "$status" -eq 1 ] || { cat "$work/reference.log"; exit 1; }
    awk '/^(PASS|FAIL) facts[.]/ { print }' "$work/reference.log" | LC_ALL=C sort > "$work/reference.results"
    diff -u "$work/reference.results" "$work/failures.results"
fi
