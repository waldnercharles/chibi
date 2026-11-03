#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=${3:-$root/.chibi/native-managed-pointers}
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native managed pointers: unsupported test host' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/Collector.ll"
for name in Cells ScopedAliases Transfer Nullable Records WholeArrays WholeArrayForeign StringArrays OptionalCells Views ViewUnions ViewStrings ViewOwned TextSequences Tasks TaskPromises TaskCaptured TaskFailure TaskPromiseFailure; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/source/$name.chibi" --entry Main \
        --output "$work/$name.ll" --emit-llvm --target "$triple"
done
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/Views.chibi" --entry AppendStress \
    --output "$work/ViewAppend.ll" --emit-llvm --target "$triple"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/TextSequences.chibi" --entry SpanStress \
    --output "$work/TextSpan.ll" --emit-llvm --target "$triple"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/TextSequences.chibi" --entry BorrowedStress \
    --output "$work/TextBorrowed.ll" --emit-llvm --target "$triple"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/SourceProbe.chibi" --entry Main \
    --output "$work/source-probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/source-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" \
    "$work/Cells.ll" "$work/ScopedAliases.ll" "$work/Transfer.ll" "$work/Nullable.ll" "$work/Tasks.ll" "$work/TaskPromises.ll" "$work/TaskCaptured.ll" "$work/Records.ll" "$work/WholeArrays.ll" "$work/WholeArrayForeign.ll" "$work/StringArrays.ll" "$work/OptionalCells.ll" "$work/Views.ll" "$work/ViewUnions.ll" "$work/ViewStrings.ll" "$work/ViewOwned.ll" "$work/TextSequences.ll" "$work/TextSpan.ll" "$work/TextBorrowed.ll" "$work/ViewAppend.ll"
"$work/source-probe" --assertion "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" "$work/TaskFailure.ll" "$work/TaskPromiseFailure.ll"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/AssertionCleanup.chibi" --entry Main \
    --output "$work/AssertionCleanup.ll" --emit-llvm --target "$triple"
"$work/source-probe" --assertion "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" "$work/AssertionCleanup.ll"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/Callbacks.chibi" --entry Main \
    --output "$work/Callbacks.ll" --emit-llvm --target "$triple"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/Callbacks.chibi" --entry Progress \
    --output "$work/CallbackPointerless.ll" --emit-llvm --target "$triple"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/source/Callbacks.chibi" --entry Failure \
    --output "$work/CallbackFailure.ll" --emit-llvm --target "$triple"
for name in PatternRows InlineRecords; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/$name.chibi" --entry Main \
        --output "$work/$name-emitter" --llvm "$llvm" --target "$triple" --jobs 1
    "$work/$name-emitter" "$work/$name.ll"
done
"$work/source-probe" --callbacks "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" \
    "$work/Callbacks.ll" "$work/ViewOwned.ll" "$work/CallbackPointerless.ll" "$work/Tasks.ll" "$work/PatternRows.ll" "$work/InlineRecords.ll" "$work/TaskPromises.ll" "$work/TaskCaptured.ll"
"$work/source-probe" --callbacks --assertion "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" \
    "$work/CallbackFailure.ll" "$work/TaskPromiseFailure.ll"

for entry in LengthShrunk ReadShrunk CloneShrunk IndexOutside SetIndexBeforeRight SetShrunkBeforeRight SetShrunkDuringRight SetValidBeforeRight; do
    case "$entry" in
        LengthShrunk|ReadShrunk|CloneShrunk) kind=ViewOutOfRange ;;
        IndexOutside) kind=IndexOutOfRange ;;
        SetValidBeforeRight) kind=AssertionFailed ;;
        *) kind='another failure' ;;
    esac
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/source/ViewFailures.chibi" --entry "$entry" \
        --output "$work/ViewFailure-$entry.ll" --emit-llvm --target "$triple"
    "$work/source-probe" --failure "$kind" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" \
        "$work/ViewFailure-$entry.ll"
done
set +e
"$compiler" run --root "$root/tools/compiler" --core "$root/packages/core" --llvm "$llvm" \
    "$root/test/native_aot/managed_pointers/source/TaskFailure.chibi" > "$work/task-failure.log" 2>&1
failure_status=$?
set -e
[ "$failure_status" -eq 1 ] || { cat "$work/task-failure.log"; exit 1; }
grep -F 'TaskFailure.chibi:13:5:' "$work/task-failure.log"
grep -F 'Assertion failed: waiting task assertion preserves cleanup' "$work/task-failure.log"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/NativeProbe.chibi" --entry Main \
    --output "$work/native-probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/native-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/Runtime.ll" \
    "$root/test/native_aot/managed_pointers/SourceAudit.ll"
"$work/native-probe" --weak-null "$work/poll-weak-null.ll"
if [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ]; then
    sh "$root/test/native_aot/managed_pointers/weak.sh" "$llvm" "$work/poll-weak-null.ll" \
        "$root/test/native_aot/managed_pointers/PollWeak.ll" "$work/weak-null"
fi
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/ViewProbe.chibi" --entry Main \
    --output "$work/view-probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/view-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll"
sh "$root/test/native_aot/managed_pointers/view-schema.sh" "$compiler" "$llvm" "$work"
for level in 0 2; do
    for name in fallible_correlated_pointers_retain_valid_error_handling \
                fallible_joined_pointers_keep_unchanged_pointees \
                fallible_optional_pointer_aliases_are_inspected \
                optional_mutation_before_a_fresh_presence_guard_is_safe; do
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
            --root "$root/test/compiler/samples/fallible" "$root/test/compiler/samples/fallible/$name/facts" \
            > "$work/optional-$name-O$level.log" 2>&1 || { cat "$work/optional-$name-O$level.log"; exit 1; }
        grep -q '^PASS .*Calls$' "$work/optional-$name-O$level.log"
    done
done
printf 'prior native output\n' > "$work/sentinel"
for source in "$root"/test/native_aot/managed_pointers/source/rejected/*.chibi; do
    name=$(basename "$source" .chibi)
    cp "$work/sentinel" "$work/rejected"
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main --output "$work/rejected" --llvm "$llvm" \
        --target "$triple" --jobs 1 > "$work/rejected-$name.log" 2>&1; then
        echo "native managed pointers: unsupported $name was accepted" >&2
        exit 1
    fi
    grep -F 'native: rejected' "$work/rejected-$name.log" >/dev/null
    cmp "$work/sentinel" "$work/rejected"
done
sh "$root/test/native_aot/managed_pointers/growth.sh" "$compiler" "$work/growth"
echo 'native managed pointer source, collector and boundary proofs passed'
