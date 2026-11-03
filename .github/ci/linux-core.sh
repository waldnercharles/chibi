#!/bin/bash
set -euo pipefail
export CHIBI_TEST_TIMINGS="$RUNNER_TEMP/case-timings"
export UBSAN_OPTIONS=halt_on_error=1
mkdir -m 700 -p "$RUNNER_TEMP/build-timings" "$CHIBI_TEST_TIMINGS"

# Native products have one LLVM recipe. The C profile applies only to explicit
# foreign-code fixtures; it does not instrument the native compiler or facade.
CHIBI_BUILD_TIMINGS="$RUNNER_TEMP/build-timings" sh tools/build.sh build --release
sh tools/build.sh native-evidence --sha "$GITHUB_SHA" --run "$GITHUB_RUN_ID" \
  --attempt "$GITHUB_RUN_ATTEMPT" --job "$GITHUB_JOB" --outcome success \
  --build build-release --output "$RUNNER_TEMP/native-profile" --nm "$(command -v nm)"

# Linux produces the exact cross-target bundle consumed by both platform jobs.
if [ "$(uname -s)" = Linux ]; then
  export CHIBI_NATIVE_BUNDLE="$RUNNER_TEMP/native-bundle"
  sh test/native_aot/targets/build.sh "$PWD/build-release/sdk/chibi" "$CHIBI_LLVM_BIN" "$CHIBI_NATIVE_BUNDLE"
fi
sh tools/build.sh test graph-engine graph-driver graph-commands graph-evidence graph-discovery graph-wasm graph-os
sh tools/build.sh test short native projects language language-scalar-native
