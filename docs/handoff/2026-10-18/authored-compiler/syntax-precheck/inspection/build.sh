#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd -- "$(dirname -- "$0")" && pwd)
BASE=$(cd -- "$HERE/.." && pwd)
cd "$BASE"
sha256sum --check --quiet frozen.sha256
sha256sum --check --quiet toolchain.sha256
mkdir -p "$HERE/build"
sha256sum /usr/bin/x86_64-linux-gnu-gcc-14
TIMEFORMAT='Build wall time: %3R seconds; target execution excluded.'
time /usr/bin/gcc -std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
    -O0 -g0 -ffunction-sections -fdata-sections \
    -I"$BASE/reference/src/language/src" \
    -I"$BASE/reference/tree-sitter/lib/include" \
    -I"$BASE/reference/tree-sitter/lib/src" \
    -I"$BASE/reference/third_party/pico_headers" \
    "$HERE/test/language/main.c" \
    "$BASE/reference/src/language/src/parser.c" \
    "$BASE/reference/src/language/src/scanner.c" \
    "$BASE/reference/tree-sitter/lib/src/lib.c" \
    -Wl,--gc-sections -o "$HERE/build/test_language"
cd "$HERE"
sha256sum build/test_language > build/target.sha256
cat build/target.sha256
