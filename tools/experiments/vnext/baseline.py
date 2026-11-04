#!/usr/bin/env python3
"""Static Gather census and bounded, fresh-process candidate measurements.

Neither a census nor a successful arbitrary command establishes Chibi throughput.
Only the standard library is used. See README.md for the measurement boundary.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys

import owned_process


ROOTS = {"project": "tools/compiler", "core": "packages/core"}
LIMITS = {"packages": 4096, "files": 16384, "file_bytes": 16 << 20,
          "total_bytes": 256 << 20}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def git(root, *args):
    return subprocess.check_output(
        ["git", "-C", str(root), *args], text=True, timeout=30).strip()


def source_identity(root, head, tree):
    if Path(git(root, "rev-parse", "--show-toplevel")).resolve() != root:
        raise ValueError(f"source root is not the repository root: {root}")
    if git(root, "rev-parse", "HEAD") != head:
        raise ValueError("source HEAD does not match expected HEAD")
    if git(root, "rev-parse", "HEAD^{tree}") != tree:
        raise ValueError("source tree does not match expected tree")
    # Docs and the harness may be edited independently; every compiler/core
    # source, including untracked additions, must match the committed tree.
    if git(root, "status", "--porcelain", "--untracked-files=all", "--", *ROOTS.values()):
        raise ValueError("compiler/core source roots have uncommitted changes")


def contained_path(root, relative):
    path = Path(relative)
    if path.is_absolute() or not path.parts or ".." in path.parts:
        raise ValueError(f"not a relative contained path: {relative}")
    result = root / path
    if not result.resolve().is_relative_to(root):
        raise ValueError(f"path escapes root: {relative}")
    if any(part.is_symlink() for part in [result, *result.parents] if part != root):
        raise ValueError(f"symlink is not supported: {relative}")
    return result


def package_path(root, relative):
    # Gather names the collection directory itself with '.', unlike an output.
    return root if relative == "." else contained_path(root, relative)


def imported_from(source, collection):
    # Match core:strings SplitLines/TrimSpace exactly. Python splitlines/strip
    # also recognize Unicode separators/spaces that Gather does not recognize.
    found = []
    prefix = collection + ":"
    for line in source.split("\n"):
        trimmed = line.removesuffix("\r").strip(" \t\n\v\f\r")
        if not trimmed.startswith("import "):
            continue
        parts = trimmed.split('"')
        if len(parts) >= 3 and parts[1].startswith(prefix):
            found.append(parts[1][len(prefix):])
    return found


def census(root, head, tree):
    root = Path(root).resolve(strict=True)
    source_identity(root, head, tree)
    queue = [("project", "native_driver")]
    seen = set(queue)
    rows = []
    total_bytes = 0
    # Check bytes against committed blobs too: git status can omit ignored files
    # and trust assume-unchanged/index stat hints. A HEAD label alone is not proof.
    blobs = {}
    for entry in git(root, "ls-tree", "-r", "-z", "HEAD", "--", *ROOTS.values()).split("\0"):
        if entry:
            metadata, path = entry.split("\t", 1)
            blobs[path] = metadata.split()[2]
    # Mirrors native_driver ImportedFrom/PackageFiles, not a language parser.
    # In particular both target branches and import-looking block-comment
    # lines count; files ending in Tests.chibi do not.
    for kind, package in queue:
        folder = contained_path(root, f"{ROOTS[kind]}/{package}")
        files = sorted((p for p in folder.iterdir()
                        if p.is_file() and p.name.endswith(".chibi")
                        and not p.name.endswith("Tests.chibi")),
                       key=lambda p: os.fsencode(p.name))
        if not files:
            raise ValueError(f"package has no sources: {kind}:{package}")
        if (kind, package) == queue[0]:
            files.sort(key=lambda p: p.name != "Main.chibi")
        for path in files:
            contained_path(root, str(path.relative_to(root)))
            if len(rows) >= LIMITS["files"] or path.stat().st_size > LIMITS["file_bytes"]:
                raise ValueError(f"source census limit exceeded: {path}")
            data = path.read_bytes()
            relative = str(path.relative_to(root))
            object_id = blobs.get(relative)
            if object_id is None:
                raise ValueError(f"source is not tracked at expected HEAD: {relative}")
            blob = b"blob " + str(len(data)).encode("ascii") + b"\0" + data
            algorithm = "sha1" if len(object_id) == 40 else "sha256"
            if hashlib.new(algorithm, blob).hexdigest() != object_id:
                raise ValueError(f"source bytes differ from expected HEAD: {relative}")
            total_bytes += len(data)
            if len(data) > LIMITS["file_bytes"] or total_bytes > LIMITS["total_bytes"]:
                raise ValueError("source census byte limit exceeded")
            rows.append({"path": str(path.relative_to(root)), "bytes": len(data),
                         "physical_lines": data.count(b"\n") + int(bool(data) and not data.endswith(b"\n")),
                         "sha256": digest(data)})
            source = data.decode("utf-8")
            for imported_kind in ROOTS:
                for imported in imported_from(source, imported_kind):
                    # Gather accepts package paths, not traversal outside a collection.
                    package_path(root / ROOTS[imported_kind], imported)
                    key = (imported_kind, imported)
                    if key not in seen:
                        if len(queue) >= LIMITS["packages"]:
                            raise ValueError("source census package limit exceeded")
                        seen.add(key)
                        queue.append(key)
    source_identity(root, head, tree)
    return {"schema": 1, "classification": "static_gather_census",
            "source_root": str(root), "head": head, "tree": tree,
            "scope": "native_driver static Gather closure; both target branches; no test files; not compilation evidence",
            "limits": LIMITS, "packages": len(queue), "files": len(rows),
            "bytes": total_bytes, "physical_lines": sum(r["physical_lines"] for r in rows),
            "package_inventory": [f"{k}:{p}" for k, p in queue], "source_files": rows,
            "unmeasured": ["active-target source", "nonblank noncomment lines", "tokens",
                           "specializations", "typed instructions", "machine-code bytes",
                           "compiler phases", "compiler time", "peak memory", "allocations"]}


def write_json(path, value):
    # Serialize first: rejected values must not create a partial receipt.
    payload = json.dumps(value, indent=2, allow_nan=False) + "\n"
    # Exclusive creation prevents accidental replacement of earlier evidence.
    with Path(path).open("x", encoding="utf-8") as stream:
        stream.write(payload)


def verify_inputs(spec):
    source = spec["source"]
    receipt_bytes = Path(source["census"]).read_bytes()
    if digest(receipt_bytes) != source["census_sha256"]:
        raise ValueError("source census digest does not match expected identity")
    recorded = json.loads(receipt_bytes)
    actual = census(source["root"], source["head"], source["tree"])
    if actual != recorded:
        raise ValueError("source closure differs from the pinned census")
    for tool in spec["tools"]:
        path = Path(tool["path"])
        if not path.is_absolute() or file_digest(path) != tool["sha256"]:
            raise ValueError(f"tool identity does not match expected tool: {path}")
    return actual


def finite_number(text):
    value = float(text)
    if not math.isfinite(value):
        raise ValueError("specification numbers must be finite")
    return value


def measure(spec_path, run_dir):
    owned_process.require_linux()
    spec_bytes = Path(spec_path).read_bytes()
    spec = json.loads(spec_bytes, parse_float=finite_number, parse_constant=finite_number)
    timeout = spec["timeout_seconds"]
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("timeout_seconds must be finite and positive")
    if spec.get("schema") != 1 or spec["classification"] not in ("harness_self_test", "chibi_candidate"):
        raise ValueError("unsupported run schema or classification")
    if not isinstance(spec["workload"], str) or not spec["workload"].strip():
        raise ValueError("an explicit workload name is required")
    argv = spec["argv"]
    if not argv or not all(isinstance(a, str) for a in argv):
        raise ValueError("argv must be a nonempty array of strings")
    if not spec["tools"] or argv[0] != spec["tools"][0]["path"]:
        raise ValueError("argv[0] must be the first explicitly pinned tool path")
    if not spec["expected_outputs"]:
        raise ValueError("at least one expected output is required")
    environment = spec["environment"]
    if not isinstance(environment, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in environment.items()):
        raise ValueError("environment must map names to strings")
    reserved = {"HOME", "TMPDIR", "XDG_CACHE_HOME"}
    if reserved.intersection(environment):
        raise ValueError("HOME, TMPDIR and XDG_CACHE_HOME belong to the fresh run")
    before = verify_inputs(spec)
    run_dir = Path(run_dir).absolute()
    # Resolve the parent only: resolve() on an existing symlink would hide reuse.
    run_dir = run_dir.parent.resolve(strict=True) / run_dir.name
    output_dir = run_dir / "output"
    for output in spec["expected_outputs"]:
        contained_path(output_dir, output["path"])
    run_dir.mkdir()  # Must not exist, even when empty.
    for name in ("output", "cache", "tmp", "home"):
        (run_dir / name).mkdir()
    substitutions = {"{source_root}": before["source_root"], "{output_dir}": str(output_dir),
                     "{cache_dir}": str(run_dir / "cache"), "{run_dir}": str(run_dir)}
    expanded = []
    for arg in argv:
        for key, value in substitutions.items():
            arg = arg.replace(key, value)
        expanded.append(arg)
    env = dict(environment, HOME=str(run_dir / "home"), TMPDIR=str(run_dir / "tmp"),
               XDG_CACHE_HOME=str(run_dir / "cache"))
    result = {"schema": 1, "classification": spec["classification"], "workload": spec["workload"],
              "spec_sha256": digest(spec_bytes), "spec": spec, "argv": expanded, "cwd": str(output_dir),
              "environment": env, "host": {"platform": platform.platform(), "machine": platform.machine(),
                                            "logical_cpus": os.cpu_count()},
              "source_census_sha256": spec["source"]["census_sha256"],
              "timing_boundary": "before process creation through observed leader exit; includes startup, child work waited for by command, finalization and exit",
              "cleanup_boundary": "after leader exit on ordinary runs: watchdog shutdown, owned-child termination/reap through ECHILD and subreaper restoration; separate 1-second allowance",
              "filesystem_condition": "uncontrolled/warmed by preflight source and tool reads; not a cold-cache measurement",
              "outside_timing": ["source census and source/tool SHA-256 verification before and after execution",
                                 "fresh directory creation", "owned descendant cleanup after observed leader exit",
                                 "output validation and output SHA-256", "receipt writing"],
              "limitations": ["arbitrary argv does not prove full non-incremental compilation or correctness",
                              "cache isolation requires command to honor fresh paths and avoid external caches/daemons",
                              "all transitive tools, runtime inputs and thread settings must be declared by the caller",
                              "static Gather lines are not a verified throughput denominator"],
              "phase_times": None, "peak_memory_bytes": None, "allocations": None,
              "exit_code": None, "timed_out": False, "elapsed_seconds": None,
              "outputs": [], "errors": []}
    with (run_dir / "stdout.log").open("xb") as stdout, (run_dir / "stderr.log").open("xb") as stderr:
        execution = owned_process.run(expanded, output_dir, env, stdout, stderr, timeout)
    for key in ("exit_code", "timed_out", "cleanup_seconds", "cleanup_echild",
                "subreaper_restored", "cleanup_limit_seconds", "overall_limit_seconds"):
        result[key] = execution[key]
    if "pid" in execution:
        result["elapsed_seconds"] = execution["elapsed_seconds"]
    if "exception" in execution:
        result["exception"] = execution["exception"]
        error = execution["exception"]
        result["errors"].append(f"process execution failed: {error['type']}: {error['message']}")
    if execution["surviving_children"]:
        result["errors"].append("owned descendant survived command exit; termination requested")
    for key in ("inspection_error", "termination_error"):
        if key in execution:
            result[key] = execution[key]
            result["errors"].append(f"process cleanup failed ({key}): {execution[key]}")
    result["errors"].extend(f"process cleanup failed: {error}" for error in execution["cleanup_errors"])
    if "interruption" in execution:
        result["interruption"] = execution["interruption"]
        result["errors"].append(f"command interrupted by {execution['interruption']['name']}")
    if result["timed_out"]:
        result["errors"].append("command deadline exceeded")
    if result["exit_code"] is not None and result["exit_code"] != 0:
        result["errors"].append(f"command exited with status {result['exit_code']}")
    # Never hide compiler hashing or other compiler work outside the timer. The
    # only source processing here is independent harness identity verification.
    try:
        verify_inputs(spec)
        for expected in spec["expected_outputs"]:
            path = contained_path(output_dir, expected["path"])
            if not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"missing or empty expected output: {expected['path']}")
            actual = {"path": expected["path"], "bytes": path.stat().st_size, "sha256": file_digest(path)}
            result["outputs"].append(actual)
            if "sha256" in expected and actual["sha256"] != expected["sha256"]:
                raise ValueError(f"output digest mismatch: {expected['path']}")
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        result["errors"].append(f"postflight failed: {error}")
    result["success"] = not result["errors"]
    write_json(run_dir / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    count = commands.add_parser("census", help="static native_driver Gather census, not compilation")
    for name in ("source-root", "expected-head", "expected-tree", "output"):
        count.add_argument("--" + name, required=True)
    run = commands.add_parser("run", help="measure one explicitly specified candidate command")
    run.add_argument("--spec", required=True)
    run.add_argument("--run-dir", required=True)
    args = parser.parse_args()
    try:
        if args.command == "census":
            result = census(args.source_root, args.expected_head, args.expected_tree)
            write_json(args.output, result)
            print(json.dumps({"classification": result["classification"], "files": result["files"],
                              "physical_lines": result["physical_lines"], "receipt_sha256": file_digest(args.output)}))
            return 0
        result = measure(args.spec, args.run_dir)
        print(json.dumps({k: result[k] for k in ("classification", "success", "exit_code", "timed_out", "elapsed_seconds", "errors")}))
        return 0 if result["success"] else 1
    except (KeyError, TypeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"baseline: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
