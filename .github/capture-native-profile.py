#!/usr/bin/env python3
"""Bind selected CI commands to current build records and sanitizer symbols.

Run immediately after the selected build, before tests re-enter the planner.
Records can describe successful cache reuse, not necessarily a fresh compile.
This checks representative objects and required links, not every translation unit.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess


def stamp(path: Path) -> str:
    info = path.stat()
    modified, modified_ns = divmod(info.st_mtime_ns, 1_000_000_000)
    changed, changed_ns = divmod(info.st_ctime_ns, 1_000_000_000)
    return (f"{info.st_dev}:{info.st_ino}:{info.st_size}:{info.st_mode:o}:"
            f"{modified}.{modified_ns:09}:{changed}.{changed_ns:09}")


def read_record(output: Path, expected_args: list[str]) -> Path:
    record = Path(str(output) + ".chibi-record")
    lines = record.read_text().splitlines()
    count = len(expected_args)
    if lines[:count] != expected_args or lines[count:count + 1] != [""]:
        raise ValueError(f"{record}: command mismatch")
    footer = lines[count + 1:]
    if len(footer) < 4 or len(footer) % 2 or Path(footer[-2]) != output:
        raise ValueError(f"{record}: missing output binding or malformed stamps")
    if expected_args.count("-o") != 1 or expected_args.index("-o") == len(expected_args) - 1:
        raise ValueError(f"{record}: missing command output")
    if Path(expected_args[expected_args.index("-o") + 1]) != output:
        raise ValueError(f"{record}: command output mismatch")
    if "-c" in expected_args:
        if expected_args.count("-c") != 1 or expected_args.index("-c") == len(expected_args) - 1:
            raise ValueError(f"{record}: unexpected compile command")
        inputs = {expected_args[expected_args.index("-c") + 1]}
    else:
        # c.LinkCommand puts objects before -o, libraries after its output.
        # Absolute flag operands (notably macOS -isysroot) are not link inputs.
        at = expected_args.index("-o")
        inputs = {arg for arg in expected_args[1:at] if arg.endswith(".o")}
        inputs.update(arg for arg in expected_args[at + 2:] if Path(arg).is_absolute())
    if not inputs or not inputs.issubset(set(footer[:-2:2])):
        raise ValueError(f"{record}: missing command input binding")
    for path, recorded in zip(footer[::2], footer[1::2]):
        if not path or stamp(Path(path)) != recorded:
            raise ValueError(f"{record}: stale input/output {path}")
    return record


def check_sanitizers(arguments: list[str], sanitizers: bool) -> None:
    selected = [arg for arg in arguments if arg.startswith("-fsanitize=")]
    expected = ["-fsanitize=address,undefined"] if sanitizers else []
    if selected != expected or any(arg.startswith("-fno-sanitize=") for arg in arguments):
        raise ValueError("expected ASan+UBSan exactly when requested, without disabling flags")


def check_flags(arguments: list[str], sanitizers: bool, test: bool = False) -> None:
    if [arg for arg in arguments if arg.startswith("-O")] != ["-O1"]:
        raise ValueError("expected exactly -O1")
    if "-g" not in arguments or "-g0" in arguments:
        raise ValueError("expected debug information")
    if "-fno-omit-frame-pointer" not in arguments or "-fomit-frame-pointer" in arguments:
        raise ValueError("expected frame pointers")
    defines = [arg[2:] for arg in arguments if arg.startswith("-D") and arg != "-D"]
    defines.extend(arguments[i + 1] for i, arg in enumerate(arguments[:-1]) if arg == "-D")
    if any(value.split("=", 1)[0] == "NDEBUG" for value in defines):
        raise ValueError("NDEBUG disables assertions")
    if test and "-UNDEBUG" not in arguments:
        raise ValueError("test assertions must be explicitly enabled")
    check_sanitizers(arguments, sanitizers)


def check_symbols(nm_text: str, sanitizers: bool) -> list[str]:
    # Mach-O adds one leading underscore to the same runtime symbol names.
    symbols = sorted(set(re.findall(r"(?m)^\s*(?:[0-9a-fA-F]+\s+)?[UT]\s+(___?(?:asan|ubsan)_\w+)\s*$", nm_text)))
    families = {symbol.lstrip("_").split("_", 1)[0] for symbol in symbols}
    if families != ({"asan", "ubsan"} if sanitizers else set()):
        raise ValueError("sanitizer symbols do not match the requested profile")
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    # Deliberate allowlist: never copy the environment, credentials or caches.
    environment = {key: os.environ.get(key, "") for key in (
        "GITHUB_SHA", "GITHUB_RUN_ID", "GITHUB_RUN_ATTEMPT", "GITHUB_JOB",
        "CHIBI_CI_SANITIZERS", "BUILD_PROFILE", "BUILD_DEBUG_OPTIMIZATION",
        "BUILD_DEBUG_FRAME_POINTERS", "CHIBI_NATIVE_BUILD_OUTCOME")}
    receipt = {"environment": environment, "status": "incomplete", "checked": [],
               "scope": "three representative objects and five required links; not every translation unit"}
    try:
        receipt["checkout"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        receipt["tree"] = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], text=True).strip()
        receipt["tracked_changes"] = subprocess.check_output(
            ["git", "status", "--porcelain", "--untracked-files=no"], text=True)
        if receipt["checkout"] != environment["GITHUB_SHA"] or receipt["tracked_changes"]:
            raise ValueError("checkout does not match the clean CI source")
        if environment["CHIBI_CI_SANITIZERS"] not in ("ON", "OFF"):
            raise ValueError("missing selected sanitizer profile")
        sanitizers = environment["CHIBI_CI_SANITIZERS"] == "ON"
        if (environment["BUILD_PROFILE"] != ("sanitizer" if sanitizers else "debug")
                or environment["BUILD_DEBUG_OPTIMIZATION"] != "1"
                or environment["BUILD_DEBUG_FRAME_POINTERS"] != ("" if sanitizers else "on")):
            raise ValueError("unexpected selected CI profile")

        # These databases are plans. Only the entries bound to live successful
        # records below are counted as built evidence.
        databases = []
        for name in ("compile_commands.json", "link_commands.json"):
            path = args.build / name
            shutil.copyfile(path, args.output / name)
            databases.append(json.loads(path.read_text()))
        receipt["planned_compile_count"] = len(databases[0])
        receipt["planned_link_count"] = len(databases[1])
        objects = ("objects/chibi_support/src/support/arena.c.o",
                   "objects/test_tools/test/tools/main.c.o",
                   "objects/test_language/test/language/main.c.o")
        links = ("sdk/chibi", "sdk/chibi-compile", "sdk/chibi-build-host",
                 "test/tools/test_tools", "test/language/test_language")
        for database, paths in zip(databases, (objects, links)):
            by_output = {}
            for row in database:
                output = Path(row["output"]).resolve()
                if output in by_output:
                    raise ValueError(f"duplicate command for {output}")
                by_output[output] = row
            for relative in paths:
                output = (args.build / relative).resolve()
                if output not in by_output:
                    raise ValueError(f"missing planned command for {output}")
                command = by_output[output]["arguments"]
                compiled = relative.endswith(".o")
                destination = args.output / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                record = Path(str(output) + ".chibi-record")
                if record.is_file():
                    shutil.copyfile(record, str(destination) + ".chibi-record")
                if compiled:
                    source = Path(relative.split("/", 2)[2][:-2]).resolve()
                    if Path(by_output[output]["file"]).resolve() != source or command[-2:] != ["-c", str(source)]:
                        raise ValueError(f"wrong representative source for {output}")
                    check_flags(command, sanitizers, test="/test/" in relative)
                else:
                    check_sanitizers(command, sanitizers)
                before = stamp(output)
                read_record(output, command)
                with output.open("rb") as stream:
                    digest = hashlib.file_digest(stream, "sha256").hexdigest()
                nm_text = subprocess.check_output([args.nm, "-u" if compiled else "-g", str(output)], text=True)
                Path(str(destination) + ".symbols.txt").write_text("\n".join(
                    line for line in nm_text.splitlines() if "__asan_" in line or "__ubsan_" in line) + "\n")
                symbols = check_symbols(nm_text, sanitizers)
                if stamp(output) != before:
                    raise ValueError(f"output changed during capture: {output}")
                receipt["checked"].append({"output": str(output), "sha256": digest,
                                           "stamp": before, "symbols": symbols})

        header = args.build / "test/tools/testing/include/export_compiler.h"
        shutil.copyfile(header, args.output / "export_compiler.h")
        text = header.read_text()
        exported = [json.loads(value) for value in re.findall(r'^\s*("(?:[^"\\]|\\.)*"),\s*$', text, re.MULTILINE)]
        check_flags(exported, sanitizers, test=True)
        if environment["CHIBI_NATIVE_BUILD_OUTCOME"] != "success":
            raise ValueError("selected build did not succeed; partial evidence only")
        receipt["status"] = "verified"
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        receipt["error"] = str(error)
    (args.output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))
    return 0 if receipt["status"] == "verified" else 1


if __name__ == "__main__":
    raise SystemExit(main())
