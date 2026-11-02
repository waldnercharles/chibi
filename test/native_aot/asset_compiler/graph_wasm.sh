#!/bin/sh
set -eu
graph=${1:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
build=${2:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
seed=${3:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
llvm=${4:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
emcc=${5:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
wasm_llvm=${6:?usage: graph_wasm.sh GRAPH BUILD SEED NATIVE_LLVM EMCC WASM_LLVM}
work=$(mktemp -d "${TMPDIR:-/tmp}/chibi-wasm-graph.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
record=$build/sdk/wasm32/libchibi_asset_compiler.a.chibi-record
cp "$record" "$work/before"
"$graph" --build "$build" --target chibi-asset-compiler-wasm --native-driver "$seed" --llvm "$llvm" \
    --emcc "$emcc" --wasm-llvm "$wasm_llvm" --jobs 2 --compiler /usr/bin/false --cxx-compiler /usr/bin/false
cmp "$record" "$work/before"
grep -F '/.chibi/build-runner/' "$build/link_commands.json" > /dev/null
set +e
EMCC_CFLAGS=-O0 "$graph" --build "$build" --target chibi-asset-compiler-wasm --native-driver "$seed" --llvm "$llvm" \
    --emcc "$emcc" --wasm-llvm "$wasm_llvm" > "$work/flags.log" 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
grep -F 'unsupported implicit tool option EMCC_CFLAGS' "$work/flags.log"
cmp "$record" "$work/before"
set +e
"$graph" --build "$work/missing" --target chibi-asset-compiler-wasm --native-driver /absent/compiler \
    --llvm /absent/llvm > "$work/missing.log" 2>&1
status=$?
set -e
[ "$status" -eq 2 ]
grep -F 'optional Wasm compiler requires --emcc FILE and --wasm-llvm DIR' "$work/missing.log"
"$graph" --build "$work/plan" --target chibi-asset-compiler-wasm --native-driver /absent/compiler \
    --llvm /absent/llvm --emcc /absent/emcc --wasm-llvm /absent/wasm --commands-only on
grep -F '/absent/compiler' "$work/plan/link_commands.json" > /dev/null
printf '%s\n' 'optional Wasm graph reuses verified artifacts and rejects hidden tool flags'
