#!/bin/bash
set -euo pipefail
# The native build entry reuses its compiler; VM parity uses the explicit Release helpers.
: "${CHIBI_LLVM_BIN:?Install the native LLVM tools before this proof}"
set -o pipefail
stage_one=()
comparison=()
windows=()
if [ "$CHIBI_FULL" != true ]; then comparison=(--skip-vm-comparison); fi
if [ -z "${CHIBI_WINDOWS_BOOTSTRAP_RUN:-}" ]; then windows=(--windows "$RUNNER_TEMP/windows"); fi
if grep -q "^linux-x64	" tools/bootstrap/native-seed.tsv; then
  sh tools/bootstrap/native-seed.sh linux-x64 "$RUNNER_TEMP/native-seed/chibi"
  stage_one=(--stage-one "$RUNNER_TEMP/native-seed/chibi")
fi
sh tools/build.sh build native-self-host --llvm "$CHIBI_LLVM_BIN" --libclang-root /usr/lib/llvm-18 --jobs 4 --compare-vm "${windows[@]}" --build-host "$PWD/build-release/sdk/chibi-build-host" --compiler-host "$PWD/build-release/sdk/chibi-compiler-host" "${stage_one[@]}" "${comparison[@]}" 2>&1 \
  | tee "$RUNNER_TEMP/native-self-host.log"

driver="$PWD/build-native-self-host/stage3/chibi"
sh test/native_aot/native_driver/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/native_driver/cache.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/native_driver/minimum.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/text_format/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/output/run.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/diagnostics/run.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/generations/run.sh "$driver" "$CHIBI_LLVM_BIN"
node .github/ci/budget.js "$(($(date +%s) + 300))" sh test/native_aot/test_attributes/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/failures/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/task_busy/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/optionals/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/bootstrap/native-entry.sh
sh test/bootstrap/native-runner.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/tasks/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/math_scalar/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/characters/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/managed_values/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/managed_pointers/run.sh "$driver" "$CHIBI_LLVM_BIN" "$RUNNER_TEMP/native-managed-pointers"
sh test/native_aot/maps/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/watch.sh "$driver" "$CHIBI_LLVM_BIN"

# Packaging uses the same Release host; do not build it again in another PR job.
if [ "$CHIBI_FULL" = true ]; then
  test -z "$(git status --porcelain --untracked-files=normal)"
  node .github/ci/budget.js "$(($(date +%s) + 1200))" bash -euo pipefail -s <<'SDK'
sh tools/build.sh build --release --target chibi --target chibi-asset-compiler --libclang-root /usr/lib/llvm-18
sh tools/build.sh test --build-host "$PWD/build-release/sdk/chibi-build-host" graph-sdk-install
sh test/native_aot/asset_compiler/bridge.sh "$PWD/build-release/sdk/chibi-asset-compiler" "$PWD/build-release/sdk/chibi" "$CHIBI_LLVM_BIN" clang-18
SDK
fi

# Preserve the tested source identity within the existing native proof budget.
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
source_status=$(git status --porcelain --untracked-files=normal)
test -z "$source_status"
