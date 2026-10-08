#!/bin/sh
set -eu

requested_profile=$1
profile=$requested_profile
source_subdir=.
build_dir=$2
shift 2
case "$profile" in
	native|web) ;;
	sdk-native) profile=native; source_subdir=sdk ;;
	sdk-web) profile=web; source_subdir=sdk ;;
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
configure_source=$source_dir
[ "$source_subdir" = . ] || configure_source="$source_dir/$source_subdir"
if [ -f "$source_dir/.chibi-workspace.json" ]; then
	python3 "$source_dir/tools/workspace_retirement.py" register "$build_dir"
fi
# Commits run the clang-format CI checks (.githooks/pre-commit).
if [ -z "$(git -C "$source_dir" config --get core.hooksPath 2>/dev/null)" ]; then
	git -C "$source_dir" config core.hooksPath .githooks 2>/dev/null || :
fi
# CMake records every loaded listfile. Check those inputs before the more
# expensive package identity probes; ordinary compilation remains incremental.
fast_stamp="$build_dir/.chibi-config-inputs"
fast_signature()
{
    python3 - "$source_dir" "$build_dir" "$requested_profile" "$@" <<'PY_INPUTS'
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

source, build = (Path(value).resolve() for value in sys.argv[1:3])
cache = build / "CMakeCache.txt"
if not cache.is_file():
    sys.exit(1)
values = {}
for line in cache.read_text().splitlines():
    match = re.fullmatch(r"([^:#=]+):[^=]+=(.*)", line)
    if match:
        values[match[1]] = match[2]
files = {cache, build / ".chibi-config", build / ".chibi-dependency-package.cmake",
         build / ".chibi-fetchcontent-cache.cmake"}
generator = values.get("CMAKE_GENERATOR")
if generator == "Ninja":
    rules = build / "build.ninja"
    line = next(line for line in rules.read_text().splitlines() if ": RERUN_CMAKE " in line)
    inputs = line.split(": RERUN_CMAKE ", 1)[1].replace("$ ", "\0")
    names = [name.replace("\0", " ").replace("$$", "$") for name in inputs.split() if name != "|"]
elif generator == "Unix Makefiles":
    rules = build / "CMakeFiles/Makefile.cmake"
    match = re.search(r"set\(CMAKE_MAKEFILE_DEPENDS\s+(.*?)\n\s*\)", rules.read_text(), re.S)
    if not match:
        sys.exit(1)
    names = re.findall(r'"([^"\n]+)"', match[1])
else:
    sys.exit(1)
files.add(rules)
dependencies = {Path(name) if Path(name).is_absolute() else build / name for name in names}
if any(not path.is_file() for path in dependencies):
    sys.exit(1)
files.update(dependencies)
# The generated dependency list does not include new listfiles or the vendored
# dependency sources from which immutable packages are selected.
tracked = subprocess.run(["git", "-C", str(source), "ls-files", "-co", "--exclude-standard", "-z",
                          "--", "*CMakeLists.txt", "*.cmake", "cmake/patches/*", "tools/configure*",
                          "tools/dependency_package.py", "tools/fetchcontent_cache.py"],
                         check=True, capture_output=True).stdout
files.update(source / os.fsdecode(name) for name in tracked.split(b"\0") if name)
for directory in ("third_party/ckdl", "third_party/fmt", "third_party/yyjson"):
    files.update(path for path in (source / directory).rglob("*") if path.is_file())
package = values.get("CHIBI_DEPENDENCY_PACKAGE")
if package:
    export = Path(package)
    marker = export.parent.parent / "complete.json"
    if not export.is_file() or not marker.is_file():
        sys.exit(1)
    files.update((export, marker))
for name, value in values.items():
    if re.fullmatch(r"CMAKE_(?:C|CXX|OBJC|OBJCXX)_COMPILER(?:_LAUNCHER)?|CMAKE_(?:AR|RANLIB|LINKER|MAKE_PROGRAM)", name):
        for command in value.split(";"):
            executable = shutil.which(command)
            if executable:
                files.add(Path(executable))
for name in ("cmake", "ninja", "make", "cc", "c++", "python3", "emcmake", "emcc", "em++", "xcrun"):
    executable = shutil.which(name)
    if executable:
        path = Path(executable).resolve()
        files.add(path)
        if name in ("emcc", "em++", "emcmake"):
            files.update(child for child in path.parent.glob("*.py"))
            files.update(child for child in (path.parent / "tools").glob("*.py"))
            files.add(path.parent / "emscripten-version.txt")
for name in ("CMAKE_TOOLCHAIN_FILE", "CMAKE_OSX_SYSROOT"):
    value = values.get(name) or os.environ.get("SDKROOT" if name == "CMAKE_OSX_SYSROOT" else name)
    if value:
        path = Path(value).resolve()
        files.add(path)
        if path.is_dir():
            files.update(path / name for name in ("SDKSettings.json", "SDKSettings.plist"))
if sys.platform == "darwin":
    # xcode-select can change the implicit SDK without changing the cache or
    # environment. Its selected path is part of this input snapshot too.
    sdk = subprocess.run(["xcrun", "--show-sdk-path"], check=True,
                         capture_output=True, text=True).stdout.strip()
    path = Path(sdk).resolve()
    files.add(path)
    files.update(path / name for name in ("SDKSettings.json", "SDKSettings.plist"))
files.add(Path(os.environ.get("EM_CONFIG", str(Path.home() / ".emscripten"))))
metadata = []
for path in sorted(files):
    try:
        stat = path.stat()
        metadata.append((str(path), str(path.resolve()), stat.st_dev, stat.st_ino,
                         stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns))
    except FileNotFoundError:
        metadata.append((str(path), None))
ignored = {"PWD", "OLDPWD", "SHLVL", "_", "CHIBI_WORKSPACE_LOCK_FD",
           "CHIBI_TEST_DEADLINE", "CHIBI_TEST_PROCESS_GROUP", "CHIBI_QUIET_LOG"}
environment = {name: value for name, value in os.environ.items() if name not in ignored}
value = [sys.argv[3:], str(source), str(build), metadata, environment]
print(hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest())
PY_INPUTS
}
if fast_input=$(fast_signature "$@" 2>/dev/null) &&
   [ -f "$fast_stamp" ] && [ "$(cat "$fast_stamp")" = "$fast_input" ]; then
    exit 0
fi
save_fast_signature()
{
    if fast_input=$(fast_signature "$@" 2>/dev/null); then
        printf '%s\n' "$fast_input" > "$fast_stamp.$$"
        mv "$fast_stamp.$$" "$fast_stamp"
    fi
}
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
    printf '%s\n' 'unset(CHIBI_DEPENDENCY_PACKAGE CACHE)' \
        'unset(CHIBI_DEPENDENCY_SOURCE_ROOT CACHE)' > "$build_dir/.chibi-dependency-package.cmake"
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
	printf '%s\n' "$requested_profile" "$configure_source" "$cache_root" "$fingerprint" "$package" "$@"
	cksum "$source_dir/tools/configure.sh" "$cache_helper"
	if [ -f "$cache" ]; then
		cksum "$cache"
	fi
}

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
	configure -S "$configure_source" -B "$build_dir" -G Ninja \
		-C "$build_dir/.chibi-fetchcontent-cache.cmake" \
		-C "$build_dir/.chibi-dependency-package.cmake" "$@"
else
	configure -S "$configure_source" -B "$build_dir" \
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
save_fast_signature "$@"
