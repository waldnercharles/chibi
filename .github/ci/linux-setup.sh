#!/bin/bash
set -euo pipefail
export CHIBI_TEST_TIMINGS="$RUNNER_TEMP/case-timings"
node .github/ci/budget.js "$(($(date +%s) + 300))" bash .github/ci/format.sh
mkdir -m 700 -p "$RUNNER_TEMP/build-timings" "$CHIBI_TEST_TIMINGS"
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
test -z "$(git status --porcelain --untracked-files=normal)"

# Install build dependencies
sudo apt-get update -q && sudo apt-get install -y -q ccache clang-18 clang-tools-18 libclang-18-dev libclang-rt-18-dev llvm-18 lld-18 \
  libgl1-mesa-dev libvulkan1 mesa-vulkan-drivers libx11-dev libxcursor-dev \
  libxext-dev libxi-dev libxrandr-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libasound2-dev libpulse-dev
mkdir -m 700 -p "$XDG_RUNTIME_DIR" "$RUNNER_TEMP/build-timings"


# Select CI native profile
sh .github/native-profile.sh

# Configure compiler cache
sh .github/native-ccache.sh
