#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=${3:-$root/.chibi/native-map-programs}
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native map programs: unsupported test host' >&2; exit 2 ;;
esac
for name in Values Strings Iteration Records Effects Receivers ManagedRecords StringEffects ArrayPayload ArrayForeign Compound ArrayBacked ArrayValues Unions Optionals ArrayForeignPlace ForeignLoans ArrayCompound ArrayPlaces NestedContainers NestedPlaces DeepPlaces; do
    for level in 0 2; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/map_programs/$name.chibi" --entry Main \
            --output "$work/$name-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 2
        "$work/$name-O$level"
    done
done
for name in MissingRecord UnionMissing ArrayMissing; do
    expected='native failure: MapKeyAbsent'
    if [ "$name" = ArrayMissing ]; then expected='native failure: another failure'; fi
    for level in 0 2; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/map_programs/$name.chibi" --entry Main \
            --output "$work/$name-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 2
        status=0
        "$work/$name-O$level" > "$work/$name-O$level.log" 2>&1 || status=$?
        test "$status" = 134
        grep -F "$expected" "$work/$name-O$level.log" >/dev/null
    done
done
sh "$root/test/native_aot/map_programs/places.sh" "$compiler" "$llvm" "$work/places"
sh "$root/test/native_aot/map_programs/nested_places.sh" "$compiler" "$llvm" "$work/nested-places"
for name in RecordMap NestedPointer TaskMaps; do
    expected='record fields require a scalar, pointer, record, union or array of those types'
    if [ "$name" = NestedPointer ]; then expected='scalar proof supports only number, bool and string values'; fi
    if [ "$name" = TaskMaps ]; then expected='native waiting frames cannot own managed values or pointers yet'; fi
    for level in 0 2; do
        output=$work/rejected-$name-O$level
        printf 'preserved' > "$output"
        result=0
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/map_programs/rejected/$name.chibi" --entry Main \
            --output "$output" --llvm "$llvm" --target "$triple" --level "$level" --jobs 2 > "$output.log" 2>&1 || result=$?
        test "$result" = 1
        grep -F "$expected" "$output.log" >/dev/null
        test "$(cat "$output")" = preserved
    done
done
echo 'native map programs passed'
