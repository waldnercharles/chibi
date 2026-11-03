#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native remove_item requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-remove-item.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sh "$root/test/native_aot/remove_item/runtime.sh" "$compiler" "$llvm"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/apps/host/Host.chibi" --entry Main --output "$work/host" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 2
for level in 0 2; do
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/types" \
        "$root/test/compiler/samples/types/record_array_fields_survive_set_and_remove/facts"
    for source in Values Ordering Shadow; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/remove_item/$source.chibi" --entry Main \
            --output "$work/$source" --llvm "$llvm" --target "$target" --level "$level" --jobs 2
        "$work/$source"
    done
    mkdir "$work/family-$level"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/remove_item/Family.chibi" --shared \
        --output "$work/family-$level/live-0.so" --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    result=0
    (cd "$work/family-$level" && timeout 30 "$work/host") || result=$?
    test "$result" -eq 42
    printf 'PASS remove_item source values, argument order and authored shadow O%s\n' "$level"
done
printf 'preserved' > "$work/rejected"
result=0
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/remove_item/Rejected.chibi" --entry Main \
    --output "$work/rejected" --llvm "$llvm" --target "$target" --level 0 > "$work/rejected.log" 2>&1 || result=$?
test "$result" -eq 1
test "$(cat "$work/rejected")" = preserved
grep -F 'argument differs from its parameter type' "$work/rejected.log" >/dev/null
for source in Values Ordering Reentry; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/remove_item/$source.chibi" --entry Main \
        --output "$work/$source.ll" --emit-llvm --target "$target"
done
sed '/^declare i32 @chibi_value_equal_test_arm()$/d' "$work/Reentry.ll" > "$work/reentry-defined.ll"
mv "$work/reentry-defined.ll" "$work/Reentry.ll"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/SourceProbe.chibi" --entry Main \
    --output "$work/source-probe" --llvm "$llvm" --target "$target" --level 2 --jobs 2
"$work/source-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" "$work/Values.ll" "$work/Ordering.ll"
# Keep the real execution-family owner. Only the next equality scratch
# allocation reenters collect, and the final check proves that it happened.
sed -e 's/define internal ptr @managed_source.allocate(/define internal ptr @managed_source.allocate_base(/' \
    -e 's/%valid = and i1 %balanced, %bounded/%collected = load i32, ptr @value_equal_test.collected\n  %once = icmp eq i32 %collected, 1\n  %balanced.bound = and i1 %balanced, %bounded\n  %valid = and i1 %balanced.bound, %once/' \
    "$root/test/native_aot/managed_pointers/SourceAudit.ll" > "$work/reentry-audit.ll"
cat "$root/test/native_aot/remove_item/Reentry.ll" >> "$work/reentry-audit.ll"
"$work/source-probe" "$work" "$llvm" "$work/reentry-audit.ll" "$work/Reentry.ll"
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/remove_item/Ordering.chibi" "$work/reference/Main.chibi"
    printf '%s\n' 'package remove_order' '@test' 'Check :: () { result := Value(); assert(result == 0); }' > "$work/reference/Tests.chibi"
    "$reference" --project-root "$work/reference" --test
fi
