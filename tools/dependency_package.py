#!/usr/bin/env python3
"""Build immutable dependency packages shared by independent worktrees."""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

import fetchcontent_cache

SCHEMA = 1
ENVIRONMENT = ("CC", "CXX", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "SDKROOT",
               "MACOSX_DEPLOYMENT_TARGET", "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM",
               "CMAKE_GENERATOR_TOOLSET", "EMSDK", "EMSDK_NODE", "EM_CACHE", "EMCC_CFLAGS",
               "OBJC", "OBJCXX", "OBJCFLAGS", "OBJCXXFLAGS", "DEVELOPER_DIR",
               "CMAKE_PREFIX_PATH", "CMAKE_INCLUDE_PATH", "CMAKE_LIBRARY_PATH",
               "CMAKE_FRAMEWORK_PATH", "CMAKE_APPBUNDLE_PATH", "CPATH", "C_INCLUDE_PATH",
               "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "LIBRARY_PATH",
               "PKG_CONFIG_PATH", "PKG_CONFIG_LIBDIR", "PKG_CONFIG_SYSROOT_DIR",
               "CMAKE_TOOLCHAIN_FILE", "EMMAKEN_CMAKE_TOOLCHAIN")


def cache_root_from_env() -> Path:
    value = os.environ.get("CHIBI_DEPENDENCY_PACKAGE_CACHE_DIR")
    if value:
        return Path(value).resolve()
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    return Path(base).resolve() / "chibi-dependency-packages"


PACKAGE_CMAKE = ("dependencies.cmake", "enkits.cmake", "Findcrypto.cmake", "apply_patch.cmake", "rmlui_dependencies.cmake",
                 "zep_dependencies.cmake", "tree_sitter.cmake", "ckdl.cmake", "thirdparty.cmake",
                 "dependency_package/CMakeLists.txt")
APP_OPTIONS = {"CHIBI_REFLECT_EXECUTABLE", "CHIBI_COLLAB_URL", "CHIBI_DISTRIBUTION",
               "CHIBI_ENABLE_WARNINGS", "LIBCLANG_ROOT", "CMAKE_EXPORT_COMPILE_COMMANDS",
               "CHIBI_SDK_BUILD_COMPILER", "CHIBI_SDK_BUILD_NATIVE", "CHIBI_SDK_BUILD_DESKTOP"}


def source_files(source: Path) -> list[Path]:
    paths = [source / "cmake" / name for name in PACKAGE_CMAKE
             if (source / "cmake" / name).is_file()]
    paths.extend((source / "cmake/patches").glob("*.patch"))
    for name in ("third_party/ckdl", "third_party/fmt", "third_party/yyjson"):
        paths.extend(path for path in (source / name).rglob("*") if path.is_file())
    return sorted(paths)


BOOL_DEFAULTS = {"CHIBI_WEB_THREADS": "OFF",
                 "CHIBI_ENABLE_TRACY": "OFF", "CHIBI_ENABLE_SANITIZERS": "ON",
                 "CHIBI_ENABLE_CCACHE": "ON", "CHIBI_ENABLE_SJLIT": "ON"}


def canonical_bool(value: str) -> str:
    upper = value.upper()
    if upper in ("1", "ON", "YES", "TRUE", "Y"):
        return "ON"
    if upper in ("0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND", "") or upper.endswith("-NOTFOUND"):
        return "OFF"
    return value


def web_toolchain() -> Path:
    result = subprocess.run(["emcmake", "cmake", "--version"], capture_output=True,
                            text=True, check=True)
    for line in result.stderr.splitlines():
        for argument in shlex.split(line):
            prefix = "-DCMAKE_TOOLCHAIN_FILE="
            if argument.startswith(prefix):
                path = Path(argument[len(prefix):]).resolve()
                if path.is_file():
                    return path
    raise RuntimeError("cannot discover selected emcmake toolchain")


def web_compilers(toolchain: Path) -> dict[str, Path]:
    # The canonical Emscripten platform module is <sdk>/cmake/Modules/Platform/Emscripten.cmake.
    root = toolchain.parents[3]
    compilers = {"C": root / "emcc", "CXX": root / "em++"}
    if not all(path.is_file() for path in compilers.values()):
        raise RuntimeError("selected Emscripten toolchain has no compiler drivers")
    return compilers


