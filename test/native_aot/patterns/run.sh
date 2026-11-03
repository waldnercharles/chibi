#!/bin/sh
# Original pattern suites plus managed captures and lexical cleanup at O0/O2.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native patterns require Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-patterns.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for level in 0 2; do
    for sample in types/patterns imports/callable_pattern_bindings_shadow_enclosing_locals; do
        suite=${sample%%/*}
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
            --root "$root/test/compiler/samples/$suite" "$root/test/compiler/samples/$sample/facts"
    done
    for source in Main Waiting OwnedWaiting Rows; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/patterns/$source.chibi" --entry Main \
            --output "$work/$source" --llvm "$llvm" --target "$target" --level "$level"
        "$work/$source"
    done
    printf 'PASS native pattern scopes O%s\n' "$level"
done
for source in "$root"/test/native_aot/patterns/rejected/*.chibi; do
    printf 'preserved\n' > "$work/rejected"
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main --output "$work/rejected" --llvm "$llvm" --target "$target" \
        > "$work/rejected.log" 2>&1; then
        echo "native patterns accepted invalid scope: $source" >&2; exit 1
    fi
    grep -q 'Pattern bindings require a matching if, while or switch arm' "$work/rejected.log"
    test "$(cat "$work/rejected")" = preserved
done
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/patterns/Main.chibi" "$work/reference/Main.chibi"
    "$reference" --project-root "$work/reference" --test
fi
