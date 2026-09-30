#!/usr/bin/env python3
"""Seed private FetchContent source trees from a per-user pristine cache."""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


PATCH_LIST = re.compile(
    rb"(?m)^\s*foreach\s*\(\s*name\s+(cute-[^)]*)\)"
)
SCHEMA = 1


def cache_root_from_env() -> Path:
    configured = os.environ.get("CHIBI_FETCHCONTENT_CACHE_DIR")
    if configured:
        root = Path(configured)
    else:
        base = os.environ.get("XDG_CACHE_HOME")
        if base:
            root = Path(base) / "chibi-fetchcontent"
        else:
            home = os.environ.get("HOME")
            if not home:
                raise RuntimeError(
                    "set CHIBI_FETCHCONTENT_CACHE_DIR or HOME to enable the source cache"
                )
            root = Path(home) / ".cache" / "chibi-fetchcontent"
    root.mkdir(parents=True, exist_ok=True)
    return root.resolve()


def fingerprint(source_dir: Path) -> str:
    cmake_path = source_dir / "CMakeLists.txt"
    cmake_bytes = cmake_path.read_bytes()
    match = PATCH_LIST.search(cmake_bytes)
    if not match:
        raise RuntimeError(f"cannot find Cute patch list in {cmake_path}")
    patch_names = match.group(1).decode("utf-8").split()
    if not patch_names:
        raise RuntimeError(f"empty CUTE_PATCH_NAME list in {cmake_path}")

    source_pins = fetchcontent_source_pins(cmake_path)
    digest = hashlib.sha256()
    digest.update(b"chibi-fetchcontent-cache-v2\0")
    pin_bytes = json.dumps(source_pins, separators=(",", ":")).encode("utf-8")
    digest.update(len(pin_bytes).to_bytes(8, "big"))
    digest.update(pin_bytes)
    for name in patch_names:
        patch_path = source_dir / "cmake" / "patches" / f"{name}.patch"
        patch_bytes = patch_path.read_bytes()
        digest.update(name.encode("utf-8") + b"\0")
        digest.update(len(patch_bytes).to_bytes(8, "big"))
        digest.update(patch_bytes)
    return digest.hexdigest()


def clone_tree(source: Path, destination: Path) -> None:
    """Copy a directory, preferring filesystem reflinks where available."""
    if sys.platform == "darwin":
        command = ["cp", "-cR", str(source), str(destination)]
    elif sys.platform.startswith("linux"):
        command = ["cp", "--reflink=auto", "-R", str(source), str(destination)]
    else:
        command = []

    if command:
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        if result.returncode == 0:
            return
        if destination.exists():
            shutil.rmtree(destination)
    shutil.copytree(source, destination, symlinks=True, copy_function=shutil.copy2)


def complete_marker(cache_dir: Path, name: str) -> Path:
    return cache_dir / ".complete" / f"{name}.json"


def read_complete(cache_dir: Path, name: str) -> dict | None:
    marker = complete_marker(cache_dir, name)
    source = cache_dir / f"{name}-src"
    try:
        value = json.loads(marker.read_text())
    except (OSError, json.JSONDecodeError):
        return None
    if value.get("schema") != SCHEMA or value.get("name") != name or not source.is_dir():
        return None
    return value


def atomic_json(path: Path, value: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=path.parent, prefix=f".{path.name}.", delete=False
    ) as output:
        temporary = Path(output.name)
        json.dump(value, output, sort_keys=True, indent=2)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def lock_for(cache_dir: Path, name: str):
    lock_dir = cache_dir / ".locks"
    lock_dir.mkdir(parents=True, exist_ok=True)
    handle = (lock_dir / f"{name}.lock").open("a+")
    fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
    return handle


