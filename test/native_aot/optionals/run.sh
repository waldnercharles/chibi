#!/bin/sh
set -eu
stage=${1:?native compiler required}
llvm=${2:?LLVM directory required}
reference=${3:-}
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$repo/test/native_aot/optionals/fixtures" "$work/project"
cp -R "$repo/test/native_aot/optionals/rejected" "$work/rejected"
for level in 0 2; do
    CHIBI_CORE="$repo/packages/core" "$stage" test "$work/project" --llvm "$llvm" -O"$level" > "$work/native.log" 2>&1 || { cat "$work/native.log"; exit 1; }
    awk '/^(PASS|FAIL) (body|facts)[.]/ { print }' "$work/native.log" | LC_ALL=C sort > "$work/native.results"
    diff -u "$repo/test/native_aot/optionals/expected.txt" "$work/native.results"
done
if [ -n "$reference" ]; then
    "$reference" --project-root "$work/project" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    awk '/^(PASS|FAIL) (body|facts)[.]/ { print }' "$work/reference.log" | LC_ALL=C sort > "$work/reference.results"
    diff -u "$work/reference.results" "$work/native.results"
fi
set +e
CHIBI_CORE="$repo/packages/core" "$stage" run --llvm "$llvm" -O2 "$work/rejected/Main.chibi" > "$work/rejected.log" 2>&1
rejected_status=$?
set -e
[ "$rejected_status" -eq 1 ] || { cat "$work/rejected.log"; exit 1; }
awk '/Optional component requires a presence guard before member access/ { found=1 } END { exit !found }' "$work/rejected.log"
printf '%s\n' 'native optional values passed'
