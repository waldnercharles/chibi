#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
work=$root/.chibi/native-asset/wasm-linked
game=$root/.chibi/native-asset/wasm-game
ems=/opt/homebrew/Cellar/emscripten/6.0.9/libexec
mkdir -p "$game"
"$root/.chibi/native-asset/wasm-driver-task-gate" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/WasmGame.chibi" --entry Main \
    --target wasm32-unknown-emscripten --emit-llvm --output "$game/game.ll"
awk -v directory="$game" '
    BEGIN { piece=0; file=sprintf("%s/piece-%04d.ll", directory, piece) }
    /^; chibi-piece$/ { close(file); piece++; file=sprintf("%s/piece-%04d.ll", directory, piece); next }
    { print > file }
' "$game/game.ll"
for source in "$game"/piece-*.ll; do
    case "$source" in *.opt.ll) continue ;; esac
    "$ems/llvm/bin/opt" -S -passes='default<O2>,verify' "$source" -o "$source.opt.ll"
    "$ems/llvm/bin/llc" -O2 -filetype=obj "$source.opt.ll" -o "$source.o"
done
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -I"$root/src" \
    -flto -sSUPPORT_LONGJMP=wasm -c "$root/src/authoring/asset_compiler_wasm.c" -o "$work/runtime-test.bc"
"$ems/llvm/bin/llvm-link" "$work/runtime-test.bc" "$work"/piece-*.ll.opt.ll -o "$work/compiler-all-test.bc"
"$ems/llvm/bin/opt" -passes=internalize,globaldce,verify \
    -internalize-public-api-list=asset_compiler_wasm_compile,asset_compiler_wasm_test_allocations \
    "$work/compiler-all-test.bc" -o "$work/compiler-private-test.bc"
/opt/homebrew/bin/emcc -O2 -sSUPPORT_LONGJMP=wasm -r "$work/compiler-private-test.bc" -o "$work/compiler-private-test.o"
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -DCHIBI_ASSET_COMPILER_WASM_GAME -I"$root/src" \
    -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 \
    "$root/test/native_aot/asset_compiler/WasmGame.c" "$root/test/native_aot/asset_compiler/Wasm.c" \
    "$work/compiler-private-test.o" "$game"/piece-*.ll.o -o "$work/combined-game.js"
/opt/homebrew/bin/node "$work/combined-game.js" --reuse "$work/requests/simple.json"
