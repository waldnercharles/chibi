#!/bin/sh
# The same catalog requests execute through LLVM's Wasm backend without a VM.
set -eu
driver=${1:?usage: wasm.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN NODE}
llvm=${2:?usage: wasm.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN NODE}
emcc=${3:?usage: wasm.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN NODE}
wasm_llvm=${4:?usage: wasm.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN NODE}
node=${5:?usage: wasm.sh NATIVE_DRIVER NATIVE_LLVM_BIN EMCC WASM_LLVM_BIN NODE}
root=$(cd "$(dirname "$0")/../../.." && pwd -P)
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-asset-wasm.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf 'package main\nMain :: () -> int { return missing; }\n' > "$work/rejected.chibi"
printf 'unchanged' > "$work/rejected.ll"
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target unknown-target \
    --emit-llvm --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 1 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'unsupported native target' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
for signature in '(value: rawptr) -> int' '() -> rawptr' '(values: rawptr[]) -> int' '(callback: fn(value: int) -> int) -> int' '() -> fn(value: int) -> int'; do
    printf 'package main\nforeign { Bad :: %s }\nMain :: () -> int { return 0; }\n' "$signature" > "$work/rejected.chibi"
    set +e
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$work/rejected.chibi" --entry Main --target wasm32-unknown-emscripten \
        --emit-llvm --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
    status=$?
    set -e
    if [ "$status" -ne 1 ]; then cat "$work/rejected.log"; exit 1; fi
    case "$signature" in
        *rawptr*) grep -F 'native WebAssembly foreign rawptr signatures are not supported' "$work/rejected.log" ;;
        *) grep -F 'native WebAssembly foreign function-pointer signatures are not supported' "$work/rejected.log" ;;
    esac
    [ "$(cat "$work/rejected.ll")" = unchanged ]
done
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target wasm32-unknown-emscripten \
    --llvm "$llvm" --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 2 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'WebAssembly requires --emit-llvm' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
printf 'package main\nMain :: () -> int { yield; return 0; }\n' > "$work/rejected.chibi"
set +e
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$work/rejected.chibi" --entry Main --target wasm32-unknown-emscripten \
    --emit-llvm --output "$work/rejected.ll" > "$work/rejected.log" 2>&1
status=$?
set -e
if [ "$status" -ne 1 ]; then cat "$work/rejected.log"; exit 1; fi
grep -F 'native WebAssembly tasks have no runtime yet' "$work/rejected.log"
[ "$(cat "$work/rejected.ll")" = unchanged ]
sh "$root/test/native_aot/asset_compiler/publication.sh" "$driver" "$llvm"
compile_objects() {
    wasm_source=$1
    wasm_work=$2
    mkdir -p "$wasm_work"
    "$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
        --source "$wasm_source" --entry Main --target wasm32-unknown-emscripten \
        --emit-llvm --output "$wasm_work/compiler.ll"
    awk -v directory="$wasm_work" '
    BEGIN { piece=0; file=sprintf("%s/piece-%04d.ll", directory, piece) }
    /^; chibi-piece$/ { close(file); piece++; file=sprintf("%s/piece-%04d.ll", directory, piece); next }
    { print > file }
' "$wasm_work/compiler.ll"
    for source in "$wasm_work"/piece-*.ll; do
        "$wasm_llvm/opt" -S -passes='default<O2>,verify' "$source" -o "$source.opt.ll"
        "$wasm_llvm/llc" -O2 -filetype=obj "$source.opt.ll" -o "$source.o"
    done
}
compile_wasm() {
    compile_objects "$1" "$2"
    "$emcc" -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -I"$root/src" \
        -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
        -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 \
        "$work/runtime.o" "$root/test/native_aot/asset_compiler/Wasm.c" \
        "$wasm_work"/piece-*.ll.o -o "$wasm_work/compiler.js"
}
target=x86_64-unknown-linux-gnu
if [ "$(uname -s)" = Darwin ]; then target=aarch64-apple-darwin; fi
"$driver" --compiler "$root" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/WasmRuntime.chibi" --collection "graph=$root/tools/graph" \
    --entry Main --target "$target" --llvm "$llvm" --level 0 --output "$work/runtime"
"$work/runtime" "$root" "$emcc" "$work" "$work/runtime.ll"
"$wasm_llvm/opt" -passes=verify "$work/runtime.ll" -o "$work/runtime.bc"
"$emcc" -O2 -sSUPPORT_LONGJMP=wasm -c "$work/runtime.bc" -o "$work/runtime.o"
compile_wasm "$root/test/native_aot/asset_compiler/WasmFormat.chibi" "$work/format"
printf '[]' > "$work/empty.json"
"$node" "$work/format/compiler.js" "$work/empty.json" "$work/format.bin" "$work/format.diagnostic"
printf 'ffffffffffffffff|1.25|true|   a\000b|{}|1.25' > "$work/format.expected"
cmp "$work/format.expected" "$work/format.bin"
"$driver" --compiler "$root" --core "$root/packages/core" \
    --source "$root/tools/graph/asset_wasm/Build.chibi" --collection "graph=$root/tools/graph" \
    --entry Main --target "$target" --llvm "$llvm" --level 2 --output "$work/build"
"$work/build" "$root" "$driver" "$emcc" "$wasm_llvm" "$work/package" --test
compile_objects "$root/test/native_aot/asset_compiler/WasmGame.chibi" "$work/game"
mkdir -p "$work/service"
"$emcc" -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST \
    -DCHIBI_ASSET_COMPILER_WASM_PUBLIC -DCHIBI_ASSET_COMPILER_WASM_GAME -I"$root/src" \
    -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 \
    "$root/test/native_aot/asset_compiler/Wasm.c" "$root/test/native_aot/asset_compiler/WasmGame.c" \
    "$work/package/libchibi_asset_compiler.a" "$work/game"/piece-*.ll.o -o "$work/service/compiler.js"
"$emcc" -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -I"$root/src" \
    -pthread -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sINITIAL_MEMORY=268435456 \
    -sSTACK_SIZE=8388608 -sDEFAULT_PTHREAD_STACK_SIZE=8388608 -sPTHREAD_POOL_SIZE=2 \
    -sENVIRONMENT=node -sNODERAWFS=1 "$root/test/native_aot/asset_compiler/WasmThreads.c" \
    "$work/package/libchibi_asset_compiler_threads.a" -o "$work/service/threads.js"
cat > "$work/compiler" <<EOF
#!/bin/sh
exec "$node" "$work/service/compiler.js" "\$@"
EOF
chmod +x "$work/compiler"
"$driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Verify.chibi" --entry Main \
    --output "$work/verify" --llvm "$llvm" --target "$target" --level 0 --jobs 2
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$work/verify" \
    "$work/compiler" "$root/test/native_aot/asset_compiler/catalog.json" "$work" "" ""
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$node" "$work/service/compiler.js" --reuse "$work/simple.json" "$work/cache"
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$node" "$work/service/threads.js" "$work/simple.json"
printf '%s\n' 'LLVM Wasm asset closures, diagnostics, quota and rejection passed'
