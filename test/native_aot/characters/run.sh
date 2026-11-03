#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM directory}
reference=${3:-}
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-characters.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/project"
cp -R "$repo/test/native_aot/characters/project" "$work/project/characters"
cp -R "$repo/test/native_aot/characters/shadow" "$work/project/shadow"
cp -R "$repo/test/native_aot/characters/foreign" "$work/project/foreign_character"
cp -R "$repo/test/native_aot/characters/procedure" "$work/project/procedure_character"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native characters: unsupported test host' >&2; exit 2 ;;
esac
mkdir "$work/callback"
cp "$repo/test/native_aot/characters/callback/Main.chibi" "$work/callback/Main.chibi"
"$llvm/llc" -mtriple="$target" -filetype=obj -relocation-model=pic -O2 \
    "$repo/test/native_aot/characters/callback/Runtime.ll" -o "$work/callback/runtime.o"
"$llvm/llvm-ar" rcs "$work/callback/libcallbacks.a" "$work/callback/runtime.o"
for level in 0 2; do
    CHIBI_CORE="$repo/packages/core" "$compiler" test "$work/project" --llvm "$llvm" -O"$level" > "$work/native.log" 2>&1 || { cat "$work/native.log"; exit 1; }
    awk '/^(PASS|FAIL) (characters|shadow|foreign_character|procedure_character)[.]/ { print }' "$work/native.log" | LC_ALL=C sort > "$work/native.results"
    diff -u "$repo/test/native_aot/characters/expected.txt" "$work/native.results" || { cat "$work/native.log"; exit 1; }
    CHIBI_CORE="$repo/packages/core" "$compiler" test "$repo/packages/core/utf8" --llvm "$llvm" -O"$level" > "$work/utf8.log" 2>&1 || { cat "$work/utf8.log"; exit 1; }
    "$compiler" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --source "$repo/test/native_aot/characters/foreign/Foreign.chibi" --entry Main \
        --output "$work/foreign-$level" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    "$work/foreign-$level"
    "$compiler" --compiler "$repo/tools/compiler" --core "$repo/packages/core" \
        --source "$work/callback/Main.chibi" --entry Main --output "$work/callback-$level" \
        --llvm "$llvm" --target "$target" --level "$level" --jobs 1
    "$work/callback-$level"
    printf 'PASS native characters and real UTF-8 tests O%s\n' "$level"
done
if [ -n "$reference" ]; then
    "$reference" --project-root "$work/project" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    awk '/^(PASS|FAIL) (characters|shadow)[.]/ { print }' "$work/reference.log" | LC_ALL=C sort > "$work/reference.results"
    awk '/^(PASS|FAIL) (characters|shadow)[.]/ { print }' "$work/native.results" > "$work/comparable.results"
    diff -u "$work/reference.results" "$work/comparable.results"
fi
for source in "$repo"/test/native_aot/characters/rejected/*.chibi; do
    set +e
    CHIBI_CORE="$repo/packages/core" "$compiler" run --llvm "$llvm" -O2 "$source" > "$work/rejected.log" 2>&1
    status=$?
    set -e
    [ "$status" -eq 1 ] || { cat "$work/rejected.log"; exit 1; }
    case "$source" in
        */Nominal.chibi) message='differs from its parameter' ;;
        */Named.chibi) message='char converts only with char(value)' ;;
        */Floating.chibi) message='char converts from an integer' ;;
        */Arithmetic.chibi) message='char arithmetic is not supported yet' ;;
        *) message='Constant does not fit in char' ;;
    esac
    grep -F "$message" "$work/rejected.log" > /dev/null || { cat "$work/rejected.log"; exit 1; }
done
printf '%s\n' 'PASS native character source rejections'
