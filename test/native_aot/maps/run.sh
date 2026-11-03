#!/bin/sh
# Usage: run.sh NATIVE_COMPILER LLVM_BIN [WORK] [ORDINARY_BUILD_HOST]
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=${3:-$root/.chibi/native-maps}
reference=${4:-}
mkdir -p "$work"
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native maps: unsupported test host' >&2; exit 2 ;;
esac
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/maps/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --jobs 2
for mode in positive tombstone-key tombstone-value facts readonly array-readonly nested-readonly; do
    "$work/probe" "$work" "$llvm" "$root/test/native_aot/maps/Runtime.ll" "$mode"
done
for kind in 1 2 3 4 5 6 7; do
    "$work/probe" "$work" "$llvm" "$root/test/native_aot/maps/Runtime.ll" "missing-address-$kind"
done
sh "$root/test/native_aot/map_programs/run.sh" "$compiler" "$llvm" "$work/programs"
# Compile authored source once, then run its emitted module through the same
# poisoned allocator and zero-outstanding-allocation check as the facts probes.
for name in Ownership Effects Receivers ManagedRecords StringEffects ArrayPayload ArrayForeign Compound ArrayBacked ArrayValues Unions Optionals ArrayForeignPlace ForeignLoans ArrayCompound ArrayPlaces NestedContainers NestedPlaces DeepPlaces; do
    project=$work/source-project/$name
    mkdir -p "$project"
    cp "$root/test/native_aot/map_programs/$name.chibi" "$project/$name.chibi"
    if source_build=$("$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --root "$project" --entry Main --llvm "$llvm" --target "$triple" --jobs 2 2>&1); then
        :
    else
        printf '%s\n' "$source_build" >&2
        exit 1
    fi
    source_program=${source_build#native: }
    source_program=${source_program% is up to date}
    cp "${source_program%/program}/source.ll" "$work/source.ll"
    "$work/probe" "$work" "$llvm" "$root/test/native_aot/maps/Runtime.ll" source
done

# Keep the existing bytecode consumer's receiver ownership in step during
# retirement. The pure source fixture needs no optional host capabilities.
if [ -n "$reference" ]; then
    "$reference" --compiler "$root/tools/compiler" --compiler-core "$root/packages/core" \
        --root "$root" --source "$root/test/native_aot/map_programs/ArrayPayload.chibi" \
        --core "$root/packages/core" --entry Main --timeout-ms 120000
fi
