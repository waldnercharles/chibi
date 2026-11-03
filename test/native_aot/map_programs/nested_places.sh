#!/bin/sh
# Nested map owner lookup follows the RHS; expression reads keep their own categories.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
work=${3:?pass the scratch folder}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native nested map places: unsupported test host' >&2; exit 2 ;;
esac
for level in 0 2; do
    for name in Initial Compound MissingMap Late DeletedMap Negative DeepInitial DeepLate DeepArrayInitial DeepArrayLate ArrayEndInitial ArrayEndMixedInitial ArrayEndLate ArrayEndIndex MethodInitial MethodLate MethodArrayLate MethodRecordGrowth; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/map_programs/nested_places/$name.chibi" --entry Main \
            --output "$work/$name-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 1
        failed=0
        "$work/$name-O$level" > "$work/$name-O$level.log" 2>&1 || failed=$?
        test "$failed" -eq 134
        expected='native failure: another failure'
        case "$name" in Initial|Compound|MissingMap|DeepInitial|DeepArrayInitial|ArrayEndIndex|MethodInitial) expected=AssertionFailed ;; esac
        grep -F "$expected" "$work/$name-O$level.log" >/dev/null
        if grep -F IndexOutOfRange "$work/$name-O$level.log" >/dev/null; then
            cat "$work/$name-O$level.log" >&2
            exit 1
        fi
    done
done
echo 'native nested map failure order and categories passed'
