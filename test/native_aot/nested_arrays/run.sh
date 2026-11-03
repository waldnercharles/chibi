#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native nested arrays: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-nested-arrays.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/llvm"
# Reuse the real source audit allocator: poison frees and require no live
# allocations after Main. No VM or C test host supplies ownership behavior.
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
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_VALUE_LLVM=$llvm CHIBI_VALUE_AUDIT=$root/test/native_aot/managed_values/Audit.ll
for level in 0 2; do
    for fixture in types/generic_array_calls_after_descriptor_growth imports/native_values collections/scoped_lent_locals collections/ranges; do
        suite=${fixture%%/*}
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
            --root "$root/test/compiler/samples/$suite" \
            "$root/test/compiler/samples/$fixture/facts" > "$work/original.log" 2>&1 || { cat "$work/original.log"; exit 1; }
        grep -F 'PASS project.Calls' "$work/original.log" > /dev/null
    done
    for source in Main Iteration Reverse; do
        for mode in ordinary audit; do
            tools=$llvm
            if [ "$mode" = audit ]; then tools=$work/llvm; fi
            "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
                --source "$root/test/native_aot/nested_arrays/$source.chibi" --entry Main \
                --output "$work/$source-$mode-O$level" --llvm "$tools" --target "$triple" --level "$level" --jobs 1
            "$work/$source-$mode-O$level"
        done
    done
    printf 'PASS nested descriptors, value copies and balanced ownership O%s\n' "$level"
done
# A filtered pointer constructor must reject without leaking a temporary
# type-table ordinal into the native view or replacing an earlier artifact.
printf 'preserved\n' > "$work/rejected"
set +e
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/nested_arrays/rejected/Pointer.chibi" --entry Main \
    --output "$work/rejected" --llvm "$llvm" --target "$triple" --level 0 --jobs 1 > "$work/rejected.log" 2>&1
status=$?
set -e
test "$status" -eq 1 || { cat "$work/rejected.log"; exit 1; }
test "$(cat "$work/rejected")" = preserved
grep -F 'Pointer.chibi:4:5: scalar proof supports only number, bool and string values' "$work/rejected.log" > /dev/null
if [ -n "$reference" ]; then
    for source in Main Iteration Reverse; do
        project=$work/reference-$source
        mkdir "$project"
        cp "$root/test/native_aot/nested_arrays/$source.chibi" "$project/Main.chibi"
        head -n 1 "$project/Main.chibi" > "$project/Tests.chibi"
        printf '%s\n' '@test' 'Copies :: () { result := Main(); assert(result == 0); }' >> "$project/Tests.chibi"
        "$reference" --project-root "$project" --test > "$project/reference.log" 2>&1 || { cat "$project/reference.log"; exit 1; }
        cat "$project/reference.log"
    done
fi
