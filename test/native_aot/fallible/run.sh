#!/bin/sh
# Exercise original authored error-result diagnostics through the native CLI.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
samples=$root/test/compiler/samples/fallible
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-fallible.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
negative=0
for source in "$samples"/*/facts/Facts.chibi; do
    if ! grep -q '^// error:' "$source"; then continue; fi
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" --root "$samples" \
        "$(dirname "$source")" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
    grep -q '^PASS .*compile_error$' "$work/case.log"
    negative=$((negative + 1))
done
# These unchanged positive controls cover returning, forwarding, branch and
# loop handling, and the absence of an outcome on a constant-short-circuit path.
for name in fallible_complete_outcomes_can_be_returned \
            fallible_errors_can_be_forwarded_to_helpers \
            fallible_every_branch_checks \
            fallible_loop_checks_each_produced_error \
            fallible_constant_short_circuit_does_not_produce_outcome \
            fallible_imported_source_error_is_checked \
            fallible_ordinary_optional_remains_ordinary; do
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" --root "$samples" \
        "$samples/$name/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
    grep -q '^PASS .*Calls$' "$work/case.log"
done
test "$negative" -gt 0
printf 'PASS native fallible ownership: %s original diagnostics and seven positive controls\n' "$negative"
