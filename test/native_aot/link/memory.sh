#!/bin/sh
set -eu
compiler=$1
llvm=$2
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-compiler-memory.XXXXXX")
trap 'rm -rf "$work"' EXIT
case "$(uname -s)" in Darwin) triple=aarch64-apple-darwin ;; *) triple=x86_64-unknown-linux-gnu ;; esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/link/Memory.chibi" --entry Main \
    --llvm "$llvm" --target "$triple" --output "$work/builder" --jobs 2
"$work/builder" "$llvm" "$work" "$root/test/native_aot/link/Memory.ll"
