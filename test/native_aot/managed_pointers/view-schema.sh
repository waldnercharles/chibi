#!/bin/sh
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
work=${3:?owned workspace}/view-schema
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native view schema: unsupported test host' >&2; exit 2 ;;
esac
mkdir -p "$work"
for name in Original Reindexed Changed; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/schema/$name.chibi" --entry Main \
        --output "$work/$name.ll" --emit-llvm --target "$triple"
done
# The source graph includes an owned array and a nested union. Changing its
# int leaf to f32 keeps its byte size while changing the semantic schema.
tag() {
    sed -nE 's/^define internal .*@chibi_view\.[0-9]+\.(-?[0-9]+)\.make\..*/\1/p' "$1"
}
original=$(tag "$work/Original.ll")
reindexed=$(tag "$work/Reindexed.ll")
changed=$(tag "$work/Changed.ll")
[ -n "$original" ] && [ "$original" = "$reindexed" ] && [ "$original" != "$changed" ] || {
    echo 'native view schema: incompatible source fingerprints' >&2; exit 1;
}
for name in Probe Host; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/ViewSchema$name.chibi" --entry Main \
        --output "$work/$name" --llvm "$llvm" --target "$triple"
done
"$work/Probe" "$work" "$llvm" "$original" "$reindexed" "$changed"
for level in 0 2; do
    "$work/Host" "$work/schema-old-O$level.so" "$work/schema-compatible-O$level.so"
    status=0
    "$work/Host" "$work/schema-old-O$level.so" "$work/schema-changed-O$level.so" > "$work/changed-O$level.log" 2>&1 || status=$?
    [ "$status" -eq 134 ] && grep -Fq 'native failure: another failure' "$work/changed-O$level.log" || {
        cat "$work/changed-O$level.log" >&2; echo "native changed view schema O$level: status$status" >&2; exit 1;
    }
    echo "PASS native view schema O$level compatible reindex, changed schema, image leases"
done
