#!/bin/sh
set -eu
compiler=${1:?native compiler}
llvm=${2:?LLVM directory}
work=${3:?owned workspace}/text-import
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
fixtures=$root/test/native_aot/managed_pointers
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu ;;
    *) echo 'boundary Text imports: unsupported test host' >&2; exit 2 ;;
esac
mkdir -p "$work"
# Exact focused inventory only. This runner never invokes run.sh or a task suite.
for name in SourceProbe BoundaryTextImages BoundaryTextImagesHost; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --package . --source "$fixtures/$name.chibi" --entry Main --output "$work/$name" \
        --llvm "$llvm" --target "$triple" --jobs 1
done
for shape in Labels Leaf; do
    directory=$work/$shape
    mkdir -p "$directory/writer"
    source=$fixtures/text_import/$shape.chibi
    # The full source is the old constructor image and the semantic/CoW proof.
    # Its prefix has Rows/Write/Replace but no ManagedNew, Main or constructor.
    sed '/^\/\/ The image runner extracts the prefix above this marker\./,$d' "$source" > "$directory/writer/Main.chibi"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main --emit-llvm --target "$triple" --output "$directory/$shape.ll"
    "$work/BoundaryTextImages" "$directory" "$llvm" "$shape" "$source" \
        "$directory/writer/Main.chibi" "$fixtures/BoundaryTextOld.ll" "$fixtures/BoundaryTextAudit.ll" \
        "$fixtures/BoundaryTextAllocation.ll" "$root/packages/core" "$fixtures/BoundaryTextLabels.ll"
    boundaries='view replace constructor scoped_set scoped_append scoped_extend'
    if [ "$shape" = Labels ]; then
        boundaries="$boundaries scoped_field scoped_field_append scoped_field_extend scoped_text_set scoped_rhs aliases"
    fi
    for level in 0 2; do
        for boundary in $boundaries; do
            "$work/BoundaryTextImagesHost" "$directory/boundary-old-O$level.so" \
                "$directory/boundary-writer-O$level.so" "$shape" "$boundary"
            echo "PASS boundary Text $shape $boundary O$level all owners before writer unload"
        done
        status=0
        "$work/BoundaryTextImagesHost" "$directory/boundary-old-O$level.so" \
            "$directory/boundary-broken-O$level.so" "$shape" view > "$directory/broken-O$level.log" 2>&1 || status=$?
        [ "$status" -eq 33 ] && grep -Fxq 'boundary Text owner missing before unload' "$directory/broken-O$level.log" || {
            cat "$directory/broken-O$level.log" >&2
            echo "boundary Text $shape missing-import O$level: status $status" >&2; exit 1;
        }
        echo "PASS boundary Text $shape one-site bypass O$level fails safely before unload"
    done
    for callbacks in normal callbacks; do
        audit_work=$directory/$callbacks
        mkdir -p "$audit_work"
        if [ "$callbacks" = callbacks ]; then
            "$work/SourceProbe" --callbacks "$audit_work" "$llvm" "$fixtures/SourceAudit.ll" "$directory/$shape.ll"
        else
            "$work/SourceProbe" "$audit_work" "$llvm" "$fixtures/SourceAudit.ll" "$directory/$shape.ll"
        fi
    done
done
# Ordinary counted copies remain O(1): 4096 copies of two rows sharing a
# 1024-element Text array must not allocate per copy or per nested element.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$fixtures/text_import/Copies.chibi" --entry Main --emit-llvm \
    --target "$triple" --output "$work/Copies.ll"
sed 's/%bounded = icmp ule i64 %peak, 2048/%bounded = icmp ule i64 %total, 64/' \
    "$fixtures/SourceAudit.ll" > "$work/copy-audit.ll"
grep -Fq '%bounded = icmp ule i64 %total, 64' "$work/copy-audit.ll"
mkdir -p "$work/copies"
"$work/SourceProbe" "$work/copies" "$llvm" "$work/copy-audit.ll" "$work/Copies.ll"
echo 'PASS boundary Text ordinary counted copies stay within 64 allocations'

# Bound importer work before excessive allocations occur. DAG has 15 distinct
# backed levels but 32768 unfolded leaves; Deep has 15 one-child levels.
# Leaf plus 15 layers is the unchanged maximum admitted metadata depth.
# Both are finite rootless source types, with no retained/map admission changes.
python3 - "$fixtures/SourceAudit.ll" "$work/graph-audit.ll" <<'PY_AUDIT'
from pathlib import Path
import sys
source = Path(sys.argv[1]).read_text()
needle = "  store i64 %allocated, ptr @managed_source.total\n"
assert source.count(needle) == 1
source = source.replace(needle, needle + """  %within.budget = icmp ule i64 %allocated, 512
  br i1 %within.budget, label %allocate, label %excessive
excessive:
  call void @llvm.trap()
  unreachable
allocate:
""")
source = source.replace("%bounded = icmp ule i64 %peak, 2048", "%bounded = icmp ule i64 %total, 512")
Path(sys.argv[2]).write_text(source)
PY_AUDIT
for entry in DAG Deep; do
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$fixtures/text_import/Graphs.chibi" --entry "$entry" --emit-llvm \
        --target "$triple" --output "$work/$entry.ll"
    mkdir -p "$work/$entry"
    "$work/SourceProbe" "$work/$entry" "$llvm" "$work/graph-audit.ll" "$work/$entry.ll"
    echo "PASS boundary Text $entry bounded to 512 allocations"
done
