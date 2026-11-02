#!/bin/sh
# Native arithmetic, source corpus, bounds and process execution coverage.
set -eu
ulimit -c 0
compiler=${1:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
llvm=${2:?usage: run.sh NATIVE_COMPILER LLVM_BIN}
root=$(CDPATH='' cd -- "$(dirname "$0")/../../.." && pwd -P)
case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) target=aarch64-apple-darwin; platform=macos-arm64; linker=ld64.lld ;;
    Linux-x86_64) target=x86_64-unknown-linux-gnu; platform=linux-x64; linker=ld.lld ;;
    *) echo 'native backend: unsupported test host' >&2; exit 2 ;;
esac
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-native-backend.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
mkdir "$work/llvm"
compile() {
    "$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$1" --entry Main --output "$2" --llvm "$3" --target "$target" --level "$4" --jobs 1
}
# Read acceptance and literal answers from the authored fixture authority.
# The current compiler then must compile every accepted source, without skips.
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/backend/Prepare.chibi" --collection "fixtures=$root/test/scalar_facts" \
    --entry Main --output "$work/prepare" --llvm "$llvm" --target "$target" --level 2 --jobs 1
"$work/prepare" "$work"
"$compiler" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/targets/Runner.chibi" --collection "bundle=$root/test/native_aot/targets" \
    --entry Main --output "$work/calls" --llvm "$llvm" --target "$target" --level 2 --jobs 1
for destination in linux-x64 macos-arm64 windows-x64; do
    directory=$work/$destination
    # Independent LLVM22 ABI tripwires retained from the C bundle producer.
    case "$destination" in
        linux-x64)
            triple=x86_64-unknown-linux-gnu
            layout='e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128' ;;
        macos-arm64)
            triple=arm64-apple-macosx11.0.0
            layout='e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32' ;;
        windows-x64)
            triple=x86_64-pc-windows-msvc
            layout='e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128' ;;
    esac
    for level in 0 2; do
        passes=verify
        if [ "$level" = 2 ]; then passes='verify,default<O2>,verify'; fi
        "$llvm/opt" -S "-passes=$passes" "$directory/total.ll" -o "$directory/total-$level.opt.ll"
        optimized=$directory/total-$level.opt.ll
        if [ "$(grep -c '^target triple =' "$optimized")" != 1 ] ||
            ! grep -Fx "target triple = \"$triple\"" "$optimized" >/dev/null ||
            ! grep -Fx "target datalayout = \"$layout\"" "$optimized" >/dev/null; then
            echo "native backend: incorrect $destination target/layout" >&2
            exit 1
        fi
        "$llvm/llc" -filetype=obj "-O=$level" -relocation-model=pic "$directory/total-$level.opt.ll" -o "$directory/0-total-$level.o"
        "$llvm/llvm-nm" --undefined-only --just-symbol-name "$directory/0-total-$level.o" > "$work/undefined"
        if [ -s "$work/undefined" ]; then
            if [ "$destination" != windows-x64 ] || grep -Ev '^(__chkstk|_fltused)$' "$work/undefined"; then
                cat "$work/undefined" >&2
                echo 'native backend: unexpected undefined arithmetic symbol' >&2
                exit 1
            fi
        fi
    done
done
"$llvm/llc" -filetype=obj -relocation-model=pic "$work/windows-x64/runtime.ll" -o "$work/windows-x64/runtime.o"
"$work/calls" --modules "$work/$platform" "$llvm/$linker"

# Keep the established cross-target artifact names and adapter ABI. Only this
# producer owns modules.txt; native-build/link own their separate program lists.
if [ -n "${CHIBI_NATIVE_BUNDLE:-}" ]; then
    for destination in macos-arm64 windows-x64; do
        directory=$CHIBI_NATIVE_BUNDLE/$destination
        mkdir -p "$directory"
        cp "$work/$destination/0-total-0.o" "$work/$destination/0-total-2.o" "$directory/"
        if [ "$destination" = windows-x64 ]; then cp "$work/$destination/runtime.o" "$directory/"; fi
        cp "$work/$destination/modules.txt" "$directory/modules.pending"
        mv "$directory/modules.pending" "$directory/modules.txt"
    done
fi

# A dense O2 switch still links and executes as PIC in a real shared image.
mkdir "$work/pic"
printf 'target triple = "%s"\n' "$target" > "$work/pic/table.ll"
cat >> "$work/pic/table.ll" <<'LLVM'
@index = internal global i32 3
define i32 @table() {
entry:
  %x = load volatile i32, ptr @index
  switch i32 %x, label %other [ i32 0, label %a
    i32 1, label %b
    i32 2, label %c
    i32 3, label %d
    i32 4, label %e ]
a: ret i32 11
b: ret i32 23
c: ret i32 5
d: ret i32 47
e: ret i32 9
other: ret i32 0
}
LLVM
"$llvm/opt" '-passes=verify,default<O2>,verify' "$work/pic/table.ll" -o "$work/pic/table.bc"
"$llvm/llc" -filetype=obj -O=2 -relocation-model=pic "$work/pic/table.bc" -o "$work/pic/table.o"
printf 'module table.o\ncall table 47 0\n' > "$work/pic/modules.txt"
"$work/calls" --modules "$work/pic" "$llvm/$linker"

# Reuse the managed-value allocation/poison oracle, so the original array_drops
# contract checks live allocations before exit rather than relying on the OS.
cat > "$work/llvm/opt" <<'WRAPPER'
#!/bin/sh
set -eu
for source do
    case "$source" in *.ll)
        if grep -q 'define i32 @chibi_scalar(ptr %ctx)' "$source"; then
            sed 's/define i32 @chibi_scalar(ptr %ctx)/define i32 @chibi_value_test_entry(ptr %ctx)/' "$source" > "$source.audit"
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
export CHIBI_VALUE_LLVM=$llvm CHIBI_VALUE_AUDIT=$root/test/native_aot/managed_values/Audit.ll
for level in 0 2; do
    while IFS= read -r name; do
        compile "$work/arrays/$name.chibi" "$work/array-program" "$work/llvm" "$level"
        "$work/array-program"
    done < "$work/arrays/arrays.txt"
    printf 'PASS native array/record/union corpus O%s, exact results and balanced allocations\n' "$level"
done

for index in 3 -1 -2147483648 2147483647; do
    operation='return items[Where()];'
    if [ "$index" = -1 ]; then operation='items[Where()] = 4; return 0;'; fi
    cat > "$work/Outside.chibi" <<SOURCE
package scalar
Main :: () -> int { items: int[] = [1, 2, 3]; $operation }
Where :: () -> int { return $index; }
SOURCE
    for level in 0 2; do
        compile "$work/Outside.chibi" "$work/outside" "$llvm" "$level"
        ended=0
        "$work/outside" > "$work/outside.log" 2>&1 || ended=$?
        if [ "$ended" != 134 ] || ! grep -F "native array index $index is out of range for length 3" "$work/outside.log" >/dev/null; then
            cat "$work/outside.log" >&2
            echo "native backend: bounds failure exited $ended, expected 134" >&2
            exit 1
        fi
    done
done
printf 'package busy\nMain :: () -> int { while true {} return 0; }\n' > "$work/Hang.chibi"
for level in 0 2; do compile "$work/Hang.chibi" "$work/hang-$level" "$llvm" "$level"; done
compile "$root/test/native_aot/backend/Supervision.chibi" "$work/supervision" "$llvm" 2
"$work/supervision" "$work" "$work/hang-0" "$work/hang-2"
printf 'PASS native backend arithmetic, PIC, corpus, bounds and supervision\n'
