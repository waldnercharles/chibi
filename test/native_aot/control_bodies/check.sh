#!/bin/sh
# Single-statement control bodies must preserve execution and lexical scope.
set -eu
compiler=${1:?usage: check.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: check.sh NATIVE_COMPILER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
export CHIBI_CORE="$root/packages/core"
for level in 0 2; do
    "$compiler" test "$root/test/native_aot/control_bodies" --llvm "$llvm" "-O$level"
done
