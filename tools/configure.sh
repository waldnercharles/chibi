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
package=
package_helper="$source_dir/tools/dependency_package.py"
package_enabled=${CHIBI_DEPENDENCY_PACKAGES:-1}
# Tracy's Python module is tooling with an interpreter-specific ABI and a
# local output path consumed by tools/tracy/launch_mcp.sh.
python_bindings=
if [ -f "$build_dir/CMakeCache.txt" ]; then
    python_bindings=$(sed -n 's/^TRACY_CLIENT_PYTHON:[^=]*=//p' "$build_dir/CMakeCache.txt")
fi
for argument do
    case "$argument" in
        -DTRACY_CLIENT_PYTHON=*|-DTRACY_CLIENT_PYTHON:*=*) python_bindings=${argument#*=} ;;
    esac
done
case "$python_bindings" in
    ON|TRUE|1|YES|on|true|yes) package_enabled=0 ;;
esac
if [ "$package_enabled" = 1 ]; then
    package_enabled=$(python3 - "$build_dir" "$@" <<'PY_OVERRIDES'
from pathlib import Path
import re
import sys
build = Path(sys.argv[1]).resolve()
values = {}
cache = build / "CMakeCache.txt"
if cache.is_file():
    for line in cache.read_text().splitlines():
        match = re.fullmatch(r"(FETCHCONTENT_SOURCE_DIR_[^:]+|CHIBI_SJLIT_SOURCE_DIR):[^=]+=(.*)", line)
        if match:
            values[match.group(1)] = match.group(2)
explicit_fetch = False
for argument in sys.argv[2:]:
    match = re.fullmatch(r"-D([^:=]+)(?::[^=]+)?=(.*)", argument)
    if not match:
        continue
    name, value = match.groups()
    if name.startswith("FETCHCONTENT_"):
        explicit_fetch = True
    if name.startswith("FETCHCONTENT_SOURCE_DIR_") or name == "CHIBI_SJLIT_SOURCE_DIR":
        values[name] = value
private = explicit_fetch
for name, value in values.items():
    if not value:
        continue
    if name == "CHIBI_SJLIT_SOURCE_DIR":
        private = True
        continue
    source = Path(value).resolve()
    if not source.is_relative_to(build / "_deps"):
        private = True
print(0 if private else 1)
PY_OVERRIDES
)
fi
if [ "$package_enabled" = 1 ]; then
    package=$(python3 "$package_helper" prepare --source-dir "$source_dir" \
        --build-dir "$build_dir" --profile "$profile" -- "$@")
else
    mkdir -p "$build_dir"
    printf '%s\n' 'unset(CHIBI_DEPENDENCY_PACKAGE CACHE)' > "$build_dir/.chibi-dependency-package.cmake"
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
	printf '%s\n' "$profile" "$source_dir" "$cache_root" "$fingerprint" "$package" "$@"
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
if [ -n "$package" ] || [ "$cache_available" -eq 0 ]; then
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
	[ "$profile" != web ] || platform_option=--web
	python3 "$cache_helper" prepare --source-dir "$source_dir" \
		--build-dir "$build_dir" --cache-root "$cache_root" \
		$tracy_option $platform_option >/dev/null
fi

if [ "$profile" = native ] && [ ! -f "$cache" ] &&
    [ -z "${CMAKE_GENERATOR:-}" ] && command -v ninja >/dev/null 2>&1; then
	configure -S "$source_dir" -B "$build_dir" -G Ninja \
		-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
		-C "$build_dir/.chibi-dependency-package.cmake" "$@"
else
	configure -S "$source_dir" -B "$build_dir" \
		-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
		-C "$build_dir/.chibi-dependency-package.cmake" "$@"
fi
if [ -z "$package" ] && [ "$cache_available" -eq 1 ]; then
	if ! python3 "$cache_helper" publish --source-dir "$source_dir" \
		--build-dir "$build_dir" --cache-root "$cache_root"; then
		echo "warning: could not publish FetchContent source cache" >&2
	fi
fi
signature "$@" > "$candidate"
mv "$candidate" "$stamp"
