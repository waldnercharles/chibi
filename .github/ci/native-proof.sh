#!/bin/bash
set -euo pipefail
# Fixed-point and independent Driver/Graph command proofs use the native compiler.
: "${CHIBI_LLVM_BIN:?Install the native LLVM tools before this proof}"
# A failed core test may precede this phase. It must not change the proven source.
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
source_status=$(git status --porcelain --untracked-files=normal)
test -z "$source_status"
set -o pipefail
stage_one=()
windows=()
if [ "$(uname -s)" = Linux ] && [ -z "${CHIBI_WINDOWS_BOOTSTRAP_RUN:-}" ]; then windows=(--windows "$RUNNER_TEMP/windows"); fi
if grep -q "^linux-x64	" tools/bootstrap/native-seed.tsv; then
  sh tools/bootstrap/native-seed.sh linux-x64 "$RUNNER_TEMP/native-seed/chibi"
  stage_one=(--stage-one "$RUNNER_TEMP/native-seed/chibi")
fi
sh tools/build.sh build native-self-host --llvm "$CHIBI_LLVM_BIN" --jobs 4 "${windows[@]}" "${stage_one[@]}" 2>&1 \
  | tee "$RUNNER_TEMP/native-self-host.log"

driver="$PWD/build-native-self-host/stage3/chibi"
# ELF interpreter and Linux line-table checks execute on their owning platform.
if [ "$(uname -s)" = Linux ]; then
  sh test/native_aot/native_driver/run.sh "$driver" "$CHIBI_LLVM_BIN"
fi
sh test/native_aot/native_driver/cache.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/native_driver/minimum.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/text_format/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/link/memory.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/directories/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/output/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/diagnostics/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/generations/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/test_attributes/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/failures/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/task_busy/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/optionals/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/fallible/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/patterns/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/host_clock/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/bootstrap/native-entry.sh
sh test/bootstrap/native-runner.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/tasks/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/math_scalar/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/math_builtin/select.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/math_builtin/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/characters/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/array_operations/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/remove_item/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/text_arguments/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/nested_arrays/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/managed_values/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_aot/managed_pointers/run.sh "$driver" "$CHIBI_LLVM_BIN" "$RUNNER_TEMP/native-managed-pointers"
sh test/native_aot/maps/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/run.sh "$driver" "$CHIBI_LLVM_BIN"
sh test/native_cli/watch.sh "$driver" "$CHIBI_LLVM_BIN"

# Packaging uses the same native Release products.
if [ "$CHIBI_FULL" = true ]; then
  test -z "$(git status --porcelain --untracked-files=normal)"
  bash -euo pipefail -s <<'SDK'
sh tools/build.sh build --release --target chibi --target chibi-asset-compiler --target chibi_asset_compiler_host
sh tools/build.sh test graph-sdk-install
sh test/native_aot/asset_compiler/bridge.sh "$PWD/build-release/sdk/chibi-asset-compiler" "$PWD/build-release/sdk/chibi" "$CHIBI_LLVM_BIN" "${CC:-cc}" "$PWD/build-release/lib/libchibi_asset_compiler_host.a"
sh test/native_aot/asset_compiler/archive.sh "$PWD/build-release/sdk/chibi" "$CHIBI_LLVM_BIN"
SDK
fi

# Preserve the tested source identity within the existing native proof budget.
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
source_status=$(git status --porcelain --untracked-files=normal)
test -z "$source_status"
