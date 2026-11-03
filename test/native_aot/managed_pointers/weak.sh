#!/bin/sh
set -eu
llvm=${1:?pass the LLVM tools directory}
module=${2:?pass the generated null-root Linux LLVM module}
fixture=${3:?pass PollWeak.ll}
work=${4:?pass the owned output directory}
mkdir -p "$work"
# The fixture supplies the standalone failure definition in this one object.
sed '/^declare void @chibi_native_failure(i32) noreturn$/d' "$module" > "$work/weak.ll"
cat "$fixture" >> "$work/weak.ll"
for level in 0 2; do
    "$llvm/opt" -O"$level" -S "$work/weak.ll" -o "$work/weak-O$level.opt.ll"
    "$llvm/llc" -O"$level" -filetype=obj "$work/weak-O$level.opt.ll" -o "$work/weak-O$level.o"
    "$llvm/ld.lld" -static -e _start -o "$work/weak-O$level" "$work/weak-O$level.o"
    "$work/weak-O$level"
done
echo 'managed poll null weak errno: static Linux O0/O2 passed'
