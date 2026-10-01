#!/bin/sh
set -eu

profile=$1
build_dir=$2
shift 2
case "$profile" in
	native|web) ;;
	*) printf 'unknown build profile: %s\n' "$profile" >&2; exit 2 ;;
esac

configure()
{
	if [ "$profile" = web ]; then
		emcmake cmake "$@"
	else
		cmake "$@"
	fi
}
source_dir=$(pwd -P)
# Commits run the clang-format CI checks (.githooks/pre-commit).
if [ -z "$(git -C "$source_dir" config --get core.hooksPath 2>/dev/null)" ]; then
	git -C "$source_dir" config core.hooksPath .githooks 2>/dev/null || :
fi
package_prefix=
if [ "$profile" = native ] && [ -f "$source_dir/tools/sdl_package.sh" ]; then
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

signature()
{
	printf '%s\n' "$profile" "$source_dir" "$cache_root" "$fingerprint" "$package_prefix" "$@"
	cksum "$source_dir/tools/configure.sh" "$cache_helper"
	if [ -f "$cache" ]; then
		cksum "$cache"
	fi
}

signature "$@" > "$candidate"
if [ -f "$cache" ] && cmp -s "$candidate" "$stamp"; then
	exit 0
fi

# Refresh only private source overrides before reconfiguring. The helper
# leaves explicitly selected external dependency checkouts to the caller.
if [ "$cache_available" -eq 0 ]; then
	: > "$build_dir/.chibi-fetchcontent-cache.cmake"
else
	tracy_option=
	for argument do
		case "$argument" in
			-DCHIBI_ENABLE_TRACY=ON|-DCHIBI_ENABLE_TRACY:BOOL=ON|-DCHIBI_ENABLE_TRACY:BOOL=TRUE|-DCHIBI_ENABLE_TRACY:BOOL=1)
				tracy_option=--tracy ;;
		esac
	done
	platform_option=
	sdl_option=
	[ "$profile" != web ] || platform_option=--web
	[ -z "$package_prefix" ] || sdl_option=--no-sdl
	python3 "$cache_helper" prepare --source-dir "$source_dir" \
		--build-dir "$build_dir" --cache-root "$cache_root" \
		$tracy_option $platform_option $sdl_option >/dev/null
fi

if [ "$profile" = native ] && [ ! -f "$cache" ] &&
    [ -z "${CMAKE_GENERATOR:-}" ] && command -v ninja >/dev/null 2>&1; then
	configure -S "$source_dir" -B "$build_dir" -G Ninja \
		-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
		"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
else
	configure -S "$source_dir" -B "$build_dir" \
		-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
		"-DCHIBI_SDL3_PACKAGE_PREFIX=$package_prefix" "$@"
fi
if [ "$cache_available" -eq 1 ]; then
	if ! python3 "$cache_helper" publish --source-dir "$source_dir" \
		--build-dir "$build_dir" --cache-root "$cache_root"; then
		echo "warning: could not publish FetchContent source cache" >&2
	fi
fi
signature "$@" > "$candidate"
mv "$candidate" "$stamp"
