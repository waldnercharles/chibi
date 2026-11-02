#!/bin/sh
# Real native root builds, cache reuse, input invalidation and failed publication.
# Usage: cache.sh DRIVER LLVM_BIN (Linux x86-64 or macOS arm64)
set -eu
driver=${1:?usage: cache.sh DRIVER LLVM_BIN}
llvm=${2:?usage: cache.sh DRIVER LLVM_BIN}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-cache.XXXXXX")
trap 'rm -rf "$work"' EXIT
case $(uname -s)-$(uname -m) in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native cache: unsupported test host' >&2; exit 2 ;;
esac
project=$work/package\ with\ spaces
tools=$work/LLVM\ with\ spaces
mkdir -p "$project/facts" "$tools"
for tool in opt llc "$linker"; do
    cat >"$tools/$tool" <<'TOOL'
#!/bin/sh
folder=${0%/*}
if [ -f "$folder/forbid" ]; then
    echo 'cache test: LLVM ran on a cache hit' >&2
    exit 93
fi
printf '%s\n' "$0" >>"$folder/calls"
exec "$CHIBI_CACHE_REAL/${0##*/}" "$@"
TOOL
    chmod +x "$tools/$tool"
done
export CHIBI_CACHE_REAL=$llvm
cache_level=0
fail() { printf 'native cache: %s\n' "$*" >&2; exit 1; }
build() {
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --root "$project" --llvm "$tools" --target "$triple" --level "$cache_level" --jobs 2 "$@"
}
path() { sed -n 's/^native: \(.*\) is up to date$/\1/p;s/^native: \(.*\/program\)$/\1/p' "$1"; }
run42() {
    status=0
    "$1" || status=$?
    [ "$status" = 42 ] || fail "program exited $status, expected 42"
}
cat >"$project/Main.chibi" <<'SOURCE'
package main
import facts "project:facts"
Main :: () -> int { return Sibling() + facts.Value(); }
SOURCE
cat >"$project/Before.chibi" <<'SOURCE'
package main
Sibling :: () -> int { return 20; }
SOURCE
cat >"$project/facts/Facts.chibi" <<'SOURCE'
package facts
pub Value :: () -> int { return 22; }
SOURCE
build >"$work/first.log" 2>&1 || { cat "$work/first.log" >&2; fail 'first build'; }
program=$(path "$work/first.log")
[ -n "$program" ] || fail 'no printed program'
run42 "$program"
output=${program%/*}
grep -F 'linkage dynamic ' "$output/receipt.txt" >/dev/null || fail 'missing dynamic receipt'
grep -F 'compiler-sha256 ' "$output/receipt.txt" >/dev/null || fail 'missing compiler identity'
[ -f "$output/source.ll" ] || fail 'missing retained module'
calls=$(wc -l <"$tools/calls")
touch "$tools/forbid"
build >"$work/reused.log" 2>&1 || { cat "$work/reused.log" >&2; fail 'cache reuse'; }
grep -F 'is up to date' "$work/reused.log" >/dev/null || fail 'no reuse report'
[ "$(wc -l <"$tools/calls")" = "$calls" ] || fail 'LLVM ran again'
rm "$tools/forbid"
cache_level=2
build >"$work/optimized.log" 2>&1 || { cat "$work/optimized.log" >&2; fail 'O2 build'; }
optimized=$(path "$work/optimized.log")
[ "$optimized" != "$program" ] || fail 'optimization mode reused O0 output'
run42 "$optimized"
cache_level=0
printf 'native cache: package entry, sibling/import closure, modes and warm reuse passed\n'

# Imported contents and the optimization mode each invalidate independently.
sed 's/return 22/return 23/' "$project/facts/Facts.chibi" >"$work/changed"
mv "$work/changed" "$project/facts/Facts.chibi"
build >"$work/changed.log" 2>&1 || { cat "$work/changed.log" >&2; fail 'import change'; }
changed=$(path "$work/changed.log")
[ "$changed" != "$program" ] && [ -f "$program" ] || fail 'changed input replaced prior output'
status=0
"$changed" || status=$?
[ "$status" = 43 ] || fail 'changed import was not used'
cp "$output/receipt.txt" "$work/receipt"
printf 'corrupt\n' >"$output/receipt.txt"
sed 's/return 23/return 22/' "$project/facts/Facts.chibi" >"$work/original"
mv "$work/original" "$project/facts/Facts.chibi"
if build >"$work/corrupt.log" 2>&1; then fail 'corrupt receipt accepted'; fi
grep -F 'no matching receipt' "$work/corrupt.log" >/dev/null || fail 'corrupt receipt diagnostic'
cp "$work/receipt" "$output/receipt.txt"
run42 "$program"

# A reserved work directory survives interruption and blocks re-entry.
mv "$output" "$work/preserved"
mkdir "$output.work"
if build >"$work/interrupted.log" 2>&1; then fail 'interrupted build ignored'; fi
grep -F 'interrupted build left' "$work/interrupted.log" >/dev/null || fail 'interrupted build diagnostic'
rmdir "$output.work"
mv "$work/preserved" "$output"

# Rejection and LLVM failure leave all published programs intact and no private work.
printf 'this is invalid Chibi\n' >"$project/Broken.chibi"
if build >"$work/rejected.log" 2>&1; then fail 'invalid source accepted'; fi
rm "$project/Broken.chibi"
run42 "$program"
sed 's/return 22/return 24/' "$project/facts/Facts.chibi" >"$work/failing"
mv "$work/failing" "$project/facts/Facts.chibi"
touch "$tools/forbid"
if build >"$work/failed.log" 2>&1; then fail 'LLVM failure accepted'; fi
rm "$tools/forbid"
[ -z "$(find "$project/build/native" -name '*.work' -o -name '*.partial')" ] || fail 'failed build left private output'
run42 "$program"
[ -f "$changed" ] || fail 'failure removed the other published output'
printf 'native cache: invalidation, receipt corruption, interruption and prior-output preservation passed\n'

# A symlinked foreign archive's target bytes are part of the identity.
rm "$project/Before.chibi"
cat >"$project/Main.chibi" <<'SOURCE'
package main
foreign /* cache dependency */
import probe "libprobe.a"
foreign probe { probe_value :: () -> i32 }
Main :: () -> int { return int(probe_value()); }
SOURCE
ln -s libactual.a "$project/libprobe.a"
for value in 42 43; do
    cat >"$project/Probe.ll" <<LLVM
target triple = "$triple"
define i32 @probe_value() { ret i32 $value }
LLVM
    "$llvm/llc" -filetype=obj -relocation-model=pic "$project/Probe.ll" -o "$project/probe.o"
    /usr/bin/ar rcs "$project/libactual.a" "$project/probe.o"
    build >"$work/archive-$value.log" 2>&1 || { cat "$work/archive-$value.log" >&2; fail 'foreign archive build'; }
    archive_program=$(path "$work/archive-$value.log")
    status=0
    "$archive_program" || status=$?
    [ "$status" = "$value" ] || fail 'foreign archive content was not used'
done
[ "$(path "$work/archive-42.log")" != "$(path "$work/archive-43.log")" ] || fail 'changed foreign archive reused stale program'
printf 'native cache: symlinked foreign archive identity passed\n'

cat >"$project/Only.chibi" <<'SOURCE'
package main
Main :: () -> int { return 42; }
SOURCE
printf 'invalid sibling source\n' >"$project/InvalidSibling.chibi"
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --root "$project/Only.chibi" --llvm "$tools" --target "$triple" --level 0 --jobs 2 \
    >"$work/file.log" 2>&1 || { cat "$work/file.log" >&2; fail 'explicit root file'; }
run42 "$(path "$work/file.log")"
printf 'native cache: explicit root file excludes unrelated siblings\n'
