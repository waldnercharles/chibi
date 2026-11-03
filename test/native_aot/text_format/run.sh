#!/bin/sh
# Real native formatting, with ordinary scalar and collection reference cases.
set -eu
driver=${1:?usage: run.sh DRIVER LLVM_BIN}
llvm=${2:?usage: run.sh DRIVER LLVM_BIN}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-format.XXXXXX")
trap 'rm -rf "$work"' EXIT
case $(uname -s)-$(uname -m) in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native format: unsupported test host' >&2; exit 2 ;;
esac
for level in 0 2; do
    for source in Main Strings Aggregates Family Records OptionalRecords; do
        project=$root/tools/compiler
        if [ "$source" = Records ]; then project=$root/test/native_aot/text_format; fi
        env PATH= CC=false CXX=false "$driver" --compiler "$project" --core "$root/packages/core" \
            --source "$root/test/native_aot/text_format/$source.chibi" --entry Main --output "$work/program" --llvm "$llvm" \
            --target "$triple" --level "$level" --jobs 2
        env PATH= CC=false CXX=false "$work/program"
    done
done
# Keep the ordinary native-call restriction on managed pointer arguments.
for source in RejectedPointers RejectedRecordPointers RejectedUnionPointers RejectedUnionPayload RejectedDirectRecord; do
    printf 'preserved\n' > "$work/rejected"
    result=0
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_format/$source.chibi" --entry Main --output "$work/rejected" \
        --llvm "$llvm" --target "$triple" --level 0 --jobs 1 > "$work/rejected.log" 2>&1 || result=$?
    test "$result" -eq 1
    test "$(cat "$work/rejected")" = preserved
    expected='Native calls cannot retain managed pointer arguments'
    if [ "$source" = RejectedUnionPayload ]; then expected='native pointer payloads require union tracing support'; fi
    if ! grep -F "$expected" "$work/rejected.log" >/dev/null; then
        cat "$work/rejected.log" >&2
        exit 1
    fi
done
# The shared image supplies its own formatter and joins the host's heap family.
# Its exported helpers keep the existing ABI restriction on scoped references.
env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/apps/host/Host.chibi" --entry Main --output "$work/host" --llvm "$llvm" \
    --target "$triple" --level 2 --jobs 2
for level in 0 2; do
    for source in Family Records; do
        project=$root/tools/compiler
        if [ "$source" = Records ]; then project=$root/test/native_aot/text_format; fi
        mkdir "$work/$source-$level"
        env PATH= CC=false CXX=false "$driver" --compiler "$project" --core "$root/packages/core" \
            --source "$root/test/native_aot/text_format/$source.chibi" --shared --output "$work/$source-$level/live-0.so" \
            --llvm "$llvm" --target "$triple" --level "$level" --jobs 2
        result=0
        (cd "$work/$source-$level" && timeout 30 env PATH= CC=false CXX=false "$work/host") || result=$?
        test "$result" -eq 42
    done
done
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/text_format/Records.chibi" "$work/reference/Records.chibi"
    cp -R "$root/test/native_aot/text_format/record_summary_import" "$work/reference/record_summary_import"
    printf '%s\n' 'package record_summary' '@test' 'Calls :: () { assert(Main() == 0); }' > "$work/reference/Tests.chibi"
    "$reference" --project-root "$work/reference" --test
fi
# Exercise the existing start-import assertions through their native entry.
env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/compiler/native_link/tests/Tests.chibi" --entry Run --output "$work/imports" --llvm "$llvm" \
    --target "$triple" --level 0 --jobs 2
env PATH= CC=false CXX=false "$work/imports"
printf 'native format: scalars, collection snapshots and shared family passed at O0/O2\n'

# Check the destination-specific import contract in a real linked image.
for source in Main Strings Aggregates Family Records; do
    project=$root/tools/compiler
    if [ "$source" = Records ]; then project=$root/test/native_aot/text_format; fi
    env PATH= CC=false CXX=false "$driver" --compiler "$project" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_format/$source.chibi" --entry Main --output "$work/$source.exe" --llvm "$llvm" \
        --target x86_64-pc-windows-msvc --level 0 --jobs 1
    if [ "$source" = Main ]; then
        strings "$work/$source.exe" | grep -i 'ucrtbase.dll' >/dev/null
    else
        if strings "$work/$source.exe" | grep -Ei 'ucrtbase.dll|msvcrt.dll' >/dev/null; then
            echo 'text/collection formatting unexpectedly imports the C runtime' >&2; exit 1
        fi
    fi
done

sh "$root/test/native_aot/windows_decimal/run.sh" "$driver" "$llvm"
