#!/bin/sh
set -eu
compiler=${1:?pass the native compiler}
work=${2:?pass the owned output directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
mkdir -p "$work"
for count in 100 1000; do
    awk -v count="$count" 'BEGIN {
        print "package managed_root_growth\nMain :: () -> int {"
        for (cursor=0; cursor<count; cursor++) print " values" cursor ": ^int[] = [];"
        print " return 0;\n}"
    }' > "$work/Growth$count.chibi"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/Growth$count.chibi" --entry Main \
        --output "$work/Growth$count.ll" --emit-llvm --target x86_64-unknown-linux-gnu
done
small=$(wc -c < "$work/Growth100.ll")
large=$(wc -c < "$work/Growth1000.ll")
# Ten times as many owners must not create a separate full root list for
# every statement. Leave room for digit widths and fixed runtime helpers.
[ "$large" -le "$((small * 15))" ] || {
    echo "managed root emission grew from $small to $large bytes" >&2
    exit 1
}
echo "managed root emission: 100 owners $small bytes; 1000 owners $large bytes"
