#!/bin/sh
set -eu
compiler=$1
llvm=$2
work=$3
root=/private/tmp/chibi-a730-composed-proof/source
triple=aarch64-apple-darwin
printf 'prior native output\n' > "$work/sentinel"
for source in "$root"/test/native_aot/managed_pointers/source/rejected/*.chibi; do
    name=$(basename "$source" .chibi)
    # Earlier negatives passed; container is separately proven positive.
    case "$name" in foreign_array|foreign_scalar|foreign_strings|nested|optional_backed|optional_pointer|optional_view|retained|unguarded_nullable) ;; *) continue ;; esac
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
