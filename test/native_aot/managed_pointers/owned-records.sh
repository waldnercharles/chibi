#!/bin/sh
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
work=${3:?owned workspace}/owned-records
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
fixtures=$root/test/native_aot/managed_pointers
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native owned record cells: unsupported test host' >&2; exit 2 ;;
esac
mkdir -p "$work"
# This inventory is independent of run.sh and selects no managed/task suite.
for name in SourceProbe OwnedRecordImages OwnedRecordImagesHost; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --package . --source "$fixtures/$name.chibi" --entry Main --output "$work/$name" \
        --llvm "$llvm" --target "$triple" --jobs 1
done
cp "$fixtures/source/OwnedRecords.chibi" "$work/OwnedRecords.source.chibi"
for entry in Main Failure CountedCopies; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$fixtures/source/OwnedRecords.chibi" --entry "$entry" --emit-llvm \
        --target "$triple" --output "$work/OwnedRecords-$entry.ll"
done
cmp "$fixtures/source/OwnedRecords.chibi" "$work/OwnedRecords.source.chibi"
python3 "$fixtures/check-owned-record-failure.py" bind "$work/OwnedRecords.source.chibi" \
    "$work/OwnedRecords-Failure.ll" "$work/failure-site.json"
for callbacks in normal callbacks; do
    directory=$work/$callbacks
    mkdir -p "$directory/main" "$directory/failure"
    if [ "$callbacks" = callbacks ]; then
        "$work/SourceProbe" --callbacks "$directory/main" "$llvm" "$fixtures/SourceAudit.ll" "$work/OwnedRecords-Main.ll"
    else
        "$work/SourceProbe" "$directory/main" "$llvm" "$fixtures/SourceAudit.ll" "$work/OwnedRecords-Main.ll"
    fi
    status=0
    if [ "$callbacks" = callbacks ]; then
        "$work/SourceProbe" --callbacks --assertion "$directory/failure" "$llvm" "$fixtures/SourceAudit.ll" \
            "$work/OwnedRecords-Failure.ll" > "$directory/failure.stdout.log" 2> "$directory/failure.stderr.log" || status=$?
    else
        "$work/SourceProbe" --assertion "$directory/failure" "$llvm" "$fixtures/SourceAudit.ll" \
            "$work/OwnedRecords-Failure.ll" > "$directory/failure.stdout.log" 2> "$directory/failure.stderr.log" || status=$?
    fi
    # Preserve every diagnostic, including on setup or child failure. The
    # shared probe still requires each child's exit134 and live-zero cleanup.
    cat "$directory/failure.stdout.log"
    cat "$directory/failure.stderr.log" >&2
    [ "$status" -eq 0 ] || exit "$status"
    python3 "$fixtures/check-owned-record-failure.py" check "$work/OwnedRecords.source.chibi" \
        "$work/OwnedRecords-Failure.ll" "$work/failure-site.json" \
        "$directory/failure.stdout.log" "$directory/failure.stderr.log"
done
# 4096 counted record replacements must transfer the existing Text owner.
# The record and scalar cells plus fixed Text setup fit below 32 allocations.
sed 's/%bounded = icmp ule i64 %peak, 2048/%bounded = icmp ule i64 %total, 32/' \
    "$fixtures/SourceAudit.ll" > "$work/counted-audit.ll"
grep -Fq '%bounded = icmp ule i64 %total, 32' "$work/counted-audit.ll"
mkdir -p "$work/counted"
"$work/SourceProbe" "$work/counted" "$llvm" "$work/counted-audit.ll" "$work/OwnedRecords-CountedCopies.ll"
for mode in borrowed owned; do
    directory=$work/$mode
    mkdir -p "$directory"
    if [ "$mode" = borrowed ]; then
        sed '/^\/\/ The image runner extracts the prefix above this marker\./,$d' \
            "$fixtures/text_cells/Record.chibi" > "$directory/writer.chibi"
    else
        cp "$fixtures/text_cells/RecordOwned.chibi" "$directory/writer.chibi"
    fi
    "$work/OwnedRecordImages" "$directory" "$llvm" "$mode" "$fixtures/text_cells/Record.chibi" \
        "$directory/writer.chibi" "$fixtures/OwnedRecordOld.ll" "$fixtures/BoundaryTextAllocation.ll" "$root/packages/core"
    for level in 0 2; do
        "$work/OwnedRecordImagesHost" "$directory/owned-record-old-O$level.so" \
            "$directory/owned-record-writer-O$level.so" "$mode"
        echo "PASS owned record $mode O$level all Text owners and exact image leases"
        status=0
        "$work/OwnedRecordImagesHost" "$directory/owned-record-old-O$level.so" \
            "$directory/owned-record-broken-O$level.so" "$mode" > "$directory/broken-O$level.log" 2>&1 || status=$?
        [ "$status" -eq 33 ] && grep -Fxq 'owned record Text owner missing before unload' "$directory/broken-O$level.log" || {
            cat "$directory/broken-O$level.log" >&2
            echo "owned record $mode missing-import O$level: status $status" >&2; exit 1;
        }
        echo "PASS owned record $mode one-site bypass O$level cleanup before either unload"
    done
done

printf 'prior native output\n' > "$work/sentinel"
cp "$work/sentinel" "$work/rejected"
if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/source/rejected/OwnedArrayRecordCell.chibi" --entry Main --emit-llvm \
    --target "$triple" --output "$work/rejected" > "$work/rejected-owned-array.log" 2>&1; then
    echo 'native owned record cells: pointer-array field was accepted' >&2; exit 1
fi
grep -F 'native managed record cells require flat numeric, bool, string or pointer fields' "$work/rejected-owned-array.log"
cmp "$work/sentinel" "$work/rejected"
echo 'PASS owned record pointer-array field remains rejected without changing output'
