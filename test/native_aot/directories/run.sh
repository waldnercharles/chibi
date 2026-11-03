#!/bin/sh
set -eu
compiler=$1
llvm=$2
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-directory-runner.XXXXXX")
trap 'rm -rf "$work"' EXIT
case "$(uname -s)" in Darwin) triple=aarch64-apple-darwin ;; *) triple=x86_64-unknown-linux-gnu ;; esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/directories/Run.chibi" --entry Run --collection "graph=$root/tools/graph" \
    --output "$work/runner" --llvm "$llvm" --target "$triple" --jobs 2
(cd "$root" && "$work/runner" "$compiler" "$llvm")
