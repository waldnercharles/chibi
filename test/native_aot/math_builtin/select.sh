#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native scalar selection: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-select.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/math_builtin/Select.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$target" --level 0 --jobs 2
"$work/probe" "$llvm" "$work"
