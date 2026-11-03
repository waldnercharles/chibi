#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
work=${3:?pass the scratch folder}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native indexed foreign loans: unsupported test host' >&2; exit 2 ;;
esac
for level in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_values/foreign_elements/Bounds.chibi" --entry Main \
        --output "$work/bounds-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 1
    failed=0
    "$work/bounds-O$level" > "$work/bounds-O$level.log" 2>&1 || failed=$?
    test "$failed" -eq 134
    grep -F 'native array index 3 is out of range for length 1' "$work/bounds-O$level.log" >/dev/null
    if grep -F 'RHS evaluated' "$work/bounds-O$level.log" >/dev/null; then cat "$work/bounds-O$level.log"; exit 1; fi
    CHIBI_CORE="$root/packages/core" "$compiler" test \
        --root "$root/test/native_aot/managed_values/foreign_elements/bounds" \
        "$root/test/native_aot/managed_values/foreign_elements/bounds" \
        --llvm "$llvm" "-O$level" > "$work/category-O$level.log" 2>&1
    grep -F '2 tests, 0 failed' "$work/category-O$level.log" >/dev/null
    failed=0
    CHIBI_CORE="$root/packages/core" "$compiler" test \
        --root "$root/test/native_aot/managed_values/foreign_elements/wrong_category" \
        "$root/test/native_aot/managed_values/foreign_elements/wrong_category" \
        --llvm "$llvm" "-O$level" > "$work/wrong-category-O$level.log" 2>&1 || failed=$?
    test "$failed" -eq 1
    grep -F 'expected UnwrappedNil, but it failed with IndexOutOfRange' "$work/wrong-category-O$level.log" >/dev/null
done
for source in RejectedRetained RejectedPointer; do
    printf 'preserved\n' > "$work/$source"
    failed=0
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_values/foreign_elements/$source.chibi" --entry Main \
        --output "$work/$source" --llvm "$llvm" --target "$triple" --level 0 --jobs 1 > "$work/$source.log" 2>&1 || failed=$?
    test "$failed" -eq 1
    test "$(cat "$work/$source")" = preserved
done
echo 'native indexed foreign buffer bounds, categories and source gates passed'
