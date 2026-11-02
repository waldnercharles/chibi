#!/bin/sh
set -eu
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
root=$(cd "$(dirname "$0")/../.." && pwd -P)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-cli.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
case "$(uname -s)" in
    Darwin) target=aarch64-apple-darwin ;;
    Linux) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native CLI regression requires Linux or macOS' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Main.chibi" --entry Main --output "$scratch/tests" \
    --llvm "$llvm" --target "$target"
mkdir "$scratch/programs"
CHIBI_CORE=$root/packages/core "$scratch/tests" "$compiler" "$llvm" "$scratch/programs"
