#!/bin/sh
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd -P)
compiler=${1:?native compiler}
probe=${2:?native maps probe}
vm=${3:?ordinary build host}
work=$root/.chibi/map-effects/receiver-proof
llvm=/tmp/chibi-llvm22
if test "${4:-both}" = both; then
    "$vm" --compiler "$root/tools/compiler" --compiler-core "$root/packages/core" \
        --root "$root" --source "$root/test/native_aot/map_programs/Receivers.chibi" \
        --core "$root/packages/core" --entry Main --timeout-ms 120000
fi
mkdir -p "$work/subject"
cp "$root/test/native_aot/map_programs/Receivers.chibi" "$work/subject/Receivers.chibi"
if built=$("$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --root "$work/subject" --entry Main --llvm "$llvm" --target aarch64-apple-darwin --level 0 --jobs 1 2>&1); then
    :
else
    printf '%s\n' "$built" >&2
    exit 1
fi
program=${built#native: }
program=${program% is up to date}
cp "${program%/program}/source.ll" "$work/source.ll"
"$probe" "$work" "$llvm" "$root/test/native_aot/maps/Runtime.ll" source
