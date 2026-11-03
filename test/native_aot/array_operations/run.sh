#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
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
