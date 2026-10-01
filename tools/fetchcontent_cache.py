#!/usr/bin/env python3
"""Seed private FetchContent checkouts from independent pristine user snapshots."""

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

SCHEMA = 3
FIELDS = ("GIT_REPOSITORY", "GIT_TAG", "URL", "URL_HASH")


def cache_root_from_env() -> Path:
    configured = os.environ.get("CHIBI_FETCHCONTENT_CACHE_DIR")
    if configured:
        return Path(configured).resolve()
    base = os.environ.get("XDG_CACHE_HOME")
    if not base:
        home = os.environ.get("HOME")
        if not home:
            raise RuntimeError("set CHIBI_FETCHCONTENT_CACHE_DIR or HOME")
        base = str(Path(home) / ".cache")
    return Path(base).resolve() / "chibi-fetchcontent"


def declarations(text: str) -> dict[str, dict[str, str]]:
    """Read literal FetchContent declarations and our tree-sitter source macro."""
    text = re.sub(r"#[^\n]*", "", text)
    pins = {}
    for match in re.finditer(r"(?is)\bFetchContent_Declare\s*\(\s*([\w${}]+)(.*?)\)", text):
        name, body = match.groups()
        if "$" in name:
            continue
        fields = {}
        for field in FIELDS:
            value = re.search(rf"(?im)^\s*{field}\s+(\"[^\"]*\"|[^\s)]+)", body)
            if value:
                fields[field] = value.group(1).strip('"')
        if fields and not any("${" in value for value in fields.values()):
            pins.setdefault(name.casefold(), fields)
    for match in re.finditer(r"(?is)\bchibi_tree_sitter_source\s*\(\s*(\w+)\s+(\S+)\s+([^\s)]+)\s*\)", text):
        name, repository, tag = match.groups()
        pins.setdefault(name.casefold(), {"GIT_REPOSITORY": repository, "GIT_TAG": tag})
    return pins


def cmake_sources(source_dir: Path) -> list[Path]:
    return [source_dir / "CMakeLists.txt", *sorted((source_dir / "cmake").glob("*.cmake"))]


def source_pins(source_dir: Path) -> dict[str, dict[str, str]]:
    pins = {}
    for path in cmake_sources(source_dir):
        for name, identity in declarations(path.read_text()).items():
            pins.setdefault(name, identity)
    return pins


def fingerprint(source_dir: Path) -> str:
    digest = hashlib.sha256(b"chibi-fetchcontent-config-v3\0")
    paths = cmake_sources(source_dir) + sorted((source_dir / "cmake" / "patches").glob("*.patch"))
    for path in paths:
        digest.update(str(path.relative_to(source_dir)).encode() + b"\0")
        contents = path.read_bytes()
        digest.update(len(contents).to_bytes(8, "big"))
        digest.update(contents)
    return digest.hexdigest()


def dependency_key(identity: dict[str, str]) -> str:
    if "GIT_REPOSITORY" in identity and "GIT_TAG" in identity:
        fields = {key: identity[key] for key in ("GIT_REPOSITORY", "GIT_TAG")}
    elif "URL" in identity and "URL_HASH" in identity:
        fields = {key: identity[key] for key in ("URL", "URL_HASH")}
    else:
        raise RuntimeError(f"dependency lacks a verifiable pin: {identity}")
    return hashlib.sha256(json.dumps(fields, sort_keys=True).encode()).hexdigest()


def entry_path(cache_root: Path, name: str, identity: dict[str, str]) -> Path:
    return cache_root / "sources" / name / dependency_key(identity)


def clone_tree(source: Path, destination: Path) -> None:
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
    shutil.copytree(source, destination, symlinks=True)


