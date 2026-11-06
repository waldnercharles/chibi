#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
work=$root/.chibi/native-asset/wasm-linked
mkdir -p "$work/requests"
/opt/homebrew/bin/emcc -O2 -std=c11 -Wall -Wextra -Werror -DCHIBI_ASSET_COMPILER_WASM_TEST -I"$root/src" \
    -sSUPPORT_LONGJMP=wasm -sSTACK_OVERFLOW_CHECK=2 -sALLOW_MEMORY_GROWTH=1 \
    -sSTACK_SIZE=8388608 -sMAXIMUM_MEMORY=2147483648 -sENVIRONMENT=node -sNODERAWFS=1 \
    "$root/src/authoring/asset_compiler_wasm.c" "$root/test/native_aot/asset_compiler/Wasm.c" \
    "$work"/piece-*.ll.o -o "$work/compiler.js"
cat > "$work/compiler" <<EOF
#!/bin/sh
exec /opt/homebrew/bin/node "$work/compiler.js" "\$@"
EOF
chmod +x "$work/compiler"
"$root/.chibi/native-asset/wasm-driver" --compiler "$root/tools/compiler" --core "$root/packages/core" \
    --source "$root/test/native_aot/asset_compiler/Verify.chibi" --entry Main \
    --output "$work/verify" --llvm /tmp/chibi-llvm22 --target aarch64-apple-darwin --level 0 --jobs 2
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false "$work/verify" \
    "$work/compiler" "$root/test/native_aot/asset_compiler/catalog.json" "$work/requests" "" ""
env -i PATH= CC=/usr/bin/false CXX=/usr/bin/false /opt/homebrew/bin/node "$work/compiler.js" --reuse "$work/requests/simple.json"
