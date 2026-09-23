#!/bin/sh
# Build the pinned SDL package once per macOS toolchain for all worktrees.
set -eu

action=${1:-}
shift || true
[ "$(uname -s)" = Darwin ] || exit 1

version=3.4.0
sha256=9ac2debb493e0d3e13dbd2729fb91f4bfeb00a0f4dff5e04b73cc9bac276b38d
root=${CHIBI_SDL3_PACKAGE_ROOT:-${XDG_CACHE_HOME:-${HOME}/.cache}/chibi-sdl3}
compiler=${CC:-cc}
sysroot=${CMAKE_OSX_SYSROOT:-}
deployment=${MACOSX_DEPLOYMENT_TARGET:-}
for argument do
    case "$argument" in
        -DCMAKE_C_COMPILER=*) compiler=${argument#*=} ;;
        -DCMAKE_C_COMPILER:*=*) compiler=${argument#*=} ;;
        -DCMAKE_OSX_SYSROOT=*) sysroot=${argument#*=} ;;
        -DCMAKE_OSX_SYSROOT:*=*) sysroot=${argument#*=} ;;
        -DCMAKE_OSX_DEPLOYMENT_TARGET=*) deployment=${argument#*=} ;;
        -DCMAKE_OSX_DEPLOYMENT_TARGET:*=*) deployment=${argument#*=} ;;
        -DCMAKE_TOOLCHAIN_FILE=*|-DCMAKE_TOOLCHAIN_FILE:*=*|-DCMAKE_OSX_ARCHITECTURES=*|-DCMAKE_OSX_ARCHITECTURES:*=*|-DCMAKE_C_FLAGS=*|-DCMAKE_C_FLAGS:*=*|-DCMAKE_OBJC_FLAGS=*|-DCMAKE_OBJC_FLAGS:*=*) exit 1 ;;
    esac
done
compiler=$(command -v "$compiler") || exit 1
compiler=$(realpath "$compiler")
cmake_path=$(command -v cmake) || exit 1
cmake_path=$(realpath "$cmake_path")
if [ -z "$sysroot" ]; then
    sysroot=$(xcrun --sdk macosx --show-sdk-path)
fi
sdk_version=$(xcrun --sdk macosx --show-sdk-version)
identity=$(printf '%s\n' "$version" "$sha256" "$(shasum -a 256 "$0" | awk '{print $1}')" \
    "$(uname -m)" "$(sw_vers -productVersion)" \
    "$compiler" "$(stat -f '%m:%z:%i' "$compiler")" "$("$compiler" --version | head -1)" \
    "$cmake_path" "$(stat -f '%m:%z:%i' "$cmake_path")" "$sysroot" "$sdk_version" "$deployment" \
    "${CFLAGS:-}" "${OBJCFLAGS:-}" "${CPPFLAGS:-}" "${LDFLAGS:-}" \
    "${PKG_CONFIG_PATH:-}" "${PKG_CONFIG_LIBDIR:-}" | shasum -a 256)
key=${identity%% *}
entry=$root/$version/$key
prefix=$entry/install
config=$prefix/lib/cmake/SDL3/SDL3Config.cmake

if [ -f "$entry/.complete" ] && [ -f "$config" ] && [ -f "$prefix/lib/libSDL3.a" ] && [ -f "$prefix/lib/libSDL3.dylib" ]; then
    printf '%s\n' "$prefix"
    exit 0
fi
[ "$action" = build ] || exit 1

archive_dir=$root/archives
archive=$archive_dir/SDL3-$version.zip
mkdir -p "$archive_dir" "$entry"
if [ ! -f "$archive" ] || [ "$(shasum -a 256 "$archive" | awk '{print $1}')" != "$sha256" ]; then
    for candidate in build-release/_deps/sdl3-tmp/SDL3-$version.zip build-dev/_deps/sdl3-tmp/SDL3-$version.zip build/_deps/sdl3-tmp/SDL3-$version.zip; do
        if [ -f "$candidate" ] && [ "$(shasum -a 256 "$candidate" | awk '{print $1}')" = "$sha256" ]; then
            cp "$candidate" "$archive"
            break
        fi
    done
fi
if [ ! -f "$archive" ] || [ "$(shasum -a 256 "$archive" | awk '{print $1}')" != "$sha256" ]; then
    temporary=$archive.$$
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    curl -fL --retry 3 "https://github.com/libsdl-org/SDL/releases/download/release-$version/SDL3-$version.zip" -o "$temporary"
    [ "$(shasum -a 256 "$temporary" | awk '{print $1}')" = "$sha256" ] || {
        printf '%s\n' 'SDL archive SHA-256 mismatch' >&2
        exit 1
    }
    mv "$temporary" "$archive"
fi
if [ ! -f "$entry/SDL3-$version/CMakeLists.txt" ]; then
    (cd "$entry" && cmake -E tar xvf "$archive" >/dev/null)
fi
printf 'Building pinned SDL %s package for %s\n' "$version" "$compiler" >&2
cmake -S "$entry/SDL3-$version" -B "$entry/build" -G Ninja \
    -DCMAKE_C_COMPILER="$compiler" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_OSX_SYSROOT="$sysroot" -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DSDL_STATIC=ON -DSDL_SHARED=ON \
    -DSDL_INSTALL=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_TESTS=OFF \
    -DSDL_CCACHE=ON > "$entry/configure.log" 2>&1 || {
        tail -40 "$entry/configure.log" >&2
        exit 1
    }
cmake --build "$entry/build" --target SDL3-static SDL3-shared \
    --parallel "${CHIBI_SDL3_PACKAGE_JOBS:-8}" > "$entry/build.log" 2>&1 || {
        tail -40 "$entry/build.log" >&2
        exit 1
    }
cmake --install "$entry/build" --prefix "$prefix" > "$entry/install.log" 2>&1 || {
    tail -40 "$entry/install.log" >&2
    exit 1
}
[ -f "$config" ] && [ -f "$prefix/lib/libSDL3.a" ] && [ -f "$prefix/lib/libSDL3.dylib" ]
: > "$entry/.complete"
printf '%s\n' "$prefix"
