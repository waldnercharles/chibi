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
# Match Makefile's expansion of the toolchain flag string into configure args.
cat > "$tmp/configure.mk" <<'MAKE'
configure:
	cmake -S "$(SOURCE)" -B "$(SOURCE)/build" -G Ninja $(NATIVE_TOOLCHAIN_FLAGS)
MAKE
make -f "$tmp/configure.mk" SOURCE="$tmp" configure
[[ $(sed -n 's/^LIBCLANG_ROOT:UNINITIALIZED=//p' "$tmp/build/CMakeCache.txt") == "${CHIBI_CLOUD_TOOLS:-/workspace/.cloud-tools}/root/usr/lib/llvm-19" ]]
cmake --build "$tmp/build" --parallel 2
"$tmp/build/smoke"
cat > "$tmp/main.cpp" <<'CPP'
#include <iostream>
int main() { std::cout << "C++ compiler preflight passed\n"; }
CPP
"$CXX" "$tmp/main.cpp" -o "$tmp/cxx"
"$tmp/cxx"
"$CC" -fsanitize=address,undefined "$tmp/main.c" -o "$tmp/sanitizers"
"$tmp/sanitizers"
# Compile AND execute optimized WebAssembly: exercise LLVM, wasm-ld and Binaryen.
EMCC_CORES=2 emcc "$tmp/main.c" -O2 -sENVIRONMENT=node -sSINGLE_FILE=1 -o "$tmp/smoke.js"
node "$tmp/smoke.js"
"$repo/tools/cloud/bin/start_skill"
printf 'Activation and compiler smoke passed\n'
