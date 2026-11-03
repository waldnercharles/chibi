#!/bin/bash
set -euo pipefail
bash .github/ci/format.sh
mkdir -m 700 -p "$RUNNER_TEMP/build-timings" "$RUNNER_TEMP/case-timings"
test "$(git rev-parse HEAD)" = "$GITHUB_SHA"
test -z "$(git status --porcelain --untracked-files=normal)"
sh .github/native-llvm.sh
# C compilers exercise the public foreign ABI and generic build graph only.
sudo apt-get update -q
sudo apt-get install -y -q --no-install-recommends clang-18