def git_value(source: Path, *args: str) -> str | None:
    result = subprocess.run(
        ["git", "-C", str(source), *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    if result.returncode:
        return None
    return result.stdout.strip()


def git_root(source: Path) -> Path | None:
    if not (source / ".git").exists():
        return None
    root = git_value(source, "rev-parse", "--show-toplevel")
    return Path(root).resolve() if root else None


def fetchcontent_git_pins(cmake_path: Path) -> dict[str, str]:
    text = cmake_path.read_text(encoding="utf-8")
    declarations = re.finditer(
        r"(?is)\bFetchContent_Declare\s*\(\s*([A-Za-z0-9_]+)(.*?)\)", text
    )
    pins: dict[str, str] = {}
    for declaration in declarations:
        name, body = declaration.groups()
        match = re.search(r"(?im)^\s*GIT_TAG\s+([^\s)#]+)", body)
        if match:
            pins[name.casefold()] = match.group(1)
    return pins


def fetchcontent_source_pins(
    cmake_path: Path,
) -> list[tuple[str, tuple[tuple[str, str], ...]]]:
    text = cmake_path.read_text(encoding="utf-8")
    declarations = re.finditer(
        r"(?is)\bFetchContent_Declare\s*\(\s*([A-Za-z0-9_]+)(.*?)\)", text
    )
    pins = []
    for declaration in declarations:
        name, body = declaration.groups()
        fields = []
        for field in ("GIT_REPOSITORY", "GIT_TAG", "URL", "URL_HASH"):
            match = re.search(rf"(?im)^\s*{field}\s+([^\s)#]+)", body)
            if match:
                fields.append((field, match.group(1)))
        if fields:
            pins.append((name.casefold(), tuple(fields)))
    return sorted(pins)


def expected_revision(source: Path, tag: str) -> str | None:
    if re.fullmatch(r"[0-9a-fA-F]{40}", tag):
        return tag.lower()
    value = git_value(source, "rev-parse", f"{tag}^{{commit}}")
    return value.lower() if value else None


def source_version(source: Path) -> str | None:
    try:
        text = (source / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"(?im)^\s*project\s*\(\s*SDL3\b.*?\bVERSION\s+\"?([0-9.]+)", text)
    return match.group(1) if match else None


def expected_sdl_version(cute_source: Path) -> str | None:
    try:
        text = (cute_source / "CMakeLists.txt").read_text(encoding="utf-8")
    except OSError:
        return None
    match = re.search(r"(?im)^\s*set\s*\(\s*SDL_REQUIRED_VERSION\s+([0-9.]+)", text)
    return match.group(1) if match else None


def validate_donor(source_dir: Path, build_dir: Path) -> list[tuple[str, Path]]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        raise RuntimeError(f"refusing to publish unconfigured build directory {build_dir}")
    home_directory = None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
            home_directory = Path(line.split("=", 1)[1]).resolve()
            break
    if home_directory != source_dir.resolve():
        raise RuntimeError(
            f"refusing donor configured from {home_directory}, expected {source_dir.resolve()}"
        )

    deps_dir = build_dir / "_deps"
    if not deps_dir.is_dir():
        return []
    sources = {
        path.name[:-4].casefold(): path
        for path in deps_dir.glob("*-src")
        if path.is_dir()
    }
    if not sources:
        return []

    root_pins = fetchcontent_git_pins(source_dir / "CMakeLists.txt")
    cute = sources.get("cute_framework")
    if cute is None or git_root(cute) != cute.resolve():
        raise RuntimeError("refusing donor without a local cute_framework Git checkout")
    cute_pin = root_pins.get("cute_framework")
    if not cute_pin:
        raise RuntimeError("cannot establish the cute_framework pin from current CMakeLists.txt")
    cute_expected = expected_revision(cute, cute_pin)
    cute_head = git_value(cute, "rev-parse", "HEAD")
    if not cute_expected or not cute_head or cute_head.lower() != cute_expected:
        raise RuntimeError("refusing donor with a cute_framework revision that differs from current pin")

    nested_pins = fetchcontent_git_pins(cute / "CMakeLists.txt")
    valid: list[tuple[str, Path]] = []
    for name, source in sorted(sources.items()):
        root = git_root(source)
        if root == source.resolve():
            pin = root_pins.get(name) or nested_pins.get(name)
            if not pin:
                raise RuntimeError(f"cannot establish the configured {name} Git pin")
            expected = expected_revision(source, pin)
            actual = git_value(source, "rev-parse", "HEAD")
            if not expected or not actual or actual.lower() != expected:
                raise RuntimeError(f"refusing donor with {name} revision that differs from current pin")
            valid.append((source.name[:-4], source))
            continue

        if name == "sdl3":
            expected = expected_sdl_version(cute)
            actual = source_version(source)
            if not expected or actual != expected:
                raise RuntimeError("refusing SDL3 donor whose version does not match cute_framework")
            valid.append((source.name[:-4], source))
    return valid


def publish_one(cache_dir: Path, name: str, source: Path) -> bool:
    """Atomically publish a clean source snapshot. Returns whether it was new."""
    cache_dir.mkdir(parents=True, exist_ok=True)
    lock = lock_for(cache_dir, name)
    try:
        if read_complete(cache_dir, name):
            return False

        final = cache_dir / f"{name}-src"
        marker = complete_marker(cache_dir, name)
        if final.exists():
            shutil.rmtree(final)
        marker.unlink(missing_ok=True)

        temporary = Path(tempfile.mkdtemp(prefix=f".{name}.", dir=cache_dir))
        source_copy = temporary / f"{name}-src"
        if git_root(source) == source.resolve():
            commit = git_value(source, "rev-parse", "HEAD")
            origin = git_value(source, "config", "--get", "remote.origin.url")
            result = subprocess.run(
                ["git", "clone", "--local", str(source), str(source_copy)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                text=True,
            )
            if result.returncode:
                shutil.rmtree(temporary, ignore_errors=True)
                raise RuntimeError(f"git clone --local failed for {source}: {result.stderr.strip()}")
            cloned_commit = git_value(source_copy, "rev-parse", "HEAD")
            if cloned_commit != commit:
                shutil.rmtree(temporary, ignore_errors=True)
                raise RuntimeError(f"published {name} at {cloned_commit}, expected {commit}")
            # The clone checks out HEAD, so Cute's local patches and generated files stay out.
            clean = git_value(source_copy, "status", "--porcelain", "--untracked-files=no")
            if clean:
                shutil.rmtree(temporary, ignore_errors=True)
                raise RuntimeError(f"published Git snapshot for {name} is not clean")
        else:
            commit = None
            origin = None
            clone_tree(source, source_copy)
            # SDL's MSVC-only feature probe writes this file into its source directory.
            if name.casefold() == "sdl3":
                (source_copy / "test_malloc_alloca.c").unlink(missing_ok=True)

        os.replace(source_copy, final)
        shutil.rmtree(temporary, ignore_errors=True)
        atomic_json(
            marker,
            {
                "schema": SCHEMA,
                "name": name,
                "commit": commit,
                "origin": origin,
            },
        )
        return True
    finally:
        lock.close()


def seed_one(cache_dir: Path, name: str, build_deps: Path) -> Path | None:
    destination = build_deps / f"{name}-src"
    if destination.exists():
        return destination
    if not read_complete(cache_dir, name):
        return None
    source = cache_dir / f"{name}-src"

    temporary = build_deps / f".{name}-src.{os.getpid()}.tmp"
    if temporary.exists():
        shutil.rmtree(temporary)
    clone_tree(source, temporary)
    try:
        os.replace(temporary, destination)
    except FileExistsError:
        shutil.rmtree(temporary, ignore_errors=True)
        return None
    return destination


def cmake_bracket(value: str) -> str:
    if "\n" in value or "\r" in value:
        raise RuntimeError("dependency cache paths cannot contain newlines")
    equals = ""
    while f"]{equals}]" in value:
        equals += "="
    return f"[{equals}[{value}]{equals}]"


def prepare(
    source_dir: Path, build_dir: Path, cache_root: Path, key: str,
    include_tracy: bool, include_sdl: bool,
) -> int:
    cache_dir = cache_root / key
    build_deps = build_dir / "_deps"
    build_deps.mkdir(parents=True, exist_ok=True)
    overrides: list[tuple[str, Path]] = []
    included = {
        "cute_framework",
        "sljit",
        "enkits",
        "box2d",
        "box3d",
        "physfs",
        "sdl3",
    }
    if include_tracy:
        included.add("tracy")
    if not include_sdl:
        included.remove("sdl3")
    names = {path.name[:-4] for path in build_deps.glob("*-src") if path.is_dir()}
    names.update(marker.stem for marker in (cache_dir / ".complete").glob("*.json"))
    destinations: dict[str, Path] = {}
    for name in sorted(names):
        if name.casefold() not in included:
            continue
        destination = seed_one(cache_dir, name, build_deps)
        if destination:
            destinations[name] = destination
    for name, destination in sorted(destinations.items()):
        if source_tree_is_complete(source_dir, build_deps, name, destination):
            overrides.append((name, destination.resolve()))

    preload = build_dir / ".chibi-fetchcontent-cache.cmake"
    lines = ["# Generated by tools/fetchcontent_cache.py; do not edit."]
    for name, destination in overrides:
        variable = re.sub(r"[^A-Za-z0-9_]", "_", name).upper()
        lines.append(
            f"set(FETCHCONTENT_SOURCE_DIR_{variable} {cmake_bracket(str(destination))} CACHE PATH \"\" FORCE)"
        )
    temporary = preload.with_name(f".{preload.name}.{os.getpid()}.tmp")
    temporary.write_text("\n".join(lines) + "\n", encoding="utf-8")
    os.replace(temporary, preload)
    return len(overrides)


def source_tree_is_complete(
    source_dir: Path, build_deps: Path, name: str, source: Path
) -> bool:
    if source.is_symlink() or source.resolve().parent != build_deps.resolve():
        return False
    if name.casefold() == "sdl3":
        cute = build_deps / "cute_framework-src"
        expected = expected_sdl_version(cute)
        return bool(expected and source_version(source) == expected)
    if git_root(source) != source.resolve():
        return False
    if any(
        line[:2] in {" D", "D "}
        for line in (git_value(source, "status", "--porcelain", "--untracked-files=no") or "").splitlines()
    ):
        return False
    root_pins = fetchcontent_git_pins(source_dir / "CMakeLists.txt")
    cute = build_deps / "cute_framework-src"
    pins = root_pins
    if name.casefold() != "cute_framework":
        if git_root(cute) != cute.resolve():
            return False
        cute_pin = root_pins.get("cute_framework")
        expected_cute = expected_revision(cute, cute_pin) if cute_pin else None
        cute_head = git_value(cute, "rev-parse", "HEAD")
        if not expected_cute or not cute_head or cute_head.lower() != expected_cute:
            return False
        pins = {**root_pins, **fetchcontent_git_pins(cute / "CMakeLists.txt")}
    pin = pins.get(name.casefold())
    if not pin:
        return False
    expected = expected_revision(source, pin)
    actual = git_value(source, "rev-parse", "HEAD")
    return bool(expected and actual and actual.lower() == expected)


def publish_build(source_dir: Path, build_dir: Path, cache_root: Path, key: str) -> int:
    cache_dir = cache_root / key
    valid_sources = validate_donor(source_dir, build_dir)
    published = 0
    for name, source in valid_sources:
        if publish_one(cache_dir, name, source):
            published += 1
    return published


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fingerprint", "prepare", "publish"))
    parser.add_argument("build_dir_pos", nargs="?", type=Path)
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", dest="build_dir", type=Path)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--tracy", action="store_true")
    parser.add_argument("--no-sdl", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.build_dir and args.build_dir_pos:
        raise RuntimeError("use either positional build directory or --build-dir")
    args.build_dir = args.build_dir or args.build_dir_pos
    source_dir = args.source_dir.resolve()
    key = fingerprint(source_dir)
    if args.action == "fingerprint":
        print(key)
        return 0
    if not args.build_dir:
        raise RuntimeError(f"--build-dir is required for {args.action}")
    build_dir = args.build_dir.resolve()
    cache_root = args.cache_root.resolve() if args.cache_root else cache_root_from_env()
    if args.action == "prepare":
        count = prepare(source_dir, build_dir, cache_root, key, args.tracy, not args.no_sdl)
        print(f"seeded {count} dependency source tree(s) for cache {key[:12]}", file=sys.stderr)
        print(key)
        return 0
    count = publish_build(source_dir, build_dir, cache_root, key)
    print(f"published {count} dependency source snapshot(s) under {cache_root / key}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"fetchcontent cache: {error}", file=sys.stderr)
        raise SystemExit(1)
