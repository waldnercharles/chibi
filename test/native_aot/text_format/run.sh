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
    env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/text_format/Main.chibi" --output "$work/program" --llvm "$llvm" \
        --target "$triple" --level "$level" --jobs 2
    env PATH= CC=false CXX=false "$work/program"
done
# Exercise the existing start-import assertions through their native entry.
env PATH= CC=false CXX=false "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/tools/compiler/native_link/tests/Tests.chibi" --entry Run --output "$work/imports" --llvm "$llvm" \
    --target "$triple" --level 0 --jobs 2
env PATH= CC=false CXX=false "$work/imports"
printf 'native format: scalar grammar, raw text, numeric modifiers and floats passed at O0/O2\n'
