#!/bin/sh
# Run the public CLI behavior suite and its POSIX process/terminal cases.
set -eu
compiler=${1:?usage: survivors.sh NATIVE_COMPILER LLVM_BIN SDK_CLI}
llvm=${2:?usage: survivors.sh NATIVE_COMPILER LLVM_BIN SDK_CLI}
launcher=${3:?usage: survivors.sh NATIVE_COMPILER LLVM_BIN SDK_CLI}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
export CHIBI_LLVM_BIN=$llvm
cd "$root"
"$compiler" run --llvm "$llvm" --root "$root" --core "$root/packages/core" \
    --entry Run "$root/test/graph/ChibiCliTests.chibi" -- "$launcher"
python3 "$root/test/native_cli/foreground.py" "$compiler" "$llvm" "$launcher"
