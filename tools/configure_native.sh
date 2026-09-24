#!/bin/sh
set -eu

build_dir=$1
shift
source_dir=$(pwd -P)
# Commits run the clang-format CI checks (.githooks/pre-commit).
if [ -z "$(git -C "$source_dir" config --get core.hooksPath 2>/dev/null)" ]; then
	git -C "$source_dir" config core.hooksPath .githooks 2>/dev/null || :
fi
package_prefix=
if [ -f "$source_dir/tools/sdl_package.sh" ]; then
	package_prefix=$(sh "$source_dir/tools/sdl_package.sh" path "$@" 2>/dev/null) || package_prefix=
fi
cache_root=${CHIBI_FETCHCONTENT_CACHE_DIR:-}
cache_available=1
if [ -z "$cache_root" ]; then
	if [ -n "${XDG_CACHE_HOME:-}" ]; then
		cache_root="$XDG_CACHE_HOME/chibi-fetchcontent"
	elif [ -n "${HOME:-}" ]; then
		cache_root="$HOME/.cache/chibi-fetchcontent"
	else
		cache_available=0
	fi
fi
if [ "$cache_available" -eq 1 ]; then
	if ! command -v python3 >/dev/null 2>&1 ||
		! mkdir -p "$cache_root" 2>/dev/null || [ ! -w "$cache_root" ]; then
		cache_available=0
	else
		cache_root=$(cd "$cache_root" && pwd -P)
	fi
fi
if [ "$cache_available" -eq 0 ]; then
	cache_root=disabled
fi
cache="$build_dir/CMakeCache.txt"
stamp="$build_dir/.chibi-config"
mkdir -p "$build_dir"
candidate="$stamp.$$"
trap 'rm -f "$candidate"' EXIT HUP INT TERM
cache_helper="$source_dir/tools/fetchcontent_cache.py"
fingerprint=disabled
if [ "$cache_available" -eq 1 ]; then
	if ! fingerprint=$(python3 "$cache_helper" fingerprint --source-dir "$source_dir" 2>/dev/null); then
		cache_available=0
		cache_root=disabled
		fingerprint=disabled
	fi
fi
fresh=0
if [ ! -f "$cache" ]; then
	fresh=1
	tracy=0
	for argument do
		case "$argument" in
			-DCHIBI_ENABLE_TRACY=ON|-DCHIBI_ENABLE_TRACY:BOOL=ON|-DCHIBI_ENABLE_TRACY:BOOL=TRUE|-DCHIBI_ENABLE_TRACY:BOOL=1)
				tracy=1
				;;
		esac
	done
	sdl_cache_option=
	if [ -n "$package_prefix" ]; then
		sdl_cache_option=--no-sdl
	fi
	if [ "$cache_available" -eq 0 ]; then
		: > "$build_dir/.chibi-fetchcontent-cache.cmake"
	elif [ "$tracy" -eq 1 ]; then
		python3 "$cache_helper" prepare --source-dir "$source_dir" \
			--build-dir "$build_dir" --cache-root "$cache_root" --tracy $sdl_cache_option >/dev/null
	else
		python3 "$cache_helper" prepare --source-dir "$source_dir" \
			--build-dir "$build_dir" --cache-root "$cache_root" $sdl_cache_option >/dev/null
	fi
fi

signature()
{
	printf '%s\n' "$source_dir" "$cache_root" "$fingerprint" "$package_prefix" "$@"
	if [ -f "$cache" ]; then
		cksum "$cache"
	fi
}

signature "$@" > "$candidate"
if [ -f "$cache" ] && cmp -s "$candidate" "$stamp"; then
	exit 0
fi

if [ ! -f "$cache" ] && [ -z "${CMAKE_GENERATOR:-}" ] && command -v ninja >/dev/null 2>&1; then
	if [ "$fresh" -eq 1 ]; then
		cmake -S "$source_dir" -B "$build_dir" -G Ninja \
			-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
			"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
	else
		cmake -S "$source_dir" -B "$build_dir" -G Ninja \
			"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
	fi
else
	if [ "$fresh" -eq 1 ]; then
		cmake -S "$source_dir" -B "$build_dir" \
			-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
			"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
	else
		cmake -S "$source_dir" -B "$build_dir" \
			"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
	fi
fi
if [ "$cache_available" -eq 1 ]; then
	if ! python3 "$cache_helper" publish --source-dir "$source_dir" \
		--build-dir "$build_dir" --cache-root "$cache_root"; then
		echo "warning: could not publish FetchContent source cache" >&2
	fi
fi
signature "$@" > "$candidate"
mv "$candidate" "$stamp"
