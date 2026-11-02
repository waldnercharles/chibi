#!/bin/sh
# Real source ownership checks at O0/O2, with poisoned frees and zero leaks.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
source=${4:-$root/test/native_aot/managed_values/Records.chibi}
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
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main \
        --output "$work/records-O$level" --llvm "$work/llvm" --target "$triple" --level "$level" --jobs 1
    "$work/records-O$level"
    printf 'PASS native managed records, arrays and unions O%s, allocations balanced\n' "$level"
done
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/managed_values/Records.chibi" "$work/reference/Records.chibi"
    printf '%s\n' 'package managed_records' '@test' 'Calls :: () { FunctionalArrays(); }' > "$work/reference/Tests.chibi"
    "$reference" --project-root "$work/reference" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    cat "$work/reference.log"
fi
