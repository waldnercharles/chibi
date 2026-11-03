#!/bin/sh
# Real runtime scope/task interaction. Generated @test dispatch is tested by
# the native CLI suite; this probe isolates each suspension/spawn instruction.
set -eu
stage=${1:?native compiler required}
llvm=${2:?LLVM directory required}
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$stage" in /*) ;; *) stage=$(pwd -P)/$stage ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native test task effects: macOS arm64 or Linux x64 required' >&2; exit 77 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-test-task-busy.XXXXXX")
trap 'rm -rf "$work"' EXIT
cp -R "$repo/test/native_aot/task_busy/project" "$work/project"
cp -R "$repo/test/native_aot/task_busy/quota" "$work/quota"
cp -R "$repo/test/native_aot/task_busy/direct" "$work/direct"
for level in 0 2; do
    build=$("$stage" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --root "$work/project" --entry Main --llvm "$llvm" --target "$target" --level "$level" --jobs 2 2>&1) || {
        printf '%s\n' "$build"; exit 1;
    }
    program=${build#native: }
    program=${program% is up to date}
    grep -q 'call void @chibi_native_test_busy(i32 0)' "${program%/program}/source.ll"
    grep -q 'call void @chibi_native_test_busy(i32 1)' "${program%/program}/source.ll"
    for index in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
        wanted=134
        case "$index" in 0|10) wanted=0 ;; esac
        result=0
        "$program" "$index" > "$work/case.log" 2>&1 || result=$?
        if [ "$result" -ne "$wanted" ]; then
            cat "$work/case.log"
            echo "native test task effects: case $index O$level expected $wanted, got $result" >&2
            exit 1
        fi
        if grep -q 'after rejected' "$work/case.log"; then cat "$work/case.log"; exit 1; fi
        case "$index" in
            0) grep -q 'after ready await' "$work/case.log" ;;
            1|4) grep -q 'before yield' "$work/case.log" ;;
            2|6|7) grep -q 'pending before yield' "$work/case.log" ;;
            3)
                grep -q 'spawn argument' "$work/case.log"
                if grep -q 'pending before yield' "$work/case.log"; then cat "$work/case.log"; exit 1; fi ;;
            5|14) grep -q 'ready body' "$work/case.log" ;;
            8) grep -q 'before zero delay' "$work/case.log" ;;
            10) grep -q 'after quota-scoped task' "$work/case.log" ;;
        esac
        if [ "$wanted" -eq 134 ]; then
            reason='a test must finish without waiting'
            [ "$index" -ne 3 ] || reason='a test cannot start a task'
            grep -q "$reason" "$work/case.log"
        fi
        if [ "$index" -eq 4 ]; then
            grep -q 'expected AssertionFailed, but it failed with another failure' "$work/case.log"
        fi
    done
    build=$("$stage" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --root "$work/quota" --entry Main --llvm "$llvm" --target "$target" --level "$level" --jobs 2 2>&1) || {
        printf '%s\n' "$build"; exit 1;
    }
    program=${build#native: }
    program=${program% is up to date}
    if grep -q 'call void @chibi_native_test_busy' "${program%/program}/source.ll"; then
        echo 'native test task effects: quota-only program gained Busy calls' >&2; exit 1
    fi
    "$program" > "$work/quota.log" 2>&1
    grep -q 'quota task finished' "$work/quota.log"
    build=$("$stage" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --root "$work/direct" --entry Main --llvm "$llvm" --target "$target" --level "$level" --jobs 2 2>&1) || {
        printf '%s\n' "$build"; exit 1;
    }
    program=${build#native: }
    program=${program% is up to date}
    "$program" 0
    "$stage" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --source "$repo/test/native_aot/task_busy/scalar_foreign/Main.chibi" --entry Main \
        --output "$work/scalar-foreign" --llvm "$llvm" --target "$target" --level "$level" --jobs 2
    "$work/scalar-foreign"
    printf 'PASS native test task effects O%s\n' "$level"
done
for source in "$repo"/test/native_aot/task_busy/rejected/*.chibi; do
    if "$stage" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --source "$source" --entry Main --output "$work/rejected" --llvm "$llvm" \
        --target "$target" --jobs 2 > "$work/rejection.log" 2>&1; then
        echo 'native test task effects: an unsupported waiting foreign ABI compiled' >&2; exit 1
    fi
    grep -q 'native foreign call differs from its operands' "$work/rejection.log"
    test ! -e "$work/rejected"
done
printf '%s\n' 'PASS native test waiting foreign ABI boundaries'
