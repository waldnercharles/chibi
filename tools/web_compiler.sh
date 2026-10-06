#!/bin/sh
set -eu

# CMake runs native tools such as reflect during a web build. Sanitize only
# compiler invocations so those host tools keep their native include paths.
unset C_INCLUDE_PATH CPLUS_INCLUDE_PATH CPATH LIBRARY_PATH
unset PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR CMAKE_PREFIX_PATH

if command -v ccache >/dev/null 2>&1; then
	exec ccache "$@"
fi
exec "$@"
