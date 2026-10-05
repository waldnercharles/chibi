#!/bin/bash
# Bounded CPU-only compiler/configuration smoke; no project build or rendering.
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd -P)
source "$repo/tools/cloud/activate.sh"
before=$(env | sort)
source "$repo/tools/cloud/activate.sh"
[[ $(env | sort) == "$before" ]] || { echo 'Activation changed on second source' >&2; exit 1; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
if CHIBI_CLOUD_TOOLS="$tmp/missing" "$repo/tools/cloud/bin/start_skill" >"$tmp/missing.log" 2>&1; then
    echo 'Missing setup unexpectedly passed' >&2; exit 1
fi
grep -q 'Run: bash' "$tmp/missing.log"
cat > "$tmp/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 4.2)
project(cloud_preflight C)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
add_executable(smoke main.c)
CMAKE
cat > "$tmp/main.c" <<'C'
#include <stdio.h>
int main(void) { puts("C23 compiler preflight passed"); return 0; }
C
cmake -S "$tmp" -B "$tmp/build" -G Ninja
cmake --build "$tmp/build" --parallel 2
"$tmp/build/smoke"
emcmake cmake --version
"$repo/tools/cloud/bin/start_skill"
printf 'Activation and compiler smoke passed\n'
