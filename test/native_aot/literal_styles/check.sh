#!/bin/sh
# Actual grouped source literals exercise the parser and numeric compiler paths.
set -eu
compiler=${1:?usage: check.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: check.sh NATIVE_COMPILER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
export CHIBI_CORE="$root/packages/core"
for level in 0 2; do
    "$compiler" test "$root/test/native_aot/literal_styles" --llvm "$llvm" "-O$level"
done
