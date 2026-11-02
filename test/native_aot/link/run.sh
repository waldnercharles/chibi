#!/bin/sh
# Runs the link behavior checks with the native compiler given as $1. Stage
# their compiler-package imports as collections beside the isolated test root.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:-${CHIBI_LLVM_BIN:?pass the LLVM tools directory}}
export CHIBI_LLVM_BIN=$llvm
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-link.XXXXXX")
trap 'rm -rf "$work"' EXIT
mkdir "$work/native_link" "$work/native_link_host"
cp "$root"/tools/compiler/native_link/*.chibi "$work/native_link/"
sed 's#"project:native_link"#"native_link:."#' "$root/tools/compiler/native_link_host/Host.chibi" >"$work/native_link_host/Host.chibi"
for test in Objects Smoke; do
    "$compiler" run --root "$root" --llvm "$llvm" \
        --collection native_link="$work/native_link" --collection native_link_host="$work/native_link_host" \
        --core "$root/packages/core" --entry Run "$root/test/native_aot/link/$test.chibi"
done
