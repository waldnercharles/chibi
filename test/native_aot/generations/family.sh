#!/bin/sh
# Called by the generations suite with its matching native host and workspace.
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
level=${3:?optimization level}
host=${4:?native generation host}
work=${5:?owned generation workspace}/family
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) exit 77 ;;
esac
mkdir -p "$work"
child=
cleanup() {
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
wait_marker() {
    marker=$1
    log=$2
    tries=0
    while [ ! -f "$marker" ]; do
        if ! kill -0 "$child" 2>/dev/null; then cat "$log" >&2; exit 1; fi
        tries=$((tries + 1))
        if [ "$tries" -ge 1000 ]; then cat "$log" >&2; echo "missing $marker" >&2; exit 1; fi
        sleep 0.01
    done
}
compile() {
    source=$1
    output=$2
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/generations/$source.chibi" --output "$output" \
        --llvm "$llvm" --target "$triple" --shared --level "$level" --jobs 1
}
compile ManagedTasks "$work/tasks.so"
compile ManagedTasksEdited "$work/edited.so"
compile ManagedTaskFailure "$work/failure.so"
for mode in tasks failure; do
    run=$work/$mode
    mkdir "$run"
    cp "$work/$mode.so" "$run/live-0.so"
    (cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
    child=$!
    wait_marker "$run/managed-ready" "$run/host.log"
    cp "$work/edited.so" "$run/pending.so"
    mv "$run/pending.so" "$run/next.so"
    result=0
    wait "$child" || result=$?
    child=
    expected=7
    if [ "$mode" = failure ]; then expected=134; fi
    if [ "$result" -ne "$expected" ]; then cat "$run/host.log" >&2; echo "$mode status$result, expected$expected" >&2; exit 1; fi
    if [ "$mode" = tasks ]; then
        test "$(cat "$run/managed-task-done")" = old42
    fi
done
echo "native generations: suspended pointer/array roots, cancellation and old task failure O$level passed"

compile ManagedRecords "$work/records.so"
compile ManagedRecordsEdited "$work/records-edited.so"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/generations/RecordCellsProbe.chibi" --entry Main \
    --output "$work/record-probe" --llvm "$llvm" --target "$triple" --level 2 --jobs 1
mkdir "$work/record-cells"
(cd "$work/record-cells" && "$work/record-probe")
run=$work/records
mkdir "$run"
cp "$work/records.so" "$run/live-0.so"
(cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
child=$!
wait_marker "$run/managed-record-ready" "$run/host.log"
cp "$work/records-edited.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
result=0
wait "$child" || result=$?
child=
if [ "$result" -ne 7 ]; then cat "$run/host.log" >&2; echo "record roots status$result, expected7" >&2; exit 1; fi
test "$(cat "$run/managed-record-done")" = old42
echo "native generations: typed cyclic record roots, changed layouts and image cell leases O$level passed"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/generations/FamilyProbe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --level 2 --jobs 1
"$work/probe" "$work" "$llvm" "$root/test/native_aot/generations/FamilyCells.ll" "$level"
run=$work/cells
mkdir "$run"
cp "$work/family-old.so" "$run/live-0.so"
(cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
child=$!
wait_marker "$run/family-cell-ready" "$run/host.log"
for mode in bad-version bad-size; do
    cp "$work/family-$mode.so" "$run/pending.so"
    mv "$run/pending.so" "$run/next.so"
    wait_marker "$run/rejected.so" "$run/host.log"
    test ! -e "$run/family-cell-dropped"
    rm "$run/rejected.so"
done
cp "$work/family-new.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
result=0
wait "$child" || result=$?
child=
if [ "$result" -ne 7 ]; then cat "$run/host.log" >&2; echo "cell lease status$result, expected7" >&2; exit 1; fi
test -f "$run/family-cell-dropped"
grep -F 'Incompatible native collector family ABI or heap size' "$run/host.log" >/dev/null
echo "native generations: ABI rejection and old cell callback lease O$level passed"
