#!/bin/sh
# Real native task programs at O0 and O2, with allocator-balance checks.
# Usage: sh test/native_aot/tasks/run.sh NATIVE_COMPILER LLVM_BIN
set -eu
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
driver=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
case "$driver" in /*) ;; *) driver=$(pwd -P)/$driver ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native tasks: this test requires macOS arm64 or Linux x64' >&2; exit 77 ;;
esac
version=$("$llvm/opt" --version | sed -n 's/.*LLVM version \([0-9][0-9]*\).*/\1/p' | head -1)
if [ -z "$version" ] || [ "$version" -lt 22 ]; then
    echo 'native tasks: LLVM 22 or later is required' >&2
    exit 2
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-tasks.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/llvm"
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in
        *.ll)
            if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
                sed 's/define i32 @chibi_scalar(ptr %ctx)/define i32 @chibi_task_test_entry(ptr %ctx)/' "$source" > "$source.audit"
                cat "$CHIBI_TASK_AUDIT" >> "$source.audit"
                mv "$source.audit" "$source"
            fi ;;
    esac
done
remaining=$#
while [ "$remaining" -gt 0 ]; do
    argument=$1
    shift
    case "$argument" in -passes=*) argument="-passes=default<O$CHIBI_TASK_LEVEL>" ;; esac
    set -- "$@" "$argument"
    remaining=$((remaining - 1))
done
exec "$CHIBI_TASK_REAL_LLVM/opt" "$@"
WRAPPER
cat > "$work/llvm/llc" <<'WRAPPER'
#!/bin/sh
set -eu
remaining=$#
while [ "$remaining" -gt 0 ]; do
    argument=$1
    shift
    case "$argument" in -O=*) argument="-O=$CHIBI_TASK_LEVEL" ;; esac
    set -- "$@" "$argument"
    remaining=$((remaining - 1))
done
exec "$CHIBI_TASK_REAL_LLVM/llc" "$@"
WRAPPER
chmod +x "$work/llvm/opt" "$work/llvm/llc"
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_TASK_REAL_LLVM=$llvm CHIBI_TASK_AUDIT=$root/test/native_aot/tasks/audit.ll
for level in 0 2; do
    export CHIBI_TASK_LEVEL=$level
    for folder in "$root"/test/native_aot/tasks/cases/*; do
        name=$(basename "$folder")
        program=$work/$name-O$level
        "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$folder/Main.chibi" --entry Main --output "$program" \
            --llvm "$work/llvm" --target "$target" --jobs 1
        "$program"
        printf 'PASS native tasks %s O%s, allocations balanced\n' "$name" "$level"
    done
    for folder in "$root"/test/native_aot/tasks/failures/*; do
        name=$(basename "$folder")
        program=$work/$name-O$level
        "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$folder/Main.chibi" --entry Main --output "$program" \
            --llvm "$work/llvm" --target "$target" --jobs 1
        status=0
        "$program" > "$work/failure" 2>&1 || status=$?
        test "$status" -eq 134
        grep -q 'assertion failed' "$work/failure"
        printf 'PASS native tasks failure %s O%s, allocations balanced\n' "$name" "$level"
    done
done
for folder in "$root"/test/native_aot/tasks/rejected/*; do
    name=$(basename "$folder")
    if "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$folder/Main.chibi" --entry Main --output "$work/rejected" \
        --llvm "$llvm" --target "$target" --jobs 1 > "$work/rejection" 2>&1; then
        printf 'native tasks: unsupported case %s compiled\n' "$name" >&2
        exit 1
    fi
    case "$name" in
        await_result) grep -q 'awaits a call of a waiting function, or a task as a statement' "$work/rejection" ;;
        deadline) grep -q 'does not support cancel after yet' "$work/rejection" ;;
        dependency) grep -q 'waiting frames cannot own managed values or pointers yet' "$work/rejection" ;;
        managed_frame) grep -q 'waiting frames cannot own managed values or pointers yet' "$work/rejection" ;;
    esac
    test ! -e "$work/rejected"
    printf 'PASS native tasks rejection %s\n' "$name"
done
