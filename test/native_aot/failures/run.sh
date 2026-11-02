#!/bin/sh
# Native failure behavior, optionally compared with the retained VM runner
# during migration. Expected outcomes stay explicit when that oracle retires.
set -eu
stage=${1:?native compiler required}
llvm=${2:?LLVM directory required}
reference=${3:-}
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$repo/test/native_aot/failures/fixtures" "$work/project"
for level in 0 2; do
    set +e
    CHIBI_CORE="$repo/packages/core" "$stage" test "$work/project" --llvm "$llvm" -O"$level" > "$work/native.log" 2>&1
    native_status=$?
    set -e
    [ "$native_status" -eq 1 ] || { cat "$work/native.log"; exit 1; }
    awk '/^(PASS|FAIL) facts[.]/ { print }' "$work/native.log" | LC_ALL=C sort > "$work/native.results"
    diff -u "$repo/test/native_aot/failures/expected.txt" "$work/native.results"
    awk '/9 tests, 4 failed/ { found=1 } END { exit !found }' "$work/native.log"
    awk '/expected IndexOutOfRange, but it failed with AssertionFailed/ { found=1 } END { exit !found }' "$work/native.log"
    awk '/expected AssertionFailed, but the test returned/ { found=1 } END { exit !found }' "$work/native.log"
done
if [ -n "$reference" ]; then
    set +e
    "$reference" --project-root "$work/project" --test > "$work/reference.log" 2>&1
    reference_status=$?
    set -e
    [ "$reference_status" -eq 1 ] || { cat "$work/reference.log"; exit 1; }
    awk '/^(PASS|FAIL) facts[.]/ { print }' "$work/reference.log" | LC_ALL=C sort > "$work/reference.results"
    diff -u "$work/reference.results" "$work/native.results"
fi
set +e
CHIBI_CORE="$repo/packages/core" "$stage" run --llvm "$llvm" -O2 "$repo/test/native_aot/failures/programs/Outside.chibi" > "$work/outside.log" 2>&1
outside_status=$?
set -e
[ "$outside_status" -eq 134 ] || { cat "$work/outside.log"; exit 1; }
awk '/ExpectFailure needs a running test and a failure kind/ { found=1 } END { exit !found }' "$work/outside.log"
printf '%s\n' 'native failure outcomes passed'
