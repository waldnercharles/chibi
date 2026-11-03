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
for name in Values Strings Iteration Records Effects Receivers ManagedRecords StringEffects ArrayPayload ArrayForeign Compound ArrayBacked ArrayValues Unions Optionals ArrayForeignPlace ForeignLoans; do
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
for name in ArrayCompound; do
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/map_programs/$name.chibi" --entry Main \
        --output "$work/$name" --llvm "$llvm" --target "$triple" --jobs 2 > "$work/$name.log" 2>&1; then
        echo "native map programs: unsupported $name was accepted" >&2
        exit 1
    fi
    expected='native mapped array elements currently assign only by ='
    grep -F "$expected" "$work/$name.log" >/dev/null
done
echo 'native map programs passed'
