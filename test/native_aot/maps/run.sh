#!/bin/sh
# Usage: run.sh NATIVE_COMPILER LLVM_BIN [WORK]
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=${3:-$root/.chibi/native-maps}
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native maps: unsupported test host' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/maps/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --jobs 2
for mode in positive tombstone-key tombstone-value facts readonly; do
    "$work/probe" "$work" "$llvm" "$root/test/native_aot/maps/Runtime.ll" "$mode"
done
sh "$root/test/native_aot/map_programs/run.sh" "$compiler" "$llvm" "$work/programs"
