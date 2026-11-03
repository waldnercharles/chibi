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
    for source in Main Patterns Failure Mixed Promises PromiseFailure; do
        input=$root/test/native_aot/task_owners/$source.chibi
        if [ "$source" = Patterns ]; then input=$root/test/native_aot/patterns/OwnedWaiting.chibi; fi
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$input" --entry Main --output "$work/program" --llvm "$work/llvm" \
            --target "$target" --level "$level" --jobs 1
        result=0
        "$work/program" > "$work/result.log" 2>&1 || result=$?
        expected=0
        if [ "$source" = Failure ] || [ "$source" = PromiseFailure ]; then expected=134; fi
        if [ "$result" -ne "$expected" ]; then cat "$work/result.log"; echo "$source O$level status$result"; exit 1; fi
        printf 'PASS owned task %s O%s, poisoned frees and live0\n' "$source" "$level"
    done
done
sh "$root/test/native_aot/task_owners/generations.sh" "$compiler" "$llvm" "$work/llvm" "$work"
