#!/bin/sh
# Execute scalar built-ins at both optimization levels through the native driver.
set -eu
compiler=${1:?pass the native compiler}
llvm=${2:?pass the LLVM tools directory}
reference=${3:-}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu ;;
    *) echo 'native scalar math: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-math-scalar.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
for level in 0 2; do
    for source in SquareRoot Shadow Kernels KernelShadow KernelWaiting ForeignWaiting; do
        "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
            --source "$root/test/native_aot/math_scalar/$source.chibi" --entry Main \
            --output "$work/$source-$level" --llvm "$llvm" --target "$target" --level "$level" --jobs 1
        "$work/$source-$level"
    done
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        --root "$root/test/compiler/samples/base" \
        "$root/test/compiler/samples/base/grouped_signed_floating_literals/facts" > "$work/grouped.log" 2>&1 || { cat "$work/grouped.log"; exit 1; }
    awk '/^PASS project[.]Calls$/ { found=1 } END { exit !found }' "$work/grouped.log"
    CHIBI_CORE="$root/packages/core" "$compiler" test --llvm "$llvm" -O"$level" \
        "$root/test/native_aot/math_scalar/ready_tests" > "$work/ready.log" 2>&1 || { cat "$work/ready.log"; exit 1; }
    grep -F 'PASS project.ScalarMathMayFinishBeforeWaiting' "$work/ready.log" > /dev/null
    printf 'PASS native scalar math widths, precision, argument order and shadow O%s\n' "$level"
done
for source in "$root"/test/native_aot/math_scalar/rejected_waiting/*.chibi; do
    if "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$source" --entry Main --output "$work/rejected" --llvm "$llvm" \
        --target "$target" > "$work/rejected.log" 2>&1; then
        echo 'native scalar math: borrowed managed input entered a waiting frame' >&2
        exit 1
    fi
    grep -F 'native waiting frames cannot own managed values or pointers yet' "$work/rejected.log" > /dev/null
    test ! -e "$work/rejected"
done
if [ -n "$reference" ]; then
    for source in SquareRoot Kernels; do
        mkdir "$work/reference-$source"
        cp "$root/test/native_aot/math_scalar/$source.chibi" "$work/reference-$source/$source.chibi"
        package=scalar_square_root
        if [ "$source" = Kernels ]; then package=scalar_math_kernels; fi
        printf '%s\n' "package $package" '@test' 'Calls :: () { result := Main(); assert(result == 0); }' > "$work/reference-$source/Tests.chibi"
        "$reference" --project-root "$work/reference-$source" --test > "$work/reference.log" 2>&1 || { cat "$work/reference.log"; exit 1; }
        grep -F 'PASS' "$work/reference.log" > /dev/null
        cat "$work/reference.log"
    done
fi
sh "$root/test/native_aot/math_scalar/remainder.sh" "$compiler" "$llvm" "$reference"
