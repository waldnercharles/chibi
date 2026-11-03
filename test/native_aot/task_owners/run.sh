#!/bin/sh
# Completion, cancellation and source failure all release the owned frame data.
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'owned task frames require Linux x86-64 or macOS arm64' >&2; exit 77 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-task-owners.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/llvm"
cp "$root/test/native_aot/task_owners/opt.sh" "$work/llvm/opt"
cp "$root/test/native_aot/task_owners/llc.sh" "$work/llvm/llc"
chmod +x "$work/llvm/opt" "$work/llvm/llc"
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_TASK_OWNER_LLVM=$llvm CHIBI_TASK_OWNER_AUDIT=$root/test/native_aot/task_owners
for level in 0 2; do
    for source in Main Patterns Failure Mixed Promises PromiseFailure NamedPromises MapPromises MapPromiseFailure MapJoinAllocations TaskMaps TaskObservers TaskObserverAllocations TaskObserverFailure; do
        input=$root/test/native_aot/task_owners/$source.chibi
        if [ "$source" = Patterns ]; then input=$root/test/native_aot/patterns/OwnedWaiting.chibi; fi
        if [ "$source" = TaskMaps ]; then input=$root/test/native_aot/map_programs/TaskMaps.chibi; fi
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$input" --entry Main --output "$work/program" --llvm "$work/llvm" \
            --target "$target" --level "$level" --jobs 1
        result=0
        "$work/program" > "$work/result.log" 2>&1 || result=$?
        expected=0
        if [ "$source" = Failure ] || [ "$source" = PromiseFailure ] || [ "$source" = MapPromiseFailure ] || [ "$source" = TaskObserverFailure ]; then expected=134; fi
        if [ "$result" -ne "$expected" ]; then cat "$work/result.log"; echo "$source O$level status$result"; exit 1; fi
        printf 'PASS owned task %s O%s, poisoned frees and live0\n' "$source" "$level"
    done
done
# Inferred observer arrays do not add source parameter, record-field or result
# syntax. Rejection must leave an existing program untouched.
for source in ObserverParameter ObserverField ObserverResult ObserverMethod; do
    printf 'prior observer-array output\n' > "$work/program"
    cp "$work/program" "$work/prior"
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/task_owners/rejected/$source.chibi" --entry Main \
        --output "$work/program" --llvm "$llvm" --target "$target" > "$work/rejected.log" 2>&1; then
        echo "$source unexpectedly compiled" >&2; exit 1
    fi
    case "$source" in
        ObserverParameter) message='parameter type is not supported yet' ;;
        ObserverField) message='record fields require a scalar, pointer, record, union or array of those types' ;;
        ObserverResult) message='result type is not supported yet' ;;
        ObserverMethod) message="argument differs from the mutating method's parameter type" ;;
    esac
    grep -F "$message" "$work/rejected.log" > /dev/null
    cmp "$work/program" "$work/prior"
    printf 'PASS %s rejected with prior output preserved\n' "$source"
done
sh "$root/test/native_aot/task_owners/generations.sh" "$compiler" "$llvm" "$work/llvm" "$work"
