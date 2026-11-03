#!/bin/sh
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
work=${3:?owned workspace}/text-cell-images
mode=${4:-text}
case "$mode" in
    text) writer=Writer ;;
    optional) writer=Optional; work=$work-optional ;;
    record) writer=Record; work=$work-record ;;
    *) echo 'native string cells: unsupported mode' >&2; exit 2 ;;
esac
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'native string cells: unsupported test host' >&2; exit 2 ;;
esac
mkdir -p "$work/source"
cp "$root/test/native_aot/managed_pointers/text_cells/$writer.chibi" "$work/source/Main.chibi"
# Cached shared builds retain the exact source-lowered family module.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --root "$work/source" --shared --llvm "$llvm" --target "$triple" --jobs 1 > "$work/source-build.log" 2>&1
library=$(sed -nE 's/^native: (.*\/generation\.so)( is up to date)?$/\1/p' "$work/source-build.log")
[ -n "$library" ] || { cat "$work/source-build.log" >&2; exit 1; }
module=${library%/*}/source.ll
tag=$(sed -nE 's/^@chibi_managed\.layout\.text(-?[0-9]+) =.*/\1/p' "$module")
if [ "$mode" != text ]; then
    tag=$(sed -nE 's/^  %whole.tag = icmp eq i64 %tag, (-?[0-9]+)$/\1/p' "$module")
fi
[ -n "$tag" ] || { echo 'native string cells: source has no typed text layout' >&2; exit 1; }
for name in TextCellImages TextCellImagesHost; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/managed_pointers/$name.chibi" --entry Main \
        --output "$work/$name" --llvm "$llvm" --target "$triple" --jobs 1
done
if [ "$mode" = record ]; then
    pointer_tag=$(sed -nE 's/^  %tag0 = icmp eq i64 %tag, (-?[0-9]+)$/\1/p' "$module")
    text_tag=$(sed -nE 's/^  %tag1 = icmp eq i64 %tag, (-?[0-9]+)$/\1/p' "$module")
    "$work/TextCellImages" "$work" "$llvm" "$module" "$tag" record "$pointer_tag" "$text_tag"
elif [ "$mode" = optional ]; then
    "$work/TextCellImages" "$work" "$llvm" "$module" "$tag" optional
else
    "$work/TextCellImages" "$work" "$llvm" "$module" "$tag"
fi
for level in 0 2; do
    "$work/TextCellImagesHost" "$work/text-cell-old-O$level.so" "$work/text-cell-writer-O$level.so"
    status=0
    "$work/TextCellImagesHost" "$work/text-cell-old-O$level.so" "$work/text-cell-broken-O$level.so" > "$work/broken-O$level.log" 2>&1 || status=$?
    [ "$status" -eq 33 ] || {
        cat "$work/broken-O$level.log" >&2; echo "native string cells missing import O$level: status$status" >&2; exit 1;
    }
    echo "PASS native $mode string cells O$level source call, borrowed-byte clobber, writer unload and old image lease"
done
