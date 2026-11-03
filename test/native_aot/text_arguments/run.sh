#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM directory}
reference=${3:-}
root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-text-arguments.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native text arguments: unsupported test host' >&2; exit 2 ;;
esac
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
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        "$root/test/compiler/samples/native_arguments" > "$work/original.log" 2>&1 || { cat "$work/original.log"; exit 1; }
    test "$(awk '/^PASS / { count++ } END { print count+0 }' "$work/original.log")" -eq 12
    for source in Main Shadow; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/text_arguments/$source.chibi" --entry Main \
            --output "$work/$source" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
        "$work/$source"
    done
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_arguments/Main.chibi" --entry Main \
        --output "$work/audited" --llvm "$work/llvm" --target "$target" --level "$level" --jobs 1
    "$work/audited"
    printf 'PASS source-order native text arguments and balanced ownership O%s\n' "$level"
done
if [ -n "$reference" ]; then
    for source in Main Shadow; do
        mkdir "$work/$source-reference"
        cp "$root/test/native_aot/text_arguments/$source.chibi" "$work/$source-reference/Main.chibi"
        package=text_arguments
        if [ "$source" = Shadow ]; then package=text_shadow; fi
        printf '%s\n' "package $package" '@test' 'Values :: () { result := Main(); assert(result == 0); }' > "$work/$source-reference/Tests.chibi"
        "$reference" --project-root "$work/$source-reference" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
        cat "$work/reference.log"
    done
fi
