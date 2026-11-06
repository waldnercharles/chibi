#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
work=$root/.chibi/native-asset/wasm-linked
ems=/opt/homebrew/Cellar/emscripten/6.0.9/libexec
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -I"$root/src" \
    -flto -sSUPPORT_LONGJMP=wasm -c "$root/src/authoring/asset_compiler_wasm.c" -o "$work/runtime.bc"
"$ems/llvm/bin/llvm-link" "$work/runtime.bc" "$work"/piece-*.ll.opt.ll -o "$work/compiler-all.bc"
"$ems/llvm/bin/opt" -passes=internalize,globaldce,verify \
    -internalize-public-api-list=asset_compiler_wasm_compile "$work/compiler-all.bc" -o "$work/compiler-private.bc"
/opt/homebrew/bin/emcc -O0 -sSUPPORT_LONGJMP=wasm -r "$work/compiler-private.bc" -o "$work/compiler-private.o"
"$ems/llvm/bin/llvm-nm" --defined-only --extern-only "$work/compiler-private.o"
