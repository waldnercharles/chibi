#!/bin/sh
# Real source ownership checks at O0/O2, with poisoned frees and zero leaks.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
records_source=${4:-$root/test/native_aot/managed_values/Records.chibi}
case "$compiler" in /*) ;; *) compiler=$(pwd -P)/$compiler ;; esac
case "$llvm" in /*) ;; *) llvm=$(pwd -P)/$llvm ;; esac
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) triple=aarch64-apple-darwin; linker=ld64.lld ;;
    Linux-x86_64) triple=x86_64-unknown-linux-gnu; linker=ld.lld ;;
    *) echo 'native managed values: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-managed-values.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/llvm"
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            sed -e "s/define i32 @chibi_scalar(ptr %ctx)/define i32 @$CHIBI_VALUE_ENTRY(ptr %ctx)/" -e '/^declare i64 @chibi_value_test_allocations()$/d' -e '/^declare i64 @chibi_pattern_test_live()$/d' -e '/^declare i64 @chibi_value_test_owner_calls()$/d' "$source" > "$source.audit"
            if [ "${CHIBI_VALUE_OWNER_AUDIT:-0}" = 1 ]; then
                awk '
                    /^define internal .*@chibi_array\.[0-9]+\.(copy|drop)\(/ { counted = 1 }
                    { print }
                    counted && /^entry:/ { print "  %owner.audit = atomicrmw add ptr @chibi_value_test_owner_calls_total, i64 1 monotonic"; counted = 0 }
                ' "$source.audit" > "$source.owners"
                mv "$source.owners" "$source.audit"
            fi
            cat "$CHIBI_VALUE_AUDIT" >> "$source.audit"
            mv "$source.audit" "$source"
        fi ;;
    esac
done
exec "$CHIBI_VALUE_LLVM/opt" "$@"
WRAPPER
chmod +x "$work/llvm/opt"
ln -s "$llvm/llc" "$work/llvm/llc"
ln -s "$llvm/$linker" "$work/llvm/$linker"
export CHIBI_VALUE_LLVM=$llvm
# Managed pointer programs retain the real execution-family owner and formatter.
# The source audit checks the same poison/balance contract without replacing it.
# Its printf report belongs to its standalone probe's explicit link imports.
sed '/%printed = call /d' "$root/test/native_aot/managed_pointers/SourceAudit.ll" > "$work/conditional-audit.ll"
cat >> "$work/conditional-audit.ll" <<'LIVE'
define i64 @chibi_pattern_test_live() {
entry:
  %live = load i64, ptr @managed_source.live
  ret i64 %live
}
LIVE
cat "$root/test/native_aot/managed_values/Audit.ll" "$root/test/native_aot/managed_values/OwnerCalls.ll" > "$work/width-audit.ll"
for level in 0 2; do
    for source in Records Foreign ForeignVoid ForeignPlaces ForeignRecord ForeignPath ForeignElements ForeignElementWidth DeepArrayFields DeepArrayGrowth DeepArrayWidth RecordArrayPlaces RecordArrayWidth Backed Growth Fields Clock Hash ConditionalOptionals NestedMath MathScopes Patterns PatternLeases TextFormat TextFormatMain TextFormatStrings TextFormatFamily TextFormatRecords TextFormatOptionalRecords MapCopies NestedMapCopies DeepMapCopies; do
        input=$root/test/native_aot/managed_values/$source.chibi
        project=$root/tools/compiler
        export CHIBI_VALUE_ENTRY=chibi_value_test_entry CHIBI_VALUE_AUDIT=$root/test/native_aot/managed_values/Audit.ll CHIBI_VALUE_OWNER_AUDIT=0
        if [ "$source" = DeepArrayWidth ] || [ "$source" = RecordArrayWidth ] || [ "$source" = ForeignElementWidth ]; then export CHIBI_VALUE_OWNER_AUDIT=1 CHIBI_VALUE_AUDIT=$work/width-audit.ll; fi
        if [ "$source" = Records ]; then input=$records_source; fi
        if [ "$source" = NestedMath ]; then input=$root/test/native_aot/math_builtin/Nested.chibi; fi
        if [ "$source" = MathScopes ]; then input=$root/test/native_aot/math_builtin/Scopes.chibi; fi
        if [ "$source" = ConditionalOptionals ] || [ "$source" = Patterns ] || [ "$source" = PatternLeases ] || [ "$source" = TextFormat ] || [ "$source" = TextFormatMain ] || [ "$source" = TextFormatStrings ] || [ "$source" = TextFormatFamily ] || [ "$source" = TextFormatRecords ] || [ "$source" = TextFormatOptionalRecords ]; then
            case "$source" in
                Patterns) input=$root/test/native_aot/patterns/Main.chibi ;;
                PatternLeases) input=$root/test/native_aot/patterns/Leases.chibi ;;
                ConditionalOptionals) input=$root/test/native_aot/fallible/conditional_optionals/Main.chibi ;;
                TextFormat) input=$root/test/native_aot/text_format/Aggregates.chibi ;;
                TextFormatMain) input=$root/test/native_aot/text_format/Main.chibi ;;
                TextFormatStrings) input=$root/test/native_aot/text_format/Strings.chibi ;;
                TextFormatFamily) input=$root/test/native_aot/text_format/Family.chibi ;;
                TextFormatRecords) input=$root/test/native_aot/text_format/Records.chibi; project=$root/test/native_aot/text_format ;;
                TextFormatOptionalRecords) input=$root/test/native_aot/text_format/OptionalRecords.chibi ;;
            esac
            export CHIBI_VALUE_ENTRY=managed_source.entry CHIBI_VALUE_AUDIT=$work/conditional-audit.ll
        fi
        "$compiler" --compiler "$project" --core "$root/packages/core" \
            --source "$input" --entry Main \
            --output "$work/$source-O$level" --llvm "$work/llvm" --target "$triple" --level "$level" --jobs 1
        "$work/$source-O$level"
        printf 'PASS native managed %s O%s, allocations balanced\n' "$source" "$level"
    done
done
# Logical bracket writes check the initial place before RHS effects and the
# current place again after callbacks. These failures keep category zero.
for level in 0 2; do
    for source in Initial Late LateField; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/managed_values/deep_places/$source.chibi" --entry Main \
            --output "$work/deep-$source-O$level" --llvm "$llvm" --target "$triple" --level "$level" --jobs 1
        set +e
        "$work/deep-$source-O$level" > "$work/deep-$source.log" 2>&1
        failed=$?
        set -e
        test "$failed" -eq 134
        grep -q 'Referenced array element is unavailable' "$work/deep-$source.log"
        if grep -q 'RHS evaluated\|IndexOutOfRange' "$work/deep-$source.log"; then cat "$work/deep-$source.log"; exit 1; fi
    done
done
sh "$root/test/native_aot/managed_values/record_places.sh" "$compiler" "$llvm" "$work/record-places"
sh "$root/test/native_aot/managed_values/foreign_elements.sh" "$compiler" "$llvm" "$work/foreign-elements"
# Mutating method labels follow the ordinary parameter-order rule.
printf 'preserved\n' > "$work/deep-labels"
set +e
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_values/deep_places/RejectedLabels.chibi" --entry Main \
    --output "$work/deep-labels" --llvm "$llvm" --target "$triple" --level 0 --jobs 1 > "$work/deep-labels.log" 2>&1
failed=$?
set -e
test "$failed" -eq 1
test "$(cat "$work/deep-labels")" = preserved
grep -q "name an array update's arguments in parameter order" "$work/deep-labels.log"
# The public native lifecycle operations also run directly, independently of
# scalar lowering's ownership decisions.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_values/Probe.chibi" --entry Main \
    --output "$work/probe" --llvm "$llvm" --target "$triple" --level 2 --jobs 1
"$work/probe" "$work" "$llvm" "$root/test/native_aot/managed_values/Audit.ll"
if [ -n "$reference" ]; then
    mkdir "$work/reference"
    cp "$root/test/native_aot/managed_values/Records.chibi" "$work/reference/Records.chibi"
    printf '%s\n' 'package managed_records' '@test' 'Calls :: () { FunctionalArrays(); }' > "$work/reference/Tests.chibi"
    "$reference" --project-root "$work/reference" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
    cat "$work/reference.log"
    mkdir "$work/reference-fields"
    cp "$root/test/native_aot/managed_values/Fields.chibi" "$work/reference-fields/Fields.chibi"
    printf '%s\n' 'package field_places' '@test' 'Calls :: () { result := Main(); assert(result == 0); }' > "$work/reference-fields/Tests.chibi"
    "$reference" --project-root "$work/reference-fields" --test > "$work/reference-fields.log" 2>&1 || { cat "$work/reference-fields.log"; exit 1; }
    cat "$work/reference-fields.log"
    mkdir "$work/reference-deep-fields"
    cp "$root/test/native_aot/managed_values/DeepArrayFields.chibi" "$work/reference-deep-fields/Main.chibi"
    printf '%s\n' 'package deep_array_fields' '@test' 'Calls :: () { Cases(); }' > "$work/reference-deep-fields/Tests.chibi"
    "$reference" --project-root "$work/reference-deep-fields" --test > "$work/reference-deep-fields.log" 2>&1 || { cat "$work/reference-deep-fields.log"; exit 1; }
    cat "$work/reference-deep-fields.log"
    for source in Initial Late LateField; do
        mkdir "$work/reference-deep-$source"
        input=$root/test/native_aot/managed_values/deep_places/$source.chibi
        cp "$input" "$work/reference-deep-$source/Main.chibi"
        printf '%s\n' "$(sed -n '1p' "$input")" '@test' 'Check :: () { result := Main(); assert(result == 0); }' > "$work/reference-deep-$source/Tests.chibi"
        set +e
        "$reference" --project-root "$work/reference-deep-$source" --test > "$work/reference-deep-$source.log" 2>&1
        failed=$?
        set -e
        test "$failed" -eq 1
        grep -Eq 'Referenced (array element|storage) is unavailable' "$work/reference-deep-$source.log"
        if grep -q 'RHS evaluated' "$work/reference-deep-$source.log"; then cat "$work/reference-deep-$source.log"; exit 1; fi
    done
fi
