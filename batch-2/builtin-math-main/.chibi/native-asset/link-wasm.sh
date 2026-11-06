#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
work=$root/.chibi/native-asset/wasm-linked
llvm=/tmp/chibi-llvm22
mkdir -p "$work"
awk -v directory="$work" '
    BEGIN { piece=0; file=sprintf("%s/piece-%04d.ll", directory, piece) }
    /^; chibi-piece$/ { close(file); piece++; file=sprintf("%s/piece-%04d.ll", directory, piece); next }
    { print > file }
' "$root/.chibi/native-asset/asset.wasm.ll"
for source in "$work"/piece-*.ll; do
    case "$source" in *.opt.ll) continue ;; esac
    "$llvm/opt" -S -passes='default<O2>,verify' "$source" -o "$source.opt.ll"
    "$llvm/llc" -O2 -filetype=obj "$source.opt.ll" -o "$source.o"
done
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -I"$root/src" \
    -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 \
    "$root/src/authoring/asset_compiler_wasm.c" "$root/test/native_aot/asset_compiler/Wasm.c" \
    "$work"/piece-*.ll.o -o "$work/compiler.js"
