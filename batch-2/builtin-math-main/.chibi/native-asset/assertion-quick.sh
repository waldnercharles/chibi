#!/bin/sh
set -eu
root=/Users/cwaldner/Personal/chibi/.worktrees/builtin-math-main/chibi
compiler=$root/.chibi/native-asset/chibi-assertions
work=$root/.chibi/native-asset/assertion-probe
export CHIBI_CORE=$root/packages/core
set +e
"$compiler" run --llvm /tmp/chibi-llvm22 -O0 "$work/Nested.chibi" > "$work/public.log" 2>&1
probe_status=$?
set -e
cat "$work/public.log"
[ "$probe_status" = 1 ]
grep -F "$work/Nested.chibi:3:5: Assertion failed: nested program failure" "$work/public.log"
"$compiler" --compiler "$work" --core "$CHIBI_CORE" --source "$work/Nested.chibi" --entry Main --output "$work/raw" --llvm /tmp/chibi-llvm22 --target aarch64-apple-darwin --level 0
set +e
"$work/raw" > "$work/raw.log" 2>&1
probe_status=$?
set -e
cat "$work/raw.log"
[ "$probe_status" = 134 ]
set +e
"$compiler" run --llvm /tmp/chibi-llvm22 -O0 "$work/Exit.chibi" > "$work/exit.log" 2>&1
probe_status=$?
set -e
cat "$work/exit.log"
[ "$probe_status" = 134 ]
printf '%s\n' 'source assertion status1; raw assertion and explicit return134 preserved'
