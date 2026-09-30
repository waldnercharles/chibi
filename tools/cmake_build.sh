#!/bin/sh
# Builds a configured CMake tree: tools/cmake_build.sh BUILD_DIR [cmake --build options]
# Ninja trees build without progress lines unless VERBOSE is set. Compiler
# warnings, command output and each failing command with its output still print.
set -eu

build_dir=$1
shift
if [ -z "${VERBOSE:-}" ] &&
	grep -qx 'CMAKE_GENERATOR:INTERNAL=Ninja' "$build_dir/CMakeCache.txt" 2>/dev/null; then
	exec cmake --build "$build_dir" "$@" -- --quiet
fi
exec cmake --build "$build_dir" "$@"
