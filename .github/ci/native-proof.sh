#!/bin/bash
set -euo pipefail
# The outer launcher, not only NativeSelfHost's children, uses the Release helper.
export CHIBI_BUILD_HOST="$PWD/build-release/sdk/chibi-build-host"
set -o pipefail
stage_one=()
comparison=()
if [ "$CHIBI_FULL" != true ]; then comparison=(--skip-vm-comparison); fi
if grep -q "^linux-x64	" tools/bootstrap/native-seed.tsv; then
  sh tools/bootstrap/native-seed.sh linux-x64 "$RUNNER_TEMP/native-seed/chibi"
  stage_one=(--stage-one "$RUNNER_TEMP/native-seed/chibi")
fi
sh tools/build.sh build native-self-host --llvm /usr/lib/llvm-18/bin --libclang-root /usr/lib/llvm-18 --jobs 4 --build-host "$PWD/build-release/sdk/chibi-build-host" --compiler-host "$PWD/build-release/sdk/chibi-compiler-host" "${stage_one[@]}" "${comparison[@]}" 2>&1 \
  | tee "$RUNNER_TEMP/native-self-host.log"

node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/diagnostics/run.sh "$PWD/build-native-self-host/stage3/chibi" /usr/lib/llvm-18/bin
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/test_attributes/run.sh "$PWD/build-native-self-host/stage3/chibi" /usr/lib/llvm-18/bin

# Packaging uses the same Release host; do not build it again in another PR job.
if [ "$CHIBI_FULL" = true ]; then
  test -z "$(git status --porcelain --untracked-files=normal)"
  node .github/ci/budget.js "$(($(date +%s) + 1200))" sh tools/build.sh test --build-host "$PWD/build-release/sdk/chibi-build-host" graph-sdk-install
  test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
  test -z "$(git status --porcelain --untracked-files=normal)"
fi
