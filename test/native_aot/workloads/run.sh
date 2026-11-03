#!/bin/sh
# The native workload timings and checksums, with no C test driver or VM.
set -eu
case "$(uname -s)-$(uname -m)" in
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native workloads: Linux x86-64 benchmark unsupported on this host' >&2; exit 0 ;;
esac
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-workloads.XXXXXX")
benchmark_child=
cleanup() {
    if [ -n "$benchmark_child" ]; then
        kill -TERM "$benchmark_child" 2>/dev/null || :
        wait "$benchmark_child" 2>/dev/null || :
    fi
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
run_owned() {
    "$@" &
    benchmark_child=$!
    completed=0
    wait "$benchmark_child" || completed=$?
    benchmark_child=
    return "$completed"
}
mkdir "$work/proof" "$work/run"
run_owned "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/workloads/Benchmark.chibi" --entry Main \
    --output "$work/benchmark" --llvm "$llvm" --target "$target" --level 2 --jobs 2
run_owned "$work/benchmark" --verify-supervision "$work/proof"
run_owned "$work/benchmark" "$compiler" "$llvm" "$root" "$work/run"
