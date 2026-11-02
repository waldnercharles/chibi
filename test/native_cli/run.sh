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

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Processes.chibi" --entry Main --output "$scratch/processes" \
    --llvm "$llvm" --target "$target"
"$scratch/processes" "$scratch/processes" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Identities.chibi" --entry Main --output "$scratch/identities" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/identities" "$compiler" "$llvm" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Workers.chibi" --entry Main --output "$scratch/workers" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/workers" "$compiler" "$llvm" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Waiting.chibi" --entry Main --output "$scratch/waiting" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/waiting" "$compiler" "$llvm" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/AssertionMessages.chibi" --entry Main --output "$scratch/messages" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/messages" "$compiler" "$llvm" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/Generics.chibi" --entry Main --output "$scratch/generics" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/generics" "$compiler" "$llvm" "$root" "$scratch/programs"

"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_cli/ArrayArguments.chibi" --entry Main --output "$scratch/array-arguments" \
    --llvm "$llvm" --target "$target"
CHIBI_CORE=$root/packages/core "$scratch/array-arguments" "$compiler" "$llvm" "$root"
