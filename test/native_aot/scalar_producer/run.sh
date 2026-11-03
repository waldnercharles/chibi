#!/bin/sh
set -eu
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native scalar producer: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-scalar-producer.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Join these test declarations with the driver's exact private target/catalog
# source. No metadata export, separate catalog snapshot or compiler-host is involved.
cat "$root/test/native_aot/scalar_producer/Producer.chibi" > "$work/Producer.chibi"
awk 'copy { print } /^TargetFacts ::/ { copy = 1; print }' "$root/tools/compiler/native_driver/Catalog.chibi" >> "$work/Producer.chibi"
for source in Positions Matrices Shapes Packages Libraries Rejections Collections TextArguments CallArguments ArrayEdits Synchronous Corpus; do
    sed '/^package /d; /^import /d' "$root/test/native_aot/scalar_producer/$source.chibi" >> "$work/Producer.chibi"
done
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/Producer.chibi" --entry Main --collection "fixtures=$root/test/scalar_facts" \
    --output "$work/producer" --llvm "$llvm" --target "$target" --level 2 --jobs 2
"$work/producer"
