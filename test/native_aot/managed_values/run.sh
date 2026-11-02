#!/bin/sh
# Real source ownership checks at O0/O2, with poisoned frees and zero leaks.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
records_source=${4:-$root/test/native_aot/managed_values/Records.chibi}
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native managed values: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-managed-values.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/llvm"
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            sed -e 's/define i32 @chibi_scalar(ptr %ctx)/define i32 @chibi_value_test_entry(ptr %ctx)/' -e '/^declare i64 @chibi_value_test_allocations()$/d' "$source" > "$source.audit"
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
    for source in Records Foreign Backed Growth Fields; do
        input=$root/test/native_aot/managed_values/$source.chibi
        if [ "$source" = Records ]; then input=$records_source; fi
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$input" --entry Main \
            --output "$work/$source-O$level" --llvm "$work/llvm" --target "$triple" --level "$level" --jobs 1
        "$work/$source-O$level"
        printf 'PASS native managed %s O%s, allocations balanced\n' "$source" "$level"
    done
done
# Unsupported record and deep writable paths leave prior outputs untouched.
for source in RejectedForeignPath RejectedForeignRecord; do
    printf 'preserved\n' > "$work/rejected"
    set +e
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_values/$source.chibi" --entry Main \
        --output "$work/rejected" --llvm "$llvm" --target "$triple" --level 0 --jobs 1 > "$work/rejected.log" 2>&1
    rejected=$?
    set -e
    test "$rejected" -eq 1
    test "$(cat "$work/rejected")" = preserved
    grep -q 'native foreign record buffer needs recursive array-backed value ownership' "$work/rejected.log"
done
# The public native lifecycle operations also run directly, independently of
# scalar lowering's ownership decisions.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_values/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --level 2 --jobs 1
"$work/probe" "$work" "$llvm" "$root/test/native_aot/managed_values/Audit.ll"
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/managed_values/Records.chibi" "$work/reference/Records.chibi"
    printf '%s\n' 'package managed_records' '@test' 'Calls :: () { FunctionalArrays(); }' > "$work/reference/Tests.chibi"
    "$reference" --project-root "$work/reference" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    cat "$work/reference.log"
    mkdir "$work/reference-fields"
    cp "$root/test/native_aot/managed_values/Fields.chibi" "$work/reference-fields/Fields.chibi"
    printf '%s\n' 'package field_places' '@test' 'Calls :: () { result := Main(); assert(result == 0); }' > "$work/reference-fields/Tests.chibi"
    "$reference" --project-root "$work/reference-fields" --test > "$work/reference-fields.log" 2>&1 || { cat "$work/reference-fields.log"; exit 1; }
    cat "$work/reference-fields.log"
fi
