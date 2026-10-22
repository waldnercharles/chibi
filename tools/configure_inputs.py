#!/usr/bin/env python3
"""Transitional read-only native configuration input probes."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def cache_values(build: Path) -> dict[str, str]:
    values = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        match = re.fullmatch(r"([^:#=]+):[^=]+=(.*)", line)
        if match:
            values[match[1]] = match[2]
    return values


def signature(source: Path, build: Path, profile: str, program: Path,
              arguments: list[str]) -> str:
    if profile not in ("native", "tool-host"):
        raise ValueError("expected native or tool-host profile")
    values = cache_values(build)
    cache = build / "CMakeCache.txt"
    files = {cache, build / ".chibi-config", program, program.parent / "configure_inputs.py",
             Path(__file__).resolve()}
    generator = values.get("CMAKE_GENERATOR")
    if generator == "Ninja":
        rules = build / "build.ninja"
        line = next((line for line in rules.read_text().splitlines()
                     if ": RERUN_CMAKE " in line), None)
        if line is None:
            raise ValueError("Ninja configuration inputs are missing")
        inputs = line.split(": RERUN_CMAKE ", 1)[1].replace("$ ", "\0")
        names = [name.replace("\0", " ").replace("$$", "$")
                 for name in inputs.split() if name != "|"]
    elif generator == "Unix Makefiles":
        rules = build / "CMakeFiles/Makefile.cmake"
        match = re.search(r"set\(CMAKE_MAKEFILE_DEPENDS\s+(.*?)\n\s*\)",
                          rules.read_text(), re.S)
        if not match:
            raise ValueError("Make configuration inputs are missing")
        names = re.findall(r'"([^"\n]+)"', match[1])
    else:
        raise ValueError("unsupported configuration generator")
    files.add(rules)
    dependencies = {Path(name) if Path(name).is_absolute() else build / name
                    for name in names}
    if any(not path.is_file() for path in dependencies):
        raise ValueError("a loaded configuration input is missing")
    files.update(dependencies)
    tracked = subprocess.run([
        "git", "-C", str(source), "ls-files", "-co", "--exclude-standard", "-z",
        "--", "*CMakeLists.txt", "*.cmake", "tools/configure*",
    ], check=True, capture_output=True).stdout
    files.update(source / os.fsdecode(name) for name in tracked.split(b"\0") if name)
    for name, value in values.items():
        if re.fullmatch(r"CMAKE_(?:C|CXX|OBJC|OBJCXX)_COMPILER(?:_LAUNCHER)?|"
                        r"CMAKE_(?:AR|RANLIB|LINKER|MAKE_PROGRAM)", name):
            for command in value.split(";"):
                executable = shutil.which(command)
                if executable:
                    files.add(Path(executable))
    for name in ("cmake", "ninja", "make", "cc", "c++", "python3", "emcmake",
                 "emcc", "em++", "xcrun"):
        executable = shutil.which(name)
        if executable:
            path = Path(executable).resolve()
            files.add(path)
            if name in ("emcc", "em++", "emcmake"):
                files.update(path.parent.glob("*.py"))
                files.update((path.parent / "tools").glob("*.py"))
                files.add(path.parent / "emscripten-version.txt")
    for name in ("CMAKE_TOOLCHAIN_FILE", "CMAKE_OSX_SYSROOT"):
        value = values.get(name) or os.environ.get(
            "SDKROOT" if name == "CMAKE_OSX_SYSROOT" else name)
        if value:
            path = Path(value).resolve()
            files.add(path)
            if path.is_dir():
                files.update(path / name for name in ("SDKSettings.json", "SDKSettings.plist"))
    if sys.platform == "darwin":
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], check=True,
                             capture_output=True, text=True).stdout.strip()
        if not sdk:
            raise ValueError("selected SDK path is empty")
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
    value = [[profile, str(program), *arguments], str(source), str(build), metadata, environment]
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def cmake_arguments(arguments: list[str], index: int) -> list[str]:
    if len(arguments) <= index or arguments[index] != "--":
        raise ValueError("expected -- before CMake arguments")
    return arguments[index + 1:]


def main(arguments: list[str]) -> int:
    try:
        if arguments and arguments[0] == "signature" and len(arguments) >= 6:
            source, build, program = (Path(arguments[index]).resolve() for index in (1, 2, 4))
            print(signature(source, build, arguments[3], program, cmake_arguments(arguments, 5)))
        else:
            raise ValueError("expected signature")
    except (OSError, UnicodeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"configure inputs: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
