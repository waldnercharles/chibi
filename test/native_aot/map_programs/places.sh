#!/bin/sh
# Logical place failures are unclassified, even though expression reads use bounds1.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
work=${3:?pass the scratch folder}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native map places: unsupported test host' >&2; exit 2 ;;
esac
for level in 0 2; do
    for name in Initial Late Deleted Negative Method; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/map_programs/array_places/$name.chibi" --entry Main \
            --output "$work/$name-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 1
        failed=0
        "$work/$name-O$level" > "$work/$name-O$level.log" 2>&1 || failed=$?
        test "$failed" -eq 134
        expected='native failure: another failure'
        if [ "$name" = Method ]; then expected=AssertionFailed; fi
        grep -F "$expected" "$work/$name-O$level.log" >/dev/null
        if grep -E 'RHS must not run|IndexOutOfRange' "$work/$name-O$level.log" >/dev/null; then
            cat "$work/$name-O$level.log" >&2
            exit 1
        fi
    done
    failed=0
    CHIBI_CORE="$root/packages/core" "$compiler" test \
        --root "$root/test/native_aot/map_programs/array_places/wrong_category" \
        "$root/test/native_aot/map_programs/array_places/wrong_category" \
        --llvm "$llvm" "-O$level" > "$work/wrong-category-O$level.log" 2>&1 || failed=$?
    test "$failed" -eq 1
    grep -F 'expected IndexOutOfRange, but it failed with another failure' "$work/wrong-category-O$level.log" >/dev/null
done
echo 'native mapped bracket failure order and categories passed'
