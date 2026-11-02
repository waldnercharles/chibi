#!/bin/sh
# Exercise original authored error-result diagnostics through the native CLI.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
case $(uname -s)-$(uname -m) in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native fallible regression requires Linux x86-64 or macOS arm64' >&2; exit 2 ;;
esac
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
# A pointer-shaped named result remains rejected at the ordinary signature
# boundary; copied-cell provenance does not admit unsupported header shapes.
CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" \
    --root "$root/test/compiler/samples/collections" \
    "$root/test/compiler/samples/collections/managed_pointer_named_result/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
grep -q '^PASS .*compile_error$' "$work/case.log"
test "$negative" -gt 0
printf 'PASS native fallible ownership: %s original diagnostics and seven positive controls\n' "$negative"

# Presence and a present bool payload are distinct. These original samples
# cover false/true/absent values, nested guards, !, &&/|| and explicit nil tests.
booleans='ordinary_optional_boolean_member_and_operand_reads_the_prese
ordinary_optional_boolean_member_nil_comparison_still_tests_
ordinary_optional_boolean_member_or_operand_reads_the_presen
ordinary_optional_negated_boolean_member_reads_the_present_p
ordinary_optional_nested_boolean_guard_reads_the_valid_paylo
ordinary_optional_nested_boolean_member_keeps_its_payload_on
ordinary_optional_unnamed_boolean_keeps_its_payload_on_false'
for level in 0 2; do
    for name in $booleans; do
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" --root "$samples" \
            "$samples/$name/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
        grep -q '^PASS .*Calls$' "$work/case.log"
    done
    for name in fallible_tuple_aliases_preserve_checking \
                fallible_shadowed_member_accepts_its_own_presence_guard \
                fallible_sibling_assignments_permit_actual_error_handling; do
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" --root "$samples" \
            "$samples/$name/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
        grep -q '^PASS .*Calls$' "$work/case.log"
    done
    for name in optional_locals_become_absent_and_present_again \
                optional_locals_start_absent_and_become_present \
                optional_parameters_take_optionals_and_nil \
                optional_results_wrap_values_and_return_nil; do
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
            --root "$root/test/compiler/samples/imports" \
            "$root/test/compiler/samples/imports/$name/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
        grep -q '^PASS .*Calls$' "$work/case.log"
    done
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/types" \
        "$root/test/compiler/samples/types/typed_optional_locals_after_descriptor_growth/facts" > "$work/case.log" 2>&1 || { cat "$work/case.log"; exit 1; }
    grep -q '^PASS .*Calls$' "$work/case.log"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/fallible/primitive_optionals/Main.chibi" --entry Main --output "$work/primitive-optionals" \
        --llvm "$llvm" --target "$target" --level "$level"
    "$work/primitive-optionals"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/fallible/result_copies/Main.chibi" --entry Main --output "$work/result-copies" \
        --llvm "$llvm" --target "$target" --level "$level"
    "$work/result-copies"
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$root/test/native_aot/fallible/Parenthesized.chibi" --entry Main --output "$work/parenthesized" \
        --llvm "$llvm" --target "$target" --level "$level"
    "$work/parenthesized"
    printf 'PASS native optional bool and result-copy values at O%s\n' "$level"
done
if [ -n "$reference" ]; then
    for name in $booleans; do
        "$reference" --project-root "$samples/$name/facts" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
        grep -F 'PASS' "$work/reference.log" > /dev/null
    done
    for name in fallible_tuple_aliases_preserve_checking \
                fallible_shadowed_member_accepts_its_own_presence_guard \
                fallible_sibling_assignments_permit_actual_error_handling; do
        "$reference" --project-root "$samples/$name/facts" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
        grep -F 'PASS' "$work/reference.log" > /dev/null
    done
    for name in optional_locals_become_absent_and_present_again \
                optional_locals_start_absent_and_become_present \
                optional_parameters_take_optionals_and_nil \
                optional_results_wrap_values_and_return_nil; do
        "$reference" --project-root "$root/test/compiler/samples/imports/$name/facts" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
        grep -F 'PASS' "$work/reference.log" > /dev/null
    done
    "$reference" --project-root "$root/test/compiler/samples/types/typed_optional_locals_after_descriptor_growth/facts" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    grep -F 'PASS' "$work/reference.log" > /dev/null
    "$reference" --project-root "$root/test/native_aot/fallible" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    grep -F 'PASS' "$work/reference.log" > /dev/null
    printf 'PASS ordinary optional bool and result-copy references\n'
fi
