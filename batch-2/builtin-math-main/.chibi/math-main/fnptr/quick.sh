#!/bin/sh
R=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
F=$R/.chibi/math-main/fnptr
cd $R
for d in scalar_facts scalar_llvm llvm_native native_abi; do cp tools/compiler/$d/*.chibi $F/compiler/$d/; done
$F/driver.sh
exec $F/run.sh ${1:-0}
