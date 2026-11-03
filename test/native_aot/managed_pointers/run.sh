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
for name in Cells ScopedAliases Transfer Nullable Records Tasks TaskFailure; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/source/$name.chibi" --entry Main \
        --output "$work/$name.ll" --emit-llvm --target "$triple"
done
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/SourceProbe.chibi" --entry Main \
    --output "$work/source-probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/source-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" \
    "$work/Cells.ll" "$work/ScopedAliases.ll" "$work/Transfer.ll" "$work/Nullable.ll" "$work/Tasks.ll" "$work/Records.ll"
"$work/source-probe" --assertion "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll" "$work/TaskFailure.ll"
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
echo 'native managed pointer source, collector and boundary proofs passed'
