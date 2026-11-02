#!/bin/bash
set -euo pipefail
# The native build entry reuses its compiler; VM parity uses the explicit Release helpers.
: "${CHIBI_LLVM_BIN:?Install the native LLVM tools before this proof}"
set -o pipefail
stage_one=()
comparison=()
if [ "$CHIBI_FULL" != true ]; then comparison=(--skip-vm-comparison); fi
if grep -q "^linux-x64	" tools/bootstrap/native-seed.tsv; then
  sh tools/bootstrap/native-seed.sh linux-x64 "$RUNNER_TEMP/native-seed/chibi"
  stage_one=(--stage-one "$RUNNER_TEMP/native-seed/chibi")
fi
sh tools/build.sh build native-self-host --llvm "$CHIBI_LLVM_BIN" --libclang-root /usr/lib/llvm-18 --jobs 4 --compare-vm --windows "$RUNNER_TEMP/windows" --build-host "$PWD/build-release/sdk/chibi-build-host" --compiler-host "$PWD/build-release/sdk/chibi-compiler-host" "${stage_one[@]}" "${comparison[@]}" 2>&1 \
  | tee "$RUNNER_TEMP/native-self-host.log"

driver="$PWD/build-native-self-host/stage3/chibi"
sh test/native_aot/native_driver/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/native_driver/cache.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/native_driver/minimum.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/diagnostics/run.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/generations/run.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/test_attributes/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/bootstrap/native-entry.sh
sh test/bootstrap/native-runner.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/tasks/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/maps/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/watch.sh "$driver" "$CHIBI_LLVM_BIN"

# Packaging uses the same Release host; do not build it again in another PR job.
if [ "$CHIBI_FULL" = true ]; then
  test -z "$(git status --porcelain --untracked-files=normal)"
  node .github/ci/budget.js "$(($(date +%s) + 1200))" sh tools/build.sh test --build-host "$PWD/build-release/sdk/chibi-build-host" graph-sdk-install
fi

# Preserve the tested source identity within the existing native proof budget.
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
source_status=$(git status --porcelain --untracked-files=normal)
test -z "$source_status"