def git(source: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(source), *args], capture_output=True, text=True,
                            env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"})
    if result.returncode:
        raise RuntimeError(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout.rstrip("\n")


def git_root(source: Path) -> Path | None:
    if not (source / ".git").exists():
        return None
    return Path(git(source, "rev-parse", "--show-toplevel")).resolve()


def expected_revision(source: Path, pin: str) -> str:
    return git(source, "rev-parse", f"{pin}^{{commit}}")


def validate_source(source: Path, identity: dict[str, str]) -> str:
    if git_root(source) != source.resolve() or "GIT_TAG" not in identity:
        raise RuntimeError(f"cannot verify source checkout {source}")
    head = git(source, "rev-parse", "HEAD")
    if head != expected_revision(source, identity["GIT_TAG"]):
        raise RuntimeError(f"revision differs from declared pin: {source}")
    origin = git(source, "config", "--get", "remote.origin.url")
    if origin != identity["GIT_REPOSITORY"]:
        raise RuntimeError(f"repository differs from declared pin: {source}")
    return head


def complete(entry: Path, name: str, identity: dict[str, str]) -> bool:
    try:
        marker = json.loads((entry / "complete.json").read_text())
    except (OSError, ValueError):
        return False
    return (marker.get("schema") == SCHEMA and marker.get("identity") == identity
            and (entry / f"{name}-src").is_dir())


def nested_pins(cute: Path) -> dict[str, dict[str, str]]:
    # Read the pinned commit, never a donor's locally modified declaration file.
    return declarations(git(cute, "show", "HEAD:CMakeLists.txt"))


def all_pins(source_dir: Path, cute: Path | None) -> dict[str, dict[str, str]]:
    pins = source_pins(source_dir)
    if cute:
        validate_source(cute, pins["cute_framework"])
        for name, identity in nested_pins(cute).items():
            pins.setdefault(name, identity)
    return pins


def clone_pristine(source: Path, destination: Path, commit: str) -> None:
    # Local transport honors depth, keeping only the pinned commit rather than
    # cloning every dependency's history. Object storage never refers to donor paths.
    result = subprocess.run(["git", "clone", "--no-local", "--depth", "1", "--no-checkout",
                             str(source), str(destination)], capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    git(destination, "checkout", "--detach", commit)
    # Tags naming this pin must still resolve in the shallow snapshot.
    for tag in git(source, "tag", "--points-at", commit).splitlines():
        git(destination, "update-ref", f"refs/tags/{tag}", commit)
    git(destination, "remote", "set-url", "origin", git(source, "config", "--get", "remote.origin.url"))
    # Clone each gitlink as an independent checkout. No .git file points back to
    # the donor's .git/modules, and dirty donor files are never copied.
    for line in git(source, "ls-tree", "-r", commit).splitlines():
        mode, _, tail = line.partition(" ")
        if mode != "160000":
            continue
        metadata, _, relative = tail.partition("\t")
        subcommit = metadata.split()[-1]
        child = source / relative
        if git_root(child) != child.resolve() or git(child, "rev-parse", "HEAD") != subcommit:
            raise RuntimeError(f"submodule is missing or differs from pinned gitlink: {child}")
        target = destination / relative
        if target.exists():
            shutil.rmtree(target)
        target.parent.mkdir(parents=True, exist_ok=True)
        clone_pristine(child, target, subcommit)


def publish_one(cache_root: Path, name: str, identity: dict[str, str], source: Path) -> bool:
    commit = validate_source(source, identity)
    entry = entry_path(cache_root, name, identity)
    entry.parent.mkdir(parents=True, exist_ok=True)
    lock_path = entry.parent / f".{entry.name}.lock"
    with lock_path.open("a+") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if complete(entry, name, identity):
            return False
        temporary = Path(tempfile.mkdtemp(prefix=f".{entry.name}.", dir=entry.parent))
        try:
            clone_pristine(source, temporary / f"{name}-src", commit)
            (temporary / "complete.json").write_text(json.dumps({"schema": SCHEMA,
                "identity": identity, "commit": commit}, sort_keys=True) + "\n")
            if entry.exists():
                shutil.rmtree(entry)
            os.replace(temporary, entry)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    return True


def validate_donor(source_dir: Path, build_dir: Path) -> list[tuple[str, Path, dict[str, str]]]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        raise RuntimeError(f"unconfigured donor: {build_dir}")
    home = next((line.split("=", 1)[1] for line in cache.read_text().splitlines()
                 if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")), None)
    if home is None or Path(home).resolve() != source_dir.resolve():
        raise RuntimeError("donor was configured from another source directory")
    deps = build_dir / "_deps"
    cute = deps / "cute_framework-src"
    pins = all_pins(source_dir, cute if cute.is_dir() else None)
    valid = []
    for source in sorted(deps.glob("*-src")):
        name = source.name[:-4].casefold()
        if name not in pins:
            raise RuntimeError(f"unknown dependency: {name}")
        identity = pins[name]
        # Older builds can hold extracted archives despite a new Git declaration.
        # A version string cannot prove their contents; do not cache those folders.
        if git_root(source) is None:
            continue
        validate_source(source, identity)
        valid.append((name, source, identity))
    return valid


def seed_one(cache_root: Path, name: str, identity: dict[str, str], deps: Path) -> Path | None:
    target = deps / f"{name}-src"
    if target.exists():
        # Never adopt arbitrary existing patched source trees as cache overrides.
        return None
    entry = entry_path(cache_root, name, identity)
    if not complete(entry, name, identity):
        return None
    deps.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{name}.", dir=deps))
    try:
        checkout = temporary / target.name
        clone_tree(entry / target.name, checkout)
        os.replace(checkout, target)
    finally:
        shutil.rmtree(temporary)
    return target


def cmake_bracket(value: str) -> str:
    if "\n" in value or "\r" in value:
        raise RuntimeError("dependency paths cannot contain newlines")
    equals = ""
    while f"]{equals}]" in value:
        equals += "="
    return f"[{equals}[{value}]{equals}]"


def prepare(source_dir: Path, build_dir: Path, cache_root: Path,
            include_tracy: bool = False, include_sdl: bool = True, web: bool = False) -> int:
    deps = build_dir / "_deps"
    deps.mkdir(parents=True, exist_ok=True)
    pins = source_pins(source_dir)
    cute_identity = pins.get("cute_framework")
    cute = None
    if cute_identity:
        entry = entry_path(cache_root, "cute_framework", cute_identity)
        if complete(entry, "cute_framework", cute_identity):
            cute = entry / "cute_framework-src"
    pins = all_pins(source_dir, cute)
    preload = build_dir / ".chibi-fetchcontent-cache.cmake"
    previous = preload.read_text() if preload.is_file() else ""
    owned = {match.group(1).casefold(): match.group(2) for match in re.finditer(
        r"set\(FETCHCONTENT_SOURCE_DIR_(\w+) \[=*\[(.*?)\]=*\] CACHE PATH", previous)}
    configured = {}
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text().splitlines():
            match = re.match(r"FETCHCONTENT_SOURCE_DIR_(\w+):[^=]+=(.*)", line)
            if match and match.group(2):
                configured[match.group(1).casefold()] = match.group(2)
    overrides = []
    unsets = []
    for name, identity in sorted(pins.items()):
        user_override = configured.get(name)
        if user_override and (name not in owned or Path(user_override).resolve() != Path(owned[name]).resolve()):
            continue
        excluded = (name == "tracy" and not include_tracy
                    or name == "sdl3" and (not include_sdl or web)
                    or name == "sljit" and web)
        target = deps / f"{name}-src"
        if name in owned:
            valid = False
            if not excluded and target.is_dir() and not target.is_symlink():
                try:
                    validate_source(target, identity)
                    valid = True
                except RuntimeError:
                    pass
            if valid:
                overrides.append((name, target))
                continue
            if user_override:
                unsets.append(name)
        if excluded:
            continue
        target = seed_one(cache_root, name, identity, deps)
        if target:
            overrides.append((name, target))
    lines = ["# Generated by tools/fetchcontent_cache.py; do not edit."]
    for name in unsets:
        lines.append(f"unset(FETCHCONTENT_SOURCE_DIR_{name.upper()} CACHE)")
    for name, target in overrides:
        lines.append(f"set(FETCHCONTENT_SOURCE_DIR_{name.upper()} {cmake_bracket(str(target.resolve()))} CACHE PATH \"\" FORCE)")
    with tempfile.NamedTemporaryFile(mode="w", dir=build_dir, delete=False) as output:
        output.write("\n".join(lines) + "\n")
        temporary = output.name
    os.replace(temporary, preload)
    return len(overrides)


def publish_build(source_dir: Path, build_dir: Path, cache_root: Path) -> int:
    return sum(publish_one(cache_root, name, identity, source)
               for name, source, identity in validate_donor(source_dir, build_dir))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("fingerprint", "prepare", "publish"))
    parser.add_argument("build_dir_pos", nargs="?", type=Path)
    parser.add_argument("--source-dir", type=Path, default=Path.cwd())
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--cache-root", type=Path)
    parser.add_argument("--tracy", action="store_true")
    parser.add_argument("--no-sdl", action="store_true")
    parser.add_argument("--web", action="store_true")
    args = parser.parse_args()
    if args.build_dir and args.build_dir_pos:
        raise RuntimeError("use either positional build directory or --build-dir")
    source_dir = args.source_dir.resolve()
    if args.action == "fingerprint":
        print(fingerprint(source_dir))
        return 0
    build = args.build_dir or args.build_dir_pos
    if not build:
        raise RuntimeError("--build-dir is required")
    build = build.resolve()
    root = args.cache_root.resolve() if args.cache_root else cache_root_from_env()
    if args.action == "prepare":
        count = prepare(source_dir, build, root, args.tracy, not args.no_sdl, args.web)
    else:
        count = publish_build(source_dir, build, root)
    print(f"{args.action}: {count} dependency source snapshot(s)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"fetchcontent cache: {error}", file=sys.stderr)
        raise SystemExit(1)