def package_arguments(arguments: list[str], profile: str) -> list[str]:
    options = definitions(arguments)
    if os.environ.get("CMAKE_TOOLCHAIN_FILE"):
        options.setdefault("CMAKE_TOOLCHAIN_FILE", os.environ["CMAKE_TOOLCHAIN_FILE"])
    if profile == "web":
        selected = web_toolchain()
        configured = options.get("CMAKE_TOOLCHAIN_FILE")
        if configured and Path(configured).resolve() == selected:
            options.pop("CMAKE_TOOLCHAIN_FILE")
        for language, compiler in web_compilers(selected).items():
            configured = options.get(f"CMAKE_{language}_COMPILER")
            if configured and Path(configured).resolve() == compiler.resolve():
                options.pop(f"CMAKE_{language}_COMPILER")
    for name in APP_OPTIONS:
        options.pop(name, None)
    for name in BOOL_DEFAULTS:
        if name in options:
            options[name] = canonical_bool(options[name])
    threaded = options.get("CHIBI_WEB_THREADS", "OFF") == "ON"
    if profile == "native" or not threaded or options.get("CHIBI_WEB_WORKERS") == "4":
        options.pop("CHIBI_WEB_WORKERS", None)
    if profile == "web":
        # The web dependency project forces SLJIT off regardless of a consumer's value.
        options.pop("CHIBI_ENABLE_SJLIT", None)
    for name, default in BOOL_DEFAULTS.items():
        if options.get(name) == default:
            options.pop(name)
    if options.get("CHIBI_SANITIZERS") == "address,undefined":
        options.pop("CHIBI_SANITIZERS")
    # Dependencies are always built as Release so every consumer build type shares them.
    for name in list(options):
        if name == "CMAKE_BUILD_TYPE" or re.fullmatch(r"CMAKE_\w+_FLAGS_[A-Z_]+", name):
            options.pop(name)
        elif re.fullmatch(r"CMAKE_\w+_FLAGS", name):
            options[name] = re.sub(r"\s*-fdebug-(?:compilation-dir|prefix-map)=\S*", "", options[name]).strip()
            if not options[name]:
                options.pop(name)
    if sys.platform == "darwin" and profile == "native":
        options["CMAKE_OSX_SYSROOT"] = sdk_path(options)
    return [f"-D{name}={value}" for name, value in sorted(options.items())]


def sdk_path(options: dict[str, str]) -> str:
    sysroot = options.get("CMAKE_OSX_SYSROOT") or os.environ.get("SDKROOT")
    if sysroot:
        return sysroot
    return subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True,
                          check=True).stdout.strip()


DEPENDENCY_OPTIONS = {"CMAKE_OSX_SYSROOT", "CMAKE_OSX_ARCHITECTURES",
                      "CMAKE_OSX_DEPLOYMENT_TARGET", "CMAKE_TOOLCHAIN_FILE",
                      "CHIBI_ENABLE_SANITIZERS", "CHIBI_SANITIZERS",
                      "CHIBI_ENABLE_SJLIT", "CHIBI_ENABLE_TRACY", "CHIBI_WEB_THREADS", "CHIBI_WEB_WORKERS"}


