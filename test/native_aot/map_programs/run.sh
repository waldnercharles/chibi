#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=${3:-$root/.chibi/native-map-programs}
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native map programs: unsupported test host' >&2; exit 2 ;;
esac
for name in Values Strings; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/map_programs/$name.chibi" --entry Main \
        --output "$work/$name" --llvm "$llvm" --target "$triple" --jobs 2
    "$work/$name"
done
if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/map_programs/Compound.chibi" --entry Main \
    --output "$work/Compound" --llvm "$llvm" --target "$triple" --jobs 2 > "$work/compound.log" 2>&1; then
    echo 'native map programs: compound assignment was accepted' >&2
    exit 1
fi
grep -F 'native compound map assignment is not supported yet' "$work/compound.log" >/dev/null
echo 'native map programs passed'
