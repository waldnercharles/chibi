#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native array operations: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-array-edits.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/array_operations/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$target" --level 2 --jobs 2
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/apps/host/Host.chibi" --entry Main --output "$work/host" \
    --llvm "$llvm" --target "$target" --level 2 --jobs 1
mkdir "$work/ordinary" "$work/audited"
"$work/probe" "$llvm" "$work/ordinary"
"$work/probe" "$llvm" "$work/audited" "$root/test/native_aot/managed_values/Audit.ll"

mkdir "$work/llvm"
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            sed 's/define i32 @chibi_scalar(ptr %ctx)/define i32 @chibi_value_test_entry(ptr %ctx)/' "$source" > "$source.audit"
            cat "$CHIBI_VALUE_AUDIT" >> "$source.audit"
            mv "$source.audit" "$source"
        fi ;;
    esac
done
exec "$CHIBI_VALUE_LLVM/opt" "$@"
WRAPPER
chmod +x "$work/llvm/opt"
ln -s "$llvm/llc" "$work/llvm/llc"
linker=ld.lld
if [ "$target" = aarch64-apple-darwin ]; then linker=ld64.lld; fi
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_VALUE_LLVM=$llvm CHIBI_VALUE_AUDIT=$root/test/native_aot/managed_values/Audit.ll
mkdir "$work/checked"
cp "$root/test/native_aot/array_operations/checked/Main.chibi" "$work/checked/Main.chibi"
check_brackets() {
    for name in BracketNegative BracketPastEnd PlainBeforeRhs CompoundBeforeRhs FieldBeforeRhs WrongIndexCategory LateUnavailable; do
        grep -F "FAIL project.$name" "$1" > /dev/null || return 1
    done
    grep -F 'PASS project.ExpressionBounds' "$1" > /dev/null || return 1
    grep -F '8 tests, 7 failed' "$1" > /dev/null || return 1
    count=$(grep -Ec 'Referenced (array element|storage) is unavailable' "$1")
    [ "$count" -eq 7 ] || return 1
    if grep -F 'RHS evaluated' "$1" > /dev/null; then return 1; fi
}
for level in 0 2; do
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/imports" "$root/test/compiler/samples/imports/array_methods/facts" \
        > "$work/original.log" 2>&1 || { cat "$work/original.log"; exit 1; }
    grep -F 'PASS project.Calls' "$work/original.log" > /dev/null
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/comptime" "$root/test/compiler/samples/comptime/comptime_records_nest_and_hold_arrays/facts" \
        > "$work/original-records.log" 2>&1 || { cat "$work/original-records.log"; exit 1; }
    grep -F 'PASS project.Calls' "$work/original-records.log" > /dev/null
    for source in Values Places Replace Assignments RecordFields RetainedFields Resize Ranges; do
        for mode in ordinary audited; do
            tools=$llvm
            if [ "$mode" = audited ]; then tools=$work/llvm; fi
            "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
                --source "$root/test/native_aot/array_operations/$source.chibi" --entry Main \
                --output "$work/$source-$mode" --llvm "$tools" --target "$target" --level "$level" --jobs 1
            "$work/$source-$mode"
        done
    done
    for entry in Method Functional Field Map; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/array_operations/ResizeGrowth.chibi" --entry "$entry" \
            --output "$work/resize-growth" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
        status=0
        ("$work/resize-growth" > "$work/resize-growth.log" 2>&1) || status=$?
        [ "$status" -eq 134 ] || { cat "$work/resize-growth.log"; echo "resize $entry status $status, expected 134" >&2; exit 1; }
    done
    mkdir "$work/shared-O$level"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/array_operations/ResizeFailure.chibi" --shared \
        --output "$work/shared-O$level/live-0.so" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    status=0
    (cd "$work/shared-O$level" && "$work/host" > log 2>&1) || status=$?
    [ "$status" -eq 134 ] || { cat "$work/shared-O$level/log"; echo "shared resize status $status, expected 134" >&2; exit 1; }
    result=0
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" "$work/checked" > "$work/checked.log" 2>&1 || result=$?
    [ "$result" -eq 1 ] || { cat "$work/checked.log"; exit 1; }
    check_brackets "$work/checked.log" || { cat "$work/checked.log"; exit 1; }
    printf 'PASS array source values, canonical places and balanced ownership O%s\n' "$level"
done
if [ -n "$reference" ]; then
    for source in Values Replace Assignments RecordFields Ranges; do
        project=$work/reference-$source
        mkdir "$project"
        cp "$root/test/native_aot/array_operations/$source.chibi" "$project/Main.chibi"
        head -n 1 "$project/Main.chibi" > "$project/Tests.chibi"
        printf '%s\n' '@test' 'Check :: () { result := Main(); assert(result == 0); }' >> "$project/Tests.chibi"
        "$reference" --project-root "$project" --test > "$project/reference.log" 2>&1 || { cat "$project/reference.log"; exit 1; }
        cat "$project/reference.log"
    done
    result=0
    "$reference" --project-root "$work/checked" --test > "$work/checked-reference.log" 2>&1 || result=$?
    [ "$result" -eq 1 ] || { cat "$work/checked-reference.log"; exit 1; }
    check_brackets "$work/checked-reference.log" || { cat "$work/checked-reference.log"; exit 1; }
fi
