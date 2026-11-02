#!/bin/sh
set -eu
driver=${1:?usage: publication.sh NATIVE_DRIVER LLVM_BIN}
llvm=${2:?usage: publication.sh NATIVE_DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-wasm-publication.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat "$root/tools/graph/asset_wasm/Build.chibi" "$root/test/native_aot/asset_compiler/Publication.chibi.fragment" > "$work/Publication.chibi"
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root" --core "$root/packages/core" --source "$work/Publication.chibi" \
    --collection "graph=$root/tools/graph" --entry PublicationChecks --target "$target" --llvm "$llvm" \
    --level 0 --output "$work/check"
"$work/check" "$work/files"
printf '%s\n' 'Wasm publication restores old outputs and preserves failed-rollback recovery bytes'
