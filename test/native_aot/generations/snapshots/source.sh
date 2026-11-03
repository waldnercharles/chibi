#!/bin/sh
# Authored retain values use the transaction API, including owned local copies.
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
level=${3:?optimization level}
host=${4:?matching generation host}
work=${5:?owned snapshot workspace}
root=$(CDPATH='' cd -- "$(dirname "$0")/../../../.." && pwd -P)
fixtures=$root/test/native_aot/generations/snapshots
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) exit 77 ;;
esac
mkdir -p "$work/llvm" "$work/audit"
cat > "$work/llvm/opt" <<'OPT'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_host_call_Frame()' "$source"; then
            sed '/^declare i64 @chibi_value_test_allocations()$/d' "$source" > "$source.audit"
            cat "$CHIBI_SNAPSHOT_ROOT/test/native_aot/managed_values/Audit.ll" \
                "$CHIBI_SNAPSHOT_ROOT/test/native_aot/generations/snapshots/SourceAudit.ll" >> "$source.audit"
            mv "$source.audit" "$source"
        fi ;;
    esac
done
exec "$CHIBI_SNAPSHOT_LLVM/opt" "$@"
OPT
chmod +x "$work/llvm/opt"
ln -s "$llvm/llc" "$work/llvm/llc"
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_SNAPSHOT_ROOT=$root CHIBI_SNAPSHOT_LLVM=$llvm
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$fixtures/SourceProbe.chibi" \
    --entry Main --output "$work/probe" --llvm "$llvm" --target "$triple" --level 0 --jobs 1
for checked in 0 2; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$fixtures/SourceStandalone.chibi" \
        --entry Main --output "$work/standalone-O$checked" --llvm "$llvm" --target "$triple" --level "$checked" --jobs 1
    "$work/standalone-O$checked"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$fixtures/SourceAudit.chibi" \
        --output "$work/audit-O$checked.so" --shared --llvm "$work/llvm" --target "$triple" --level "$checked" --jobs 1
    (cd "$work/audit" && "$work/probe" "$work/audit-O$checked.so")
done
for variant in First Bad Edited; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$fixtures/Source$variant.chibi" \
        --output "$work/$variant.so" --shared --llvm "$llvm" --target "$triple" --level "$level" --jobs 1
done
run=$work/live
mkdir "$run"
cp "$work/First.so" "$run/live-0.so"
child=
cleanup() {
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
(cd "$run" && exec timeout 90 "$host") > "$run/log" 2>&1 &
child=$!
wait_marker() {
    tries=0
    while [ ! -e "$run/$1" ]; do
        if ! kill -0 "$child" 2>/dev/null; then cat "$run/log" >&2; exit 1; fi
        tries=$((tries + 1))
        if [ "$tries" -ge 1000 ]; then cat "$run/log" >&2; echo "missing source snapshot marker $1" >&2; exit 1; fi
        sleep 0.01
    done
}
wait_marker ready
cp "$work/Bad.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
wait_marker rejected.so
kill -0 "$child"
cp "$work/Edited.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
status=0
wait "$child" || status=$?
child=
if [ "$status" -ne 42 ]; then cat "$run/log" >&2; echo "source snapshot status $status, expected 42" >&2; exit 1; fi
echo "native retained source: O0/O2 poison/balance, bounded growth, rejected schema, type reindex and Main once passed O$level"
