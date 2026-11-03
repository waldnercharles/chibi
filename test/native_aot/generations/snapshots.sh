#!/bin/sh
# Explicit native snapshot ABI; source retain admission is a separate gate.
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
level=${3:?optimization level}
host=${4:?matching native generation host}
work=${5:?owned generation workspace}/snapshots
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) exit 77 ;;
esac
mkdir -p "$work"
fixtures=$root/test/native_aot/generations/snapshots
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/Probe.chibi" --entry Main --output "$work/probe" \
    --llvm "$llvm" --target "$triple" --level 0 --jobs 1
"$work/probe" "$work" "$llvm" "$fixtures/Runtime.ll" "$root/test/native_aot/managed_values/Audit.ll"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/Build.chibi" --entry Main --output "$work/build" \
    --llvm "$llvm" --target "$triple" --level 0 --jobs 1
"$work/build" "$work" "$llvm" "$fixtures/Frame.ll" "$level"
run=$work/live
mkdir "$run"
cp "$work/first.so" "$run/live-0.so"
child=
cleanup() {
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || :; wait "$child" 2>/dev/null || :; fi
}
trap cleanup EXIT HUP INT TERM
(cd "$run" && exec timeout 90 "$host") > "$run/host.log" 2>&1 &
child=$!
wait_marker() {
    marker=$1
    tries=0
    while [ ! -f "$run/$marker" ]; do
        if ! kill -0 "$child" 2>/dev/null; then cat "$run/host.log" >&2; exit 1; fi
        tries=$((tries + 1))
        if [ "$tries" -ge 1000 ]; then
            cat "$run/host.log" >&2
            echo "missing snapshot marker $marker" >&2
            exit 1
        fi
        sleep 0.01
    done
}
wait_marker ready
cp "$work/bad.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
wait_marker rejected.so
kill -0 "$child"
cp "$work/edited.so" "$run/pending.so"
mv "$run/pending.so" "$run/next.so"
result=0
wait "$child" || result=$?
child=
if [ "$result" -ne 42 ]; then
    cat "$run/host.log" >&2
    echo "snapshot host status$result, expected42" >&2
    exit 1
fi
test -f "$run/unloaded"
echo "native retained snapshots: callbacks O0/O2, rejected schema, native type reindex, actual unload and Main once passed O$level"

sh "$fixtures/source.sh" "$compiler" "$llvm" "$level" "$host" "$work/source"
