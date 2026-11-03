#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native clock embedding requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-clock-host.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sh "$root/test/native_aot/host_clock/projection.sh" "$compiler" "$llvm"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/host_clock/FactoryProbe.chibi" --entry Main \
    --output "$work/factory" --llvm "$llvm" --target "$target" --level 2
"$work/factory" "$work" "$llvm" "$root/test/native_aot/host_clock/Factory.ll"
"$llvm/llc" -mtriple="$target" -relocation-model=pic -filetype=obj \
    "$root/test/native_aot/host_clock/Provider.ll" -o "$work/provider.o"
"$llvm/llvm-ar" rcs "$work/provider.a" "$work/provider.o"
sed "s|@PROVIDER@|$work/provider.a|g" "$root/test/native_aot/host_clock/HostProbe.chibi" > "$work/Host.chibi"
if [ "$target" = x86_64-unknown-linux-gnu ]; then
    printf 'unchanged' > "$work/static-host"
    set +e
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Host.chibi" --entry Main --output "$work/static-host" \
        --llvm "$llvm" --target "$target" --static > "$work/static.log" 2>&1
    result=$?
    set -e
    if [ "$result" -ne 2 ]; then cat "$work/static.log"; exit 1; fi
    grep -F 'clock host factory links libc dynamically' "$work/static.log"
    [ "$(cat "$work/static-host")" = unchanged ]
fi
mkdir "$work/clock images"
for level in 0 2; do
    first="$work/clock images/first-$level.so"
    second="$work/clock images/second-$level.so"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/host_clock/Guest.chibi" --collection "host=$root/packages/host" \
        --shared --output "$first" --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    cp "$first" "$second"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Host.chibi" --entry Main --output "$work/host" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    for mode in normal negative invalid; do
        set +e
        "$work/host" "$first" "$second" "$mode" > "$work/result.log" 2>&1
        result=$?
        set -e
        expected=0
        if [ "$mode" != normal ]; then expected=134; fi
        if [ "$result" -ne "$expected" ]; then cat "$work/result.log"; exit 1; fi
        printf 'PASS native clock images %s O%s\n' "$mode" "$level"
    done
done
if [ "$#" -gt 2 ]; then
    sh "$root/test/native_aot/host_clock/web.sh" "$compiler" "$llvm" "${3:?pass emcc}" "${4:?pass Wasm LLVM}" "${5:?pass Node}"
fi