def cache_values(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    result = {}
    for line in path.read_text().splitlines():
        match = re.fullmatch(r"([^:#=]+):[^=]+=(.*)", line)
        if match:
            result[match.group(1)] = match.group(2)
    return result


def effective_arguments(build: Path, arguments: list[str]) -> list[str]:
    explicit = definitions(arguments)
    consumer = cache_values(build / "CMakeCache.txt")
    defaults = {}
    original = {}
    package = consumer.get("CHIBI_DEPENDENCY_PACKAGE")
    if package:
        package_build = Path(package).parent
        try:
            marker = json.loads((package_build.parent / "complete.json").read_text())
            original = definitions(marker["arguments"])
            original.pop("CHIBI_DEVELOPMENT_RELOAD", None)
            defaults = cache_values(package_build / "CMakeCache.txt")
        except (OSError, ValueError, KeyError):
            pass
    inherited = dict(original)
    for name, value in consumer.items():
        compiler = re.fullmatch(r"CMAKE_(C|CXX|OBJC|OBJCXX)_COMPILER", name)
        flags = re.fullmatch(r"CMAKE_(C|CXX|OBJC|OBJCXX)_FLAGS(?:_[A-Z_]+)?", name)
        if not (name in DEPENDENCY_OPTIONS or compiler or flags):
            continue
        if not value and name not in original and not defaults.get(name):
            continue
        # CMake fills compiler paths and default flags on the first configure.
        # Those values are already part of the package's compiler identity.
        if name not in original and value == defaults.get(name):
            continue
        inherited[name] = original.get(name, value) if value == defaults.get(name) else value
    inherited.update(explicit)
    return [f"-D{name}={value}" for name, value in sorted(inherited.items())]


def publish_sources(snapshot: Path, build: Path) -> None:
    home = next((line.split("=", 1)[1] for line in (build / "CMakeCache.txt").read_text().splitlines()
                 if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), None)
    if not home or Path(home).resolve() != (snapshot / "cmake/dependency_package").resolve():
        raise RuntimeError("dependency package cache was configured from another source directory")
    deps = build / "_deps"
    cute = deps / "cute_framework-src"
    pins = fetchcontent_cache.all_pins(snapshot, cute if cute.is_dir() else None)
    root = fetchcontent_cache.cache_root_from_env()
    for source in sorted(deps.glob("*-src")):
        name = source.name[:-4].casefold()
        if fetchcontent_cache.git_root(source) is None:
            continue
        if name not in pins:
            raise RuntimeError(f"unknown dependency: {name}")
        pin = pins[name]
        try:
            fetchcontent_cache.dependency_key(pin)
        except RuntimeError:
            continue
        fetchcontent_cache.publish_one(root, name, pin, source)


def file_identity(path: Path) -> dict:
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def definitions(arguments: list[str]) -> dict[str, str]:
    result = {}
    for argument in arguments:
        match = re.fullmatch(r"-D([^:=]+)(?::[^=]+)?=(.*)", argument, re.DOTALL)
        if not match:
            raise RuntimeError(f"dependency packages accept only -DNAME=VALUE arguments: {argument}")
        name, value = match.groups()
        if (name.startswith("FETCHCONTENT_") or name.startswith("CHIBI_DEPENDENCY_")
                or name in ("CHIBI_SOURCE_ROOT", "CF_USE_SYSTEM_SDL", "SDL3_DIR", "CMAKE_PROJECT_INCLUDE",
                            "CMAKE_PROJECT_INCLUDE_BEFORE", "CMAKE_PROJECT_TOP_LEVEL_INCLUDES",
                            "CHIBI_SLJIT_SOURCE_DIR", "CHIBI_SJLIT_SOURCE_DIR",
                            "CMAKE_PREFIX_PATH", "CMAKE_MODULE_PATH", "CMAKE_INCLUDE_PATH",
                            "CMAKE_LIBRARY_PATH", "CMAKE_FIND_ROOT_PATH", "CMAKE_CONFIGURATION_TYPES")):
            raise RuntimeError(f"unsupported dependency package override: {name}")
        result[name] = value
    return result


def executable_identity(command: str) -> dict:
    words = shlex.split(command)
    if not words:
        raise RuntimeError("empty compiler command")
    executable = shutil.which(words[0])
    if not executable:
        raise RuntimeError(f"missing compiler/tool: {words[0]}")
    result = subprocess.run([*words, "--version"], capture_output=True, text=True, check=True)
    return {"command": words, "executable": file_identity(Path(executable)),
            "version": result.stdout + result.stderr}


def identity(source: Path, profile: str, arguments: list[str]) -> dict:
    arguments = package_arguments(arguments, profile)
    options = definitions(arguments)
    generator = options.get("CMAKE_GENERATOR") or os.environ.get("CMAKE_GENERATOR", "")
    if generator in ("Ninja Multi-Config", "Xcode") or generator.startswith("Visual Studio"):
        raise RuntimeError("dependency packages require a single configuration CMake generator")
    files = {str(path.relative_to(source)): hashlib.sha256(path.read_bytes()).hexdigest()
             for path in source_files(source)}
    tools = {"cmake": executable_identity("cmake")}
    compilers = {}
    selected_web = web_toolchain() if profile == "web" else None
    web_defaults = web_compilers(selected_web) if selected_web else {}
    for language, variable, default in (("C", "CC", "cc"), ("CXX", "CXX", "c++")):
        command = options.get(f"CMAKE_{language}_COMPILER") or os.environ.get(variable) or str(web_defaults.get(language, default))
        compilers[language] = command
        tools[language] = executable_identity(command)
    if sys.platform == "darwin" and profile == "native":
        for language, default in (("OBJC", "C"), ("OBJCXX", "CXX")):
            command = options.get(f"CMAKE_{language}_COMPILER") or os.environ.get(language) or compilers[default]
            tools[language] = executable_identity(command)
    toolchain = options.get("CMAKE_TOOLCHAIN_FILE")
    if toolchain:
        path = Path(toolchain).resolve()
        text = path.read_text()
        if re.search(r"(?i)(\b(include|add_subdirectory|execute_process|file)\s*\(|\$ENV\{)", text):
            raise RuntimeError("dependency package toolchains must not read external files, environment variables, or run commands")
        tools["toolchain"] = file_identity(path)
    if profile == "web":
        modules = selected_web.parents[2]
        tools["web_toolchain"] = {str(path.relative_to(modules)): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in sorted(modules.rglob("*")) if path.is_file()}
        tools["emcc"] = executable_identity("emcc")
        tools["emcmake"] = file_identity(Path(shutil.which("emcmake") or "missing-emcmake"))
    sdk = sdk_path(options) if sys.platform == "darwin" else options.get("CMAKE_OSX_SYSROOT") or os.environ.get("SDKROOT")
    sdk_identity = None
    if sdk:
        path = Path(sdk)
        if path.is_dir():
            settings = next((path / name for name in ("SDKSettings.json", "SDKSettings.plist")
                             if (path / name).is_file()), None)
            sdk_identity = file_identity(settings) if settings else {"path": str(path.resolve())}
        else:
            sdk_identity = sdk
    environment = {name: os.environ.get(name) for name in ENVIRONMENT}
    if sys.platform == "darwin" and profile == "native":
        environment["SDKROOT"] = None
    if selected_web and environment.get("CMAKE_TOOLCHAIN_FILE"):
        if Path(environment["CMAKE_TOOLCHAIN_FILE"]).resolve() == selected_web:
            environment["CMAKE_TOOLCHAIN_FILE"] = None
    return {"schema": SCHEMA, "profile": profile, "arguments": arguments, "files": files,
            "tools": tools, "sdk": sdk_identity, "platform": sys.platform,
            "machine": os.uname().machine,
            "environment": environment,
            "helper": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "fetchcontent_helper": hashlib.sha256(Path(fetchcontent_cache.__file__).read_bytes()).hexdigest()}


def package_key(value: dict) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def complete(entry: Path, value: dict) -> bool:
    try:
        return (json.loads((entry / "complete.json").read_text()) == value
                and (entry / "build/chibi-dependencies.cmake").is_file())
    except (OSError, ValueError):
        return False


def atomic_write(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(contents)
    os.replace(temporary, path)


def probe_path(root: Path, profile: str, value: dict) -> Path:
    """Configure-time compile checks depend on the toolchain and dependency pins, not on patches."""
    pins = {name: digest for name, digest in value["files"].items()
            if name.startswith("cmake/") and not name.endswith(".patch")}
    identity = {name: item for name, item in value.items()
                if name not in ("files", "helper", "fetchcontent_helper")}
    return root / profile / "probes" / f"{package_key({**identity, 'files': pins})}.cmake"


def save_probes(build: Path, destination: Path) -> None:
    """Keep the results of every try_compile check so equivalent configures can skip them."""
    log = build / "CMakeFiles/CMakeConfigureLog.yaml"
    if not log.is_file():  # CMake before 3.26 does not record its checks.
        return
    names, current = [], None
    for line in log.read_text().splitlines():
        if line.startswith("    kind:"):
            current = line.split('"')[1]
        elif current and current.startswith("try_compile") and line.strip().startswith("variable:"):
            names.append(line.split('"')[1])
    cache = cache_values(build / "CMakeCache.txt")
    lines = [f"set({name} {fetchcontent_cache.cmake_bracket(cache[name])} CACHE INTERNAL \"\")\n"
             for name in sorted(set(names)) if name in cache]
    atomic_write(destination, "".join(lines))


KEEP_PACKAGES = 6
PROTECT_SECONDS = 24 * 3600


def evict(root: Path, current: Path) -> None:
    """Bound every profile: drop packages beyond the newest few that have been idle for a day."""
    keep = int(os.environ.get("CHIBI_DEPENDENCY_PACKAGE_KEEP", KEEP_PACKAGES))
    for profile in (path for path in root.iterdir() if path.is_dir()):
        entries = sorted((path for path in profile.iterdir() if (path / "complete.json").is_file()),
                         key=lambda path: (path / "complete.json").stat().st_mtime, reverse=True)
        for entry in entries[keep:]:
            if entry == current or time.time() - (entry / "complete.json").stat().st_mtime < PROTECT_SECONDS:
                continue
            with (profile / f".{entry.name}.lock").open("a+") as lock:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    continue
                shutil.rmtree(entry, ignore_errors=True)


def prepare(source: Path, build: Path, profile: str, arguments: list[str], root: Path) -> Path:
    arguments = effective_arguments(build, arguments)
    value = identity(source, profile, arguments)
    arguments = package_arguments(arguments, profile)
    options = definitions(arguments)
    entry = root / profile / package_key(value)
    entry.parent.mkdir(parents=True, exist_ok=True)
    with (entry.parent / f".{entry.name}.lock").open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not complete(entry, value):
            if entry.exists():
                shutil.rmtree(entry)
            snapshot = entry / "source"
            package_build = entry / "build"
            try:
                snapshot.mkdir(parents=True)
                for path in [source / "CMakeLists.txt", *source_files(source)]:
                    destination = snapshot / path.relative_to(source)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    if sys.platform == "darwin":
                        subprocess.run(["cp", "-c", str(path), str(destination)], check=True)
                    else:
                        shutil.copy2(path, destination)
                copied = {str(path.relative_to(snapshot)): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in source_files(snapshot)}
                if copied != value["files"]:
                    raise RuntimeError("dependency definitions changed while taking their snapshot; retry configure")
                tracy = options.get("CHIBI_ENABLE_TRACY", "OFF").upper() in ("ON", "TRUE", "1", "YES")
                fetchcontent_cache.prepare(snapshot, package_build,
                    fetchcontent_cache.cache_root_from_env(), include_tracy=tracy, web=profile == "web")
                command = ["cmake", "-S", str(snapshot / "cmake/dependency_package"),
                           "-B", str(package_build), f"-DCHIBI_SOURCE_ROOT={snapshot}",
                           "-DCMAKE_BUILD_TYPE=Release",
                           "-C", str(package_build / ".chibi-fetchcontent-cache.cmake"), *arguments]
                probes = probe_path(root, profile, value)
                if probes.is_file():
                    command.extend(["-C", str(probes)])
                if profile == "web":
                    command.insert(0, "emcmake")
                generator = options.get("CMAKE_GENERATOR")
                if generator:
                    command.extend(["-G", generator])
                elif not os.environ.get("CMAKE_GENERATOR") and shutil.which("ninja"):
                    command.extend(["-G", "Ninja"])
                # Entries live at different paths; base_dir makes their compile commands identical to ccache.
                environment = {**os.environ, "CCACHE_BASEDIR": str(entry)}
                subprocess.run(command, check=True, stdout=sys.stderr, env=environment)
                subprocess.run(["cmake", "--build", str(package_build), "--target",
                                "chibi_dependency_package", "--parallel",
                                os.environ.get("CHIBI_DEPENDENCY_PACKAGE_JOBS", "4")],
                               check=True, stdout=sys.stderr, env=environment)
                publish_sources(snapshot, package_build)
                save_probes(package_build, probes)
                for path in package_build.rglob("*"):
                    relative = path.relative_to(package_build)
                    compiler_directory = "CMakeFiles" in relative.parts and any(
                        part.endswith(".dir") for part in relative.parts)
                    target_prefix = relative.parts[:relative.parts.index("CMakeFiles")] if compiler_directory else relative.parts
                    dependency_source = any(part.endswith("-src") for part in target_prefix)
                    if compiler_directory and not dependency_source and path.is_file() and (
                            path.suffix in (".o", ".obj") or path.name.endswith(".o.d")):
                        path.unlink()
                if not (package_build / "chibi-dependencies.cmake").is_file():
                    raise RuntimeError("package configure did not export chibi-dependencies.cmake")
                atomic_write(entry / "complete.json", json.dumps(value, sort_keys=True) + "\n")
            except BaseException:
                shutil.rmtree(entry, ignore_errors=True)
                raise
        os.utime(entry / "complete.json")
    evict(root, entry)
    bracket = fetchcontent_cache.cmake_bracket
    atomic_write(build / ".chibi-dependency-package.cmake",
        "# Generated by tools/dependency_package.py; do not edit.\n"
        f"set(CHIBI_DEPENDENCY_PACKAGE {bracket(str(entry / 'build/chibi-dependencies.cmake'))} CACHE FILEPATH \"\" FORCE)\n"
        f"set(CHIBI_DEPENDENCY_SOURCE_ROOT {bracket(str(entry / 'source'))} CACHE PATH \"\" FORCE)\n")
    return entry


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "key"))
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--profile", choices=("native", "web"), required=True)
    parser.add_argument("--cache-root", type=Path)
    argv = sys.argv[1:]
    boundary = argv.index("--") if "--" in argv else len(argv)
    args = parser.parse_args(argv[:boundary])
    arguments = argv[boundary + 1:]
    source = args.source_dir.resolve()
    if args.action == "key":
        if args.build_dir:
            arguments = effective_arguments(args.build_dir.resolve(), arguments)
        print(package_key(identity(source, args.profile, arguments)))
        return 0
    if not args.build_dir:
        parser.error("prepare requires --build-dir")
    print(prepare(source, args.build_dir.resolve(), args.profile, arguments,
                  args.cache_root.resolve() if args.cache_root else cache_root_from_env()))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"dependency package: {error}", file=sys.stderr)
        raise SystemExit(1)
