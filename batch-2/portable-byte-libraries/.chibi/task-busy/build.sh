#!/bin/sh
set -eu
root=$(pwd -P)
/usr/bin/time -p /Users/cwaldner/Personal/chibi/.worktrees/compositional-records-integration/chibi/build-native-self-host/timing/chibi-checkpoint3 --compiler "$root/tools/compiler" --core "$root/packages/core" --package native_driver --entry Main --output "$root/.chibi/task-busy/chibi" --llvm /tmp/chibi-llvm22 --target aarch64-apple-darwin --jobs 4
