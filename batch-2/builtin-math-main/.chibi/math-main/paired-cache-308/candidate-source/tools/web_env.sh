#!/bin/sh
set -eu

# activate.sh exports native toolchain paths for ordinary CMake builds. Keep
# those host headers, libraries, and package hints out of Emscripten builds.
unset CC CXX C_INCLUDE_PATH CPLUS_INCLUDE_PATH CPATH LIBRARY_PATH
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR CMAKE_PREFIX_PATH

exec "$@"
