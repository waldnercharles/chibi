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
# Hooks are an explicit developer choice: make install-hooks.
# CMake records every loaded listfile. Unchanged inputs skip reconfiguration;
# ordinary compilation remains incremental.
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
files = {cache, build / ".chibi-config"}
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
# The generated dependency list does not include new listfiles.
tracked = subprocess.run(["git", "-C", str(source), "ls-files", "-co", "--exclude-standard", "-z",
                          "--", "*CMakeLists.txt", "*.cmake", "tools/configure*"],
                         check=True, capture_output=True).stdout
files.update(source / os.fsdecode(name) for name in tracked.split(b"\0") if name)
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
ignored = {"PWD", "OLDPWD", "SHLVL", "_", "CHIBI_RUNNER_PROCESS_GROUP", "CHIBI_QUIET_LOG"}
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
cache="$build_dir/CMakeCache.txt"
stamp="$build_dir/.chibi-config"
mkdir -p "$build_dir"
candidate="$stamp.$$"
trap 'rm -f "$candidate"' EXIT HUP INT TERM

signature()
{
	printf '%s\n' "$requested_profile" "$configure_source" "$@"
	cksum "$source_dir/tools/configure.sh"
	if [ -f "$cache" ]; then
		cksum "$cache"
	fi
}

if [ "$profile" = native ] && [ ! -f "$cache" ] &&
    [ -z "${CMAKE_GENERATOR:-}" ] && command -v ninja >/dev/null 2>&1; then
	configure -S "$configure_source" -B "$build_dir" -G Ninja "$@"
else
	configure -S "$configure_source" -B "$build_dir" "$@"
fi
signature "$@" > "$candidate"
mv "$candidate" "$stamp"
save_fast_signature "$@"
