#!/bin/sh
set -eu
compiler=$1
llvm=$2
work=$3
root=/private/tmp/chibi-a730-composed-proof/source
triple=aarch64-apple-darwin
"$work/native-probe" --weak-null "$work/poll-weak-null.ll"
if [ "$(uname -s)-$(uname -m)" = Linux-x86_64 ]; then
    sh "$root/test/native_aot/managed_pointers/weak.sh" "$llvm" "$work/poll-weak-null.ll" \
        "$root/test/native_aot/managed_pointers/PollWeak.ll" "$work/weak-null"
fi
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/managed_pointers/ViewProbe.chibi" --entry Main \
    --output "$work/view-probe" --llvm "$llvm" --target "$triple" --jobs 1
"$work/view-probe" "$work" "$llvm" "$root/test/native_aot/managed_pointers/SourceAudit.ll"
sh "$root/test/native_aot/managed_pointers/view-schema.sh" "$compiler" "$llvm" "$work"
sh "$root/test/native_aot/managed_pointers/text-cell-images.sh" "$compiler" "$llvm" "$work"
sh "$root/test/native_aot/managed_pointers/text-cell-images.sh" "$compiler" "$llvm" "$work" optional
for level in 0 2; do
    for name in fallible_correlated_pointers_retain_valid_error_handling \
                fallible_joined_pointers_keep_unchanged_pointees \
                fallible_optional_pointer_aliases_are_inspected \
                optional_mutation_before_a_fresh_presence_guard_is_safe; do
        CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
            --root "$root/test/compiler/samples/fallible" "$root/test/compiler/samples/fallible/$name/facts" \
            > "$work/optional-$name-O$level.log" 2>&1 || { cat "$work/optional-$name-O$level.log"; exit 1; }
        grep -q '^PASS .*Calls$' "$work/optional-$name-O$level.log"
    done
done
printf 'prior native output\n' > "$work/sentinel"
for source in "$root"/test/native_aot/managed_pointers/source/rejected/*.chibi; do
    name=$(basename "$source" .chibi)
    cp "$work/sentinel" "$work/rejected"
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main --output "$work/rejected" --llvm "$llvm" \
        --target "$triple" --jobs 1 > "$work/rejected-$name.log" 2>&1; then
        echo "native managed pointers: unsupported $name was accepted" >&2
        exit 1
    fi
    grep -F 'native: rejected' "$work/rejected-$name.log" >/dev/null
    cmp "$work/sentinel" "$work/rejected"
done
sh "$root/test/native_aot/managed_pointers/growth.sh" "$compiler" "$work/growth"
echo 'native managed pointer source, collector and boundary proofs passed'
