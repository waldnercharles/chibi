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
cp -R "$repo/test/native_aot/optionals/rejected_union" "$work/rejected_union"
cp -R "$repo/test/native_aot/optionals/rejected_union_is" "$work/rejected_union_is"
for level in 0 2; do
    CHIBI_CORE="$repo/packages/core" "$stage" test "$work/project" --llvm "$llvm" -O"$level" > "$work/native.log" 2>&1 || { cat "$work/native.log"; exit 1; }
    awk '/^(PASS|FAIL) (body|facts|unions|named)[.]/ { print }' "$work/native.log" | LC_ALL=C sort > "$work/native.results"
    diff -u "$repo/test/native_aot/optionals/expected.txt" "$work/native.results"
done
if [ -n "$reference" ]; then
    "$reference" --project-root "$work/project" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    awk '/^(PASS|FAIL) (body|facts|unions|named)[.]/ { print }' "$work/reference.log" | LC_ALL=C sort > "$work/reference.results"
    diff -u "$work/reference.results" "$work/native.results"
fi
set +e
CHIBI_CORE="$repo/packages/core" "$stage" run --llvm "$llvm" -O2 "$work/rejected/Main.chibi" > "$work/rejected.log" 2>&1
rejected_status=$?
set -e
[ "$rejected_status" -eq 1 ] || { cat "$work/rejected.log"; exit 1; }
awk '/Optional component requires a presence guard before member access/ { found=1 } END { exit !found }' "$work/rejected.log"
set +e
CHIBI_CORE="$repo/packages/core" "$stage" run --llvm "$llvm" -O2 "$work/rejected_union/Main.chibi" > "$work/rejected_union.log" 2>&1
union_status=$?
set -e
[ "$union_status" -eq 1 ] || { cat "$work/rejected_union.log"; exit 1; }
awk "/Unknown variant 'Empty'/ { found=1 } END { exit !found }" "$work/rejected_union.log"
set +e
CHIBI_CORE="$repo/packages/core" "$stage" run --llvm "$llvm" -O2 "$work/rejected_union_is/Main.chibi" > "$work/rejected_union_is.log" 2>&1
union_is_status=$?
set -e
[ "$union_is_status" -eq 1 ] || { cat "$work/rejected_union_is.log"; exit 1; }
awk "/Unknown union variant 'Empty'/ { found=1 } END { exit !found }" "$work/rejected_union_is.log"
for level in 0 2; do
    for sample in fallible/fallible_guarded_tagged_union_can_change_alternatives types/optional_errors_beside_named_struct_results; do
        suite=${sample%%/*}
        CHIBI_CORE="$repo/packages/core" "$stage" test --llvm "$llvm" -O"$level" --root "$repo/test/compiler/samples/$suite" \
            "$repo/test/compiler/samples/$sample/facts" > "$work/authored.log" 2>&1 || { cat "$work/authored.log"; exit 1; }
        awk '/^PASS project[.]Calls$/ { found=1 } END { exit !found }' "$work/authored.log"
    done
done
sh "$repo/test/native_aot/managed_values/run.sh" "$stage" "$llvm" '' "$repo/test/native_aot/optionals/fixtures/unions/Unions.chibi"
sh "$repo/test/native_aot/managed_values/run.sh" "$stage" "$llvm" '' "$repo/test/native_aot/optionals/fixtures/facts/Ownership.chibi"
printf '%s\n' 'native optional values passed'
