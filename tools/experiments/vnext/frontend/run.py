#!/usr/bin/env python3
"""Run ONE independently approved frozen parser build, never bootstrap recovery."""

import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from prepare import identity, PARSER_FILES, IMPLEMENTATION_FILES

# In the checkout the shared operation is one directory up; preparation copies
# it beside this controller, where the ordinary script import path finds it.
sys.path.append(str(Path(__file__).resolve().parent.parent))
import owned_process

SEED_SHA256 = "1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e"


def write_new(path, value):
    payload = json.dumps(value, indent=2, allow_nan=False) + "\n"
    with path.open("x") as stream:
        stream.write(payload)


def check_sources(directory, fence):
    actual = sorted(str(p.relative_to(directory / "compiler"))
                    for p in (directory / "compiler").rglob("*.chibi"))
    if actual != sorted(PARSER_FILES):
        raise ValueError("frozen parser closure changed")
    if sorted(row["path"] for row in fence["parser_sources"]) != sorted(PARSER_FILES):
        raise ValueError("frozen parser fence membership changed")
    for row in fence["parser_sources"]:
        if identity(directory / "compiler" / row["path"]) != {k: row[k] for k in ("bytes", "sha256")}:
            raise ValueError(f"frozen source changed: {row['path']}")
    if identity(directory / "ParserHarness.chibi") != fence["harness"]:
        raise ValueError("generated harness changed")
    if sorted(row["path"] for row in fence["implementation"]) != sorted("controller/" + name for name in IMPLEMENTATION_FILES):
        raise ValueError("frozen implementation fence membership changed")
    for row in fence["implementation"]:
        if identity(directory / row["path"]) != {k: row[k] for k in ("bytes", "sha256")}:
            raise ValueError(f"probe implementation changed: {row['path']}")
    if list((directory / "empty-core").iterdir()):
        raise ValueError("unexpected core dependency")


def run_command(argv, directory, environment, name, limit):
    # The launch marker is exclusive and precedes Popen. An interrupted caller
    # cannot silently rerun an uncertain operation into the same evidence path.
    write_new(directory / f"{name}-launch.json", {"argv": argv, "limit_seconds": limit})
    result = {"argv": argv, "limit_seconds": limit, "exit_code": None, "status": "not_launched"}
    start = time.monotonic()
    try:
        with (directory / f"{name}.stdout").open("xb") as out, (directory / f"{name}.stderr").open("xb") as err:
            execution = owned_process.run(argv, directory, environment, out, err, limit)
        for key in ("pid", "exit_code", "status", "elapsed_seconds", "cleanup_seconds",
                    "inspection_error", "termination_error"):
            if key in execution:
                result[key] = execution[key]
        if "exception" in execution:
            error = execution["exception"]
            result["error"] = f"{error['type']}: {error['message']}"
        if execution["cleanup_errors"]:
            result["cleanup_errors"] = execution["cleanup_errors"]
        if "interruption" in execution:
            result["signal"] = execution["last_signal"]
    except BaseException as error:
        result["status"] = "interrupted" if isinstance(error, (InterruptedError, KeyboardInterrupt)) else "error"
        result["error"] = f"{type(error).__name__}: {error}"
        result["elapsed_seconds"] = time.monotonic() - start
    write_new(directory / f"{name}-result.json", result)
    return result


def check_inputs(directory, fence, identities):
    check_sources(directory, fence)
    for row in identities:
        path = Path(row["path"])
        if str(path.resolve(strict=True)) != row["resolved"] or identity(path) != {k: row[k] for k in ("bytes", "sha256")}:
            raise ValueError(f"tool/link input changed: {row['path']}")


def run(directory, seed, llvm):
    owned_process.require_linux()
    if Path(__file__).resolve() != directory / "controller/run.py":
        raise ValueError("launch the frozen controller/run.py inside the prepared directory")
    # No inherited compiler configuration, caches or output locations.
    environment = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8",
                   "LD_LIBRARY_PATH": f"{llvm}/usr/lib/x86_64-linux-gnu:{llvm}/usr/lib/llvm-22/lib"}
    for variable, folder in (("HOME", "home"), ("TMPDIR", "tmp"), ("XDG_CACHE_HOME", "cache")):
        environment[variable] = str(directory / folder)
    fence = json.loads((directory / "source-fence.json").read_text())
    check_sources(directory, fence)
    if identity(seed)["sha256"] != SEED_SHA256:
        raise ValueError("seed identity does not match pinned c5 public seed")
    tools = [seed, *(llvm / "usr/lib/llvm-22/bin" / name
                    for name in ("opt", "llc", "llvm-as", "llvm-link", "ld.lld"))]
    # Keep lookup aliases: the command/loader follow these, not a previously
    # resolved target that could stay unchanged after an alias is retargeted.
    inputs = set([*tools, directory / "source-fence.json"])
    dependencies = {}
    for tool in tools:
        found = subprocess.check_output(["/usr/bin/ldd", str(tool)], env=environment, text=True, timeout=10)
        if "not found" in found:
            raise ValueError(f"missing tool dependency: {tool}\n{found}")
        dependencies[str(tool)] = found
        inputs.update(Path(path) for path in re.findall(r"(/[^\s()]+)", found))
    # The seed's Linux dynamic link searches these normal system locations.
    # libc.so is a linker script and libc_nonshared.a is its nonshared input.
    for name in ("crt1.o", "crti.o", "crtn.o", "libc.so", "libc_nonshared.a", "libc.so.6"):
        inputs.add(Path("/usr/lib/x86_64-linux-gnu") / name)
    identities = [{"path": str(path), "resolved": str(path.resolve(strict=True)), **identity(path)} for path in sorted(inputs)]
    write_new(directory / "tool-inputs.json", {"environment": environment, "files": identities, "ldd": dependencies})
    for folder in ("home", "tmp", "cache"):
        (directory / folder).mkdir()
    executable = directory / "parser-harness"
    argv = [str(seed), "--compiler", str(directory / "compiler"), "--core", str(directory / "empty-core"),
            "--source", str(directory / "ParserHarness.chibi"), "--entry", "Main", "--output", str(executable),
            "--llvm", str(llvm / "usr/lib/llvm-22/bin"), "--level", "0", "--jobs", "1"]
    result = {"compile": run_command(argv, directory, environment, "compile", 300), "parser_execution": "not_run"}
    try:
        check_inputs(directory, fence, identities)
        if result["compile"]["status"] == "passed":
            result["executable"] = identity(executable)
            result["parser_execution"] = run_command([str(executable)], directory, environment, "parser", 30)
            if identity(executable) != result["executable"]:
                raise ValueError("parser executable changed during execution")
            check_inputs(directory, fence, identities)
    except BaseException as error:
        result["postflight_error"] = f"{type(error).__name__}: {error}"
    result["claim"] = "parser experiment only; no current compiler admission, typed body, JIT/AOT slice or throughput result"
    write_new(directory / "result.json", result)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--llvm-root", required=True, type=Path)
    args = parser.parse_args()
    result = run(args.directory.resolve(strict=True), args.seed.resolve(strict=True), args.llvm_root.resolve(strict=True))
    print(json.dumps(result))
    sys.exit(0 if result["compile"]["status"] == "passed"
             and isinstance(result["parser_execution"], dict)
             and result["parser_execution"]["status"] == "passed"
             and "postflight_error" not in result else 1)
