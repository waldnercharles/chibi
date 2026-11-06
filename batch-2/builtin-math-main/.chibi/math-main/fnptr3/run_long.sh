#!/bin/sh
# Usage: run.sh MODE   builds the scratch project natively with the recovered compiler
R=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
F=$R/.chibi/math-main/fnptr3
exec $R/.chibi/math-main/fnptr3/claude-host2/chibi-build-host-macos-arm64 --root $F/driver --source $F/driver/Driver.chibi --collection native_aot=$F/driver/driver --core $R/packages/core --compiler $F/compiler --compiler-core $R/packages/core --entry Native --timeout-ms 3300000 -- $F/runtime/chibi-compiler-host-macos-arm64 $F/compiler $R/packages/core ${PROJ:-$F/proj} $F/llvm ${1:-0} none x86_64-unknown-linux-gnu
