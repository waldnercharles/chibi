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
for level in 0 2; do
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/imports" "$root/test/compiler/samples/imports/array_methods/facts" \
        > "$work/original.log" 2>&1 || { cat "$work/original.log"; exit 1; }
    grep -F 'PASS project.Calls' "$work/original.log" > /dev/null
    for source in Values Places Replace; do
        for mode in ordinary audited; do
            tools=$llvm
            if [ "$mode" = audited ]; then tools=$work/llvm; fi
            "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
                --source "$root/test/native_aot/array_operations/$source.chibi" --entry Main \
                --output "$work/$source-$mode" --llvm "$tools" --target "$target" --level "$level" --jobs 1
            "$work/$source-$mode"
        done
    done
    printf 'PASS array source values, canonical places and balanced ownership O%s\n' "$level"
done
if [ -n "$reference" ]; then
    for source in Values Replace; do
        project=$work/reference-$source
        mkdir "$project"
        cp "$root/test/native_aot/array_operations/$source.chibi" "$project/Main.chibi"
        head -n 1 "$project/Main.chibi" > "$project/Tests.chibi"
        printf '%s\n' '@test' 'Check :: () { result := Main(); assert(result == 0); }' >> "$project/Tests.chibi"
        "$reference" --project-root "$project" --test > "$project/reference.log" 2>&1 || { cat "$project/reference.log"; exit 1; }
        cat "$project/reference.log"
    done
fi
