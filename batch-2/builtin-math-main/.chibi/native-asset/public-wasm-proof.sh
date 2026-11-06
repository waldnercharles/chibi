#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
work=$root/.chibi/native-asset/wasm-public-proof
llvm=/opt/homebrew/Cellar/emscripten/6.0.9/libexec/llvm/bin
mkdir -p "$work"
"$root/.chibi/native-asset/wasm-emitter-pointer-gate" --compiler "$root/tools/compiler" --core "$root/packages/core" --source "$root/test/native_aot/asset_compiler/WasmGame.chibi" --entry Main --target wasm32-unknown-emscripten --emit-llvm --output "$work/game.ll"
awk -v directory="$work" '
BEGIN { piece=0; file=sprintf("%s/game-%04d.ll", directory, piece) }
/^; chibi-piece$/ { close(file); piece++; file=sprintf("%s/game-%04d.ll", directory, piece); next }
{ print > file }
' "$work/game.ll"
for source in "$work"/game-*.ll; do
    "$llvm/llc" -O2 -filetype=obj "$source" -o "$source.o"
done
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -DCHIBI_ASSET_COMPILER_WASM_GAME -DCHIBI_ASSET_COMPILER_WASM_PUBLIC -I"$root/src" -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 "$root/test/native_aot/asset_compiler/WasmGame.c" "$root/test/native_aot/asset_compiler/Wasm.c" "$root/.chibi/native-asset/sdk-wasm-instrumented/libchibi_asset_compiler.a" "$work"/game-*.ll.o -o "$work/public.js"
/opt/homebrew/bin/node "$work/public.js" --reuse "$root/.chibi/native-asset/wasm-linked/requests/simple.json" "$work/cache"
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -I"$root/src" -pthread -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608 -sDEFAULT_PTHREAD_STACK_SIZE=8388608 -sPTHREAD_POOL_SIZE=2 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 "$root/test/native_aot/asset_compiler/WasmThreads.c" "$root/.chibi/native-asset/sdk-wasm-instrumented/libchibi_asset_compiler_threads.a" -o "$work/threads.js"
/opt/homebrew/bin/node "$work/threads.js" "$root/.chibi/native-asset/wasm-linked/requests/simple.json"
