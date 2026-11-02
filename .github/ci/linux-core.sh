#!/bin/bash
set -euo pipefail
export CHIBI_TEST_TIMINGS="$RUNNER_TEMP/case-timings"
export CHIBI_NATIVE_BUNDLE="$RUNNER_TEMP/native-bundle" UBSAN_OPTIONS=halt_on_error=1
ccache --show-config
ccache --zero-stats

# Keep scalar/target evidence even if another selector fails, as before.
debug_attempted=false debug_succeeded=false
finish() {
  status=$?
  trap - EXIT
  set +e
  unset CHIBI_BUILD_TIMINGS
  if [ "$debug_attempted" = true ]; then
    outcome=failure
    if [ "$debug_succeeded" = true ]; then outcome=success; fi
    sh tools/build.sh native-evidence --sha "$GITHUB_SHA" --run "$GITHUB_RUN_ID" --attempt "$GITHUB_RUN_ATTEMPT" --job "$GITHUB_JOB" --sanitizers "$CHIBI_CI_SANITIZERS" --profile "$BUILD_PROFILE" --optimization "$BUILD_DEBUG_OPTIMIZATION" --frame-pointers "$BUILD_DEBUG_FRAME_POINTERS" --outcome "$outcome" --host "$PWD/build-release/sdk/chibi-build-host" --build build --output "$RUNNER_TEMP/native-profile" --nm /usr/lib/llvm-18/bin/llvm-nm || echo "::warning::Native profile evidence failed (informational)"
  fi
  if [ "$debug_succeeded" = true ]; then
    CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin sh tools/build.sh test --build-host build-release/sdk/chibi-build-host language-scalar-native
    scalar_status=$?
    if [ "$status" = 0 ]; then status=$scalar_status; fi
    if [ "$scalar_status" = 0 ]; then touch "$RUNNER_TEMP/native-bundle-ready"; fi
  fi
  ccache --show-stats --verbose
  cache_status=$?
  if [ "$status" = 0 ]; then status=$cache_status; fi
  exit "$status"
}
trap finish EXIT

# Build an unsanitized chibi launcher (Release)
export CHIBI_BUILD_TIMINGS="$RUNNER_TEMP/build-timings"
sh tools/build.sh build --release --target chibi --target chibi-build-host --target chibi-compiler-host --target test_tools --libclang-root /usr/lib/llvm-18
export CHIBI_BUILD_HOST=build-release/sdk/chibi-build-host

# Build products and test drivers (selected Debug profile)
debug_attempted=true
set -o pipefail
VERBOSE=1 sh tools/build.sh build --build build --profile "$BUILD_PROFILE" --debug-optimization "$BUILD_DEBUG_OPTIMIZATION" --debug-frame-pointers "$BUILD_DEBUG_FRAME_POINTERS" --target chibi --target chibi-compile --target test_tools --target test_language --host "$PWD/build-release/sdk/chibi-build-host" --asset-generator-hosts build-release/sdk --libclang-root /usr/lib/llvm-18 2>&1 | tee "$RUNNER_TEMP/native-build.log"
debug_succeeded=true
unset CHIBI_BUILD_TIMINGS
export CHIBI_TOOLS_BUILD_DIR=build/test/tools CHIBI_LANGUAGE_BUILD_DIR=build/test/language

# Graph and native driver programs
sh tools/build.sh test --build-host "$CHIBI_BUILD_HOST" graph-engine graph-driver graph-evidence graph-discovery graph-asset-cache graph-os native-aot-driver

# Tooling and process integrations
mkdir -m 700 -p "$CHIBI_TEST_TIMINGS"
sh tools/build.sh test --build-host build-release/sdk/chibi-build-host short
build/test/tools/test_tools --case-timings


# Native, material, project and language selectors
set -e
mkdir -m 700 -p "$CHIBI_TEST_TIMINGS"
export CHIBI_TOOLS_BUILD_DIR=build/test/tools CHIBI_LANGUAGE_BUILD_DIR=build/test/language
for driver in "$CHIBI_TOOLS_BUILD_DIR/test_tools" "$CHIBI_LANGUAGE_BUILD_DIR/test_language" build-release/sdk/chibi build-release/sdk/chibi-compile; do test -x "$driver"; done
sh tools/build.sh test --build-host build-release/sdk/chibi-build-host native
CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin sh tools/build.sh test --build-host build-release/sdk/chibi-build-host --jobs 3 bootstrap-load projects language
