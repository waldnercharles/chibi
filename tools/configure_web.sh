#!/bin/sh
set -eu

if [ -n "${CHIBI_NATIVE_BUILD_DIR:-}" ] &&
   [ -f "${CHIBI_NATIVE_BUILD_DIR}/CMakeCache.txt" ]; then
    native_libclang_root=$(sed -n 's/^LIBCLANG_ROOT:PATH=//p' \
        "${CHIBI_NATIVE_BUILD_DIR}/CMakeCache.txt")
    if [ -n "$native_libclang_root" ]; then
        set -- "$@" "-DLIBCLANG_ROOT=$native_libclang_root"
    fi
fi

exec sh "$(dirname "$0")/configure.sh" web "$@"
