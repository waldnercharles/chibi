#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native remainder: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-remainder.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for level in 0 2; do
    for source in Remainder RemainderShadow; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/math_scalar/$source.chibi" --entry Main \
            --output "$work/$source-$level" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
        "$work/$source-$level"
    done
    printf 'PASS native fmod precision, defaults, evaluation order, IEEE and shadow O%s\n' "$level"
done
for source in RemainderEmpty RemainderBoolean; do
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/math_scalar/$source.chibi" --entry Main \
        --output "$work/rejected" --llvm "$llvm" --target "$target" > "$work/rejected.log" 2>&1; then
        echo "native remainder: $source must be rejected" >&2
        exit 1
    fi
    case "$source" in
        RemainderEmpty) expected="Ambiguous function 'fmod'" ;;
        *) expected='no overload of this native takes these arguments' ;;
    esac
    grep -F "$expected" "$work/rejected.log" > /dev/null
    test ! -e "$work/rejected"
done
# The system-math dependency must not turn a requested freestanding program
# into an environment-dependent link, nor overwrite a previous executable.
if [ "$target" = x86_64-unknown-linux-gnu ]; then
    printf '%s\n' 'package static_remainder' \
        'Main :: () -> int { value: number = 7.5; divisor: number = 2.0; result := fmod(value, divisor); if result == 1.5 { return 0; } return 1; }' > "$work/Static.chibi"
    printf 'previous output' > "$work/static"
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Static.chibi" --entry Main --output "$work/static" --llvm "$llvm" \
        --target "$target" --level 0 --static > "$work/static.log" 2>&1; then
        echo 'native remainder: static system math must be rejected' >&2
        exit 1
    fi
    grep -F 'static programs cannot link system math yet' "$work/static.log" > /dev/null
    test "$(cat "$work/static")" = 'previous output'
    test ! -e "$work/static.0.ll"
    printf '%s\n' 'package shared_remainder' \
        'Main :: () -> int { return int(fmod(7.5, 2.0)); }' \
        'Frame :: () -> int { return int(fmod(9.5, 2.0)); }' > "$work/Shared.chibi"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Shared.chibi" --shared --output "$work/math.so" --llvm "$llvm" \
        --target "$target" --level 0
    # A new loader process must see libm as a declared dependency. A process
    # already linked with libm could mask an unresolved-symbol regression.
    /lib64/ld-linux-x86-64.so.2 --list "$work/math.so" > "$work/shared.log"
    grep -F 'libm.so' "$work/shared.log" > /dev/null
    printf 'PASS Linux shared math dependency and early static rejection\n'
fi
