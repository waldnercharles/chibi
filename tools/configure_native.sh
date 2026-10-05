#!/bin/sh
set -eu
build_dir=$1
shift
# Match the native CI compiler on fresh Linux builds. Explicit toolchains,
# environment selections, and existing CMake caches remain authoritative.
if [ "$(uname -s)" = Linux ] && [ ! -f "$build_dir/CMakeCache.txt" ] &&
    [ -z "${CC:-}${CXX:-}${CMAKE_TOOLCHAIN_FILE:-}" ]; then
    selected=0
    definition=0
    for argument do
        if [ "$definition" -eq 1 ]; then
            argument="-D$argument"
            definition=0
        fi
        case "$argument" in
            -D) definition=1 ;;
            --toolchain|--toolchain=*|-C*) selected=1 ;;
            -DCMAKE_C_COMPILER=*|-DCMAKE_C_COMPILER:*=*|\
            -DCMAKE_CXX_COMPILER=*|-DCMAKE_CXX_COMPILER:*=*|\
            -DCMAKE_TOOLCHAIN_FILE=*|-DCMAKE_TOOLCHAIN_FILE:*=*) selected=1 ;;
        esac
    done
    if [ "$selected" -eq 0 ] && command -v clang >/dev/null 2>&1 &&
        command -v clang++ >/dev/null 2>&1; then
        set -- "$@" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
    fi
fi
# Keep framework headers and compiler system headers on the same selected SDK.
sdk=${SDKROOT:-}
compiler=${CC:-cc}
if [ -f "$build_dir/CMakeCache.txt" ]; then
    cached_sdk=$(sed -n 's/^CMAKE_OSX_SYSROOT:[^=]*=//p' "$build_dir/CMakeCache.txt")
    [ -z "$cached_sdk" ] || sdk=$cached_sdk
    cached_compiler=$(sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' "$build_dir/CMakeCache.txt")
    [ -z "$cached_compiler" ] || compiler=$cached_compiler
fi
for argument do
    case "$argument" in
        -DCMAKE_OSX_SYSROOT=*|-DCMAKE_OSX_SYSROOT:*=*) sdk=${argument#*=} ;;
        -DCMAKE_C_COMPILER=*|-DCMAKE_C_COMPILER:*=*) compiler=${argument#*=} ;;
    esac
done
if [ "$(uname -s)" = Darwin ] && [ -z "$sdk" ]; then
    sdk=$(python3 - "$compiler" <<'PY_SDK'
from pathlib import Path
import shlex
import subprocess
import sys
try:
    command = [sys.argv[1]] if Path(sys.argv[1]).is_file() else shlex.split(sys.argv[1])
    result = subprocess.run([*command, "-###", "-x", "c", "-fsyntax-only", "-"],
                            input="", text=True, capture_output=True)
    for line in result.stderr.splitlines():
        arguments = shlex.split(line)
        if "-isysroot" in arguments:
            sdk = Path(arguments[arguments.index("-isysroot") + 1])
            if sdk.is_dir():
                print(sdk.resolve())
                break
except (OSError, ValueError):
    pass
PY_SDK
)
    [ -n "$sdk" ] || sdk=$(xcrun --show-sdk-path)
    set -- "$@" "-DCMAKE_OSX_SYSROOT=$sdk"
fi
exec sh "$(dirname "$0")/configure.sh" native "$build_dir" "$@"
