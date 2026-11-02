#!/bin/sh
# Real native formatting, including the VM scalar conversion reference case.
set -eu
driver=${1:?usage: run.sh DRIVER LLVM_BIN}
llvm=${2:?usage: run.sh DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-format.XXXXXX")
trap 'rm -rf "$work"' EXIT
case $(uname -s)-$(uname -m) in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native format: unsupported test host' >&2; exit 2 ;;
esac
for level in 0 2; do
    for source in Main Strings; do
    env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_format/$source.chibi" --entry Main --output "$work/program" --llvm "$llvm" \
        --target "$triple" --level "$level" --jobs 2
    env PATH= CC=false CXX=false "$work/program"
    done
done
# Exercise the existing start-import assertions through their native entry.
env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/compiler/native_link/tests/Tests.chibi" --entry Run --output "$work/imports" --llvm "$llvm" \
    --target "$triple" --level 0 --jobs 2
env PATH= CC=false CXX=false "$work/imports"
printf 'native format: scalar grammar, raw text, numeric modifiers and floats passed at O0/O2\n'

# Check the destination-specific import contract in a real linked image.
for source in Main Strings; do
    env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_format/$source.chibi" --entry Main --output "$work/$source.exe" --llvm "$llvm" \
        --target x86_64-pc-windows-msvc --level 0 --jobs 1
    if [ "$source" = Main ]; then
        strings "$work/$source.exe" | grep -i 'ucrtbase.dll' >/dev/null
    else
        if strings "$work/$source.exe" | grep -Ei 'ucrtbase.dll|msvcrt.dll' >/dev/null; then
            echo 'string-only formatting unexpectedly imports the C runtime' >&2; exit 1
        fi
    fi
done
if [ -n "${3:-}" ]; then
    for source in Main Strings; do
        mkdir "$work/reference-$source"
        cp "$root/test/native_aot/text_format/$source.chibi" "$work/reference-$source/Main.chibi"
        package=text_format
        if [ "$source" = Strings ]; then package=text_format_strings; fi
        printf 'package %s\n@test\nValues :: () { result := Main(); assert(result == 0); }\n' "$package" > "$work/reference-$source/Tests.chibi"
        "$3" --project-root "$work/reference-$source" --test
    done
fi

sh "$root/test/native_aot/windows_decimal/run.sh" "$driver" "$llvm" "${3:-}"
