#!/usr/bin/env python3
"""Run the five source-native correctness stages once using explicit existing tools.

This is developer reproduction, not independent backend/toolchain admission or a
performance measurement. No building dependencies, retries, shell, or fallback.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import stat
import signal
import sys
import types

HERE = Path(__file__).resolve().parent
SEED_SHA256 = "1d1d289a2429fe2ee2a75a71a959bf21590a02ff3ebf7cd8af1037a11bf1268e"
AOT_SUCCESS = b'{"aot_c_caller_checks":45,"status":"passed","input":"source-produced-copied-object"}\n'


def file_identity(path):
    """Bind a regular file's bytes and filesystem identity, not just its name."""
    path = Path(path)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or path.resolve(strict=True) != path:
        raise ValueError(f"expected a regular non-symlink file: {path}")
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as stream:
        while block := stream.read(256 * 1024):
            digest.update(block)
            size += len(block)
    after = path.stat()
    if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
            after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise ValueError(f"file changed while reading: {path}")
    return {"bytes": size, "sha256": digest.hexdigest(), "device": after.st_dev,
            "inode": after.st_ino, "mode": stat.S_IMODE(after.st_mode)}


def write_new(path, value):
    # Serialize before touching the destination. NaN/Infinity never enter receipts.
    payload = json.dumps(value, indent=2, allow_nan=False) + "\n"
    with path.open("x", encoding="utf-8") as stream:
        stream.write(payload)


def stage_plan(directory, seed, llvm_bin, cc, library_dirs):
    """Pure argv/environment construction. The five stages have one definition."""
    directory, seed, llvm_bin, cc = map(Path, (directory, seed, llvm_bin, cc))
    normal = directory / "run/normal"
    output = directory / "output"
    executable = output / "native-main"
    obj = normal / "source-native.o"
    aot = output / "native-aot"
    env = {"PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "LC_ALL": "C.UTF-8",
           "HOME": str(directory / "home"), "TMPDIR": str(directory / "tmp"),
           "XDG_CACHE_HOME": str(directory / "cache")}
    if library_dirs:
        env["LD_LIBRARY_PATH"] = ":".join(map(str, library_dirs))
    commands = [
        ("chibi_compile", 179, [str(seed), "--compiler", str(directory / "project"), "--core", str(directory / "core"),
          "--source", str(directory / "entry/Main.chibi"), "--entry", "Main", "--output", str(executable),
          "--llvm", str(llvm_bin), "--level", "0", "--jobs", "1"], output, executable),
        ("jit_normal", 119, [str(executable)], normal, obj),
        ("perf_failure", 29, [str(executable), "expect-perf-buildid-failure"], directory / "run/perf", None),
        ("c_link", 59, [str(cc), "-std=c11", "-O0", "-g0", "-Wall", "-Wextra", "-Werror",
          "-B" + str(llvm_bin) + "/", "-fuse-ld=lld", "-v", "-H", "-MD", "-MF",
          str(output / "native-aot.d"), "-Wl,--trace", str(directory / "inputs/native_aot_caller.c"),
          str(obj), "-o", str(aot)], output, aot),
        ("aot_run", 29, [str(aot)], directory / "run/aot", None),
    ]
    result = []
    for name, limit, argv, cwd, artifact in commands:
        environment = dict(env)
        if name == "perf_failure":
            environment["PERF_BUILDID_DIR"] = str(directory / "run/perf/forbidden-profile")
        result.append({"name": name, "argv": argv, "cwd": str(cwd), "environment": environment,
                       "limit_seconds": limit, "cleanup_limit_seconds": 1,
                       "artifact": str(artifact) if artifact is not None else None})
    return result


def next_stage(plan, results):
    # At most five rows. Appending a failed receipt is terminal, never a retry.
    assert len(results) <= len(plan)
    if any(row["status"] != "passed" for row in results) or len(results) == len(plan):
        return None
    return plan[len(results)]


def require_success(execution, limit):
    """Validate the supervisor's facts before allowing the next native stage."""
    for field, cap in (("elapsed_seconds", limit), ("cleanup_seconds", 1)):
        value = execution.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= cap:
            raise ValueError(f"invalid or exceeded {field}: {value!r}")
    for field, expected in (("cleanup_limit_seconds", 1), ("overall_limit_seconds", limit + 1)):
        value = execution.get(field)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or value != expected:
            raise ValueError(f"invalid {field}: {value!r}")
    if (execution.get("status") != "passed" or type(execution.get("exit_code")) is not int
            or execution["exit_code"] != 0 or execution.get("timed_out") is not False
            or execution.get("surviving_children") is not False or execution.get("cleanup_errors") != []
            or execution.get("cleanup_echild") is not True or execution.get("subreaper_restored") is not True
            or any(key in execution for key in ("exception", "inspection_error", "termination_error", "interruption"))):
        raise ValueError("native stage failed or process ownership did not close cleanly")


def validate_manifest(directory, manifest, stage_names):
    """One boundary for the fields this runner consumes, not a general decoder."""
    prefix = str(directory / "source-manifest.json")
    if not isinstance(manifest, dict):
        raise ValueError(f"{prefix}: root must be an object")
    if type(manifest.get("schema")) is not int or manifest["schema"] != 1:
        raise ValueError(f"{prefix}: schema must be 1")
    if manifest.get("directory") != str(directory):
        raise ValueError(f"{prefix}: directory is missing, invalid, or relocated")
    if manifest.get("stages") != {name: "not_run" for name in stage_names}:
        raise ValueError(f"{prefix}: stages must name the five unrun stages")
    for field, count in (("files", 32), ("implementation", 2)):
        rows = manifest.get(field)
        if not isinstance(rows, list) or len(rows) != count:
            raise ValueError(f"{prefix}: {field} must contain exactly {count} file records")
        for index, row in enumerate(rows):
            label = f"{prefix}: {field}[{index}]"
            if not isinstance(row, dict) or not isinstance(row.get("path"), str) or not row["path"]:
                raise ValueError(f"{label}.path must be a nonempty string")
            if type(row.get("bytes")) is not int or row["bytes"] < 0:
                raise ValueError(f"{label}.bytes must be a nonnegative integer")
            digest = row.get("sha256")
            if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise ValueError(f"{label}.sha256 must be a lowercase SHA256 digest")
    backend = manifest.get("backend")
    if not isinstance(backend, dict):
        raise ValueError(f"{prefix}: backend must be an object")
    for field in ("path", "resolved"):
        if not isinstance(backend.get(field), str) or not backend[field]:
            raise ValueError(f"{prefix}: backend.{field} must be a nonempty string")
    if type(backend.get("bytes")) is not int or backend["bytes"] <= 0:
        raise ValueError(f"{prefix}: backend.bytes must be a positive integer")
    digest = backend.get("sha256")
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise ValueError(f"{prefix}: backend.sha256 must be a lowercase SHA256 digest")


def check_sources(directory, manifest, stage_names):
    validate_manifest(directory, manifest, stage_names)
    rows = manifest["files"] + manifest["implementation"]
    names = [row["path"] for row in rows]
    if (len(manifest["files"]) != 32 or len(set(names)) != len(names)
            or sorted(row["path"] for row in manifest["implementation"]) != ["controller/owned_process.py", "controller/run_native.py"]):
        raise ValueError("source-native snapshot membership changed")
    for name in names:
        if (Path(name).is_absolute() or any(part in ("", ".", "..") for part in name.split("/"))
                or name.split("/")[0] not in ("project", "core", "inputs", "entry", "controller")):
            raise ValueError(f"invalid snapshot path: {name}")
    actual = []
    for folder in ("project", "core", "inputs", "entry", "controller"):
        root = directory / folder
        if root.is_symlink() or not root.is_dir():
            raise ValueError(f"invalid snapshot directory: {root}")
        for path in root.rglob("*"):
            if path.is_symlink():
                raise ValueError(f"symlink in snapshot: {path}")
            if not path.is_dir():
                actual.append(str(path.relative_to(directory)))
    if sorted(actual) != sorted(names):
        raise ValueError("snapshot contains missing or unexpected files")
    identities = {}
    for row in rows:
        path = directory / row["path"]
        current = file_identity(path)
        if any(current[key] != row[key] for key in ("bytes", "sha256")):
            raise ValueError(f"snapshot changed: {path}")
        identities[str(path)] = current
    backend = manifest["backend"]
    library = Path(backend["path"])
    if not library.is_absolute() or str(library) != backend["resolved"]:
        raise ValueError("backend must name its absolute non-symlink path")
    current = file_identity(library)
    if not current["bytes"] or any(current[key] != backend[key] for key in ("bytes", "sha256")):
        raise ValueError(f"backend library changed: {library}")
    identities[str(library)] = current
    return identities


def check_stage_outputs(stage, directory, attempt):
    if stage["name"] == "perf_failure" and any((directory / "run/perf").iterdir()):
        raise ValueError("perf failure control created unexpected filesystem output")
    if stage["name"] == "aot_run":
        with (attempt / "aot_run.stdout").open("rb") as stream:
            if stream.read(len(AOT_SUCCESS) + 1) != AOT_SUCCESS:
                raise ValueError("AOT caller did not emit the exact 45-check source-produced-object success record")


def run(directory, seed, llvm_bin, cc, library_dirs):
    directory = Path(directory).absolute()
    if directory.resolve(strict=True) != directory or Path(__file__).resolve() != directory / "controller/run_native.py":
        raise ValueError("launch the frozen controller/run_native.py in its original prepared directory")
    # Construct the closed stage rows before validation. Resolving explicit tool
    # aliases later changes only their argv paths, not the stage definitions.
    plan = stage_plan(directory, seed, llvm_bin, cc, library_dirs)
    stage_names = [stage["name"] for stage in plan]
    manifest_path = directory / "source-manifest.json"
    manifest_identity = file_identity(manifest_path)
    raw = manifest_path.read_bytes()
    if len(raw) != manifest_identity["bytes"] or hashlib.sha256(raw).hexdigest() != manifest_identity["sha256"]:
        raise ValueError(f"source manifest changed while reading: {manifest_path}")
    try:
        manifest = json.loads(raw)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise ValueError(f"invalid {manifest_path}: {error}") from error
    sources = check_sources(directory, manifest, stage_names)
    inputs = dict(sources)
    inputs[str(manifest_path)] = manifest_identity
    # Execute exactly the admitted bytes, never a path reopened after checking
    # and never a helper discovered outside the copied controller directory.
    owner_path = directory / "controller/owned_process.py"
    owner_bytes = owner_path.read_bytes()
    expected = sources[str(owner_path)]
    if (len(owner_bytes) != expected["bytes"] or hashlib.sha256(owner_bytes).hexdigest() != expected["sha256"]
            or file_identity(owner_path) != expected):
        raise ValueError(f"copied process supervisor changed: {owner_path}")
    owned_process = types.ModuleType("native_owned_process")
    owned_process.__file__ = str(owner_path)
    exec(compile(owner_bytes, str(owner_path), "exec"), owned_process.__dict__)
    owned_process.require_linux()
    # This records explicit tool identities, not a loader-closure proof.
    aliases = {str(Path(path).absolute()): str(Path(path).resolve(strict=True))
               for path in (seed, cc, *(Path(llvm_bin) / name for name in ("opt", "llc", "llvm-as", "llvm-link", "ld.lld")))}
    for resolved in aliases.values():
        inputs[resolved] = file_identity(Path(resolved))
        if not os.access(resolved, os.X_OK):
            raise ValueError(f"tool is not executable: {resolved}")
    seed, cc = (Path(aliases[str(Path(path).absolute())]) for path in (seed, cc))
    if inputs[str(seed)]["sha256"] != SEED_SHA256:
        raise ValueError("seed does not match the pinned c5 public seed")
    llvm_bin = Path(llvm_bin).absolute()
    libraries = [Path(path).absolute() for path in library_dirs]
    for path in [llvm_bin, *libraries]:
        if not path.is_dir() or path.resolve() != path or ":" in str(path):
            raise ValueError(f"tool/library directory must be an existing non-symlink path without colons: {path}")
    plan = stage_plan(directory, seed, llvm_bin, cc, libraries)
    for name in ("attempt", "output", "run", "home", "tmp", "cache"):
        path = directory / name
        if path.exists() or path.is_symlink():
            raise FileExistsError(f"native reproduction requires an unused preparation: {path}")

    attempt = directory / "attempt"
    results, artifacts = [], {}
    controller_signals, signal_cleanup_errors, handlers = [], [], {}
    stop_signals = (signal.SIGINT, signal.SIGTERM)
    raising_stops, attempt_owned = True, False
    attempt_error = None

    def stop(signum, _frame):
        controller_signals.append(signal.Signals(signum).name)
        if raising_stops:
            raise InterruptedError("controller stopped by " + signal.Signals(signum).name)

    try:
        for signum in stop_signals:
            handlers[signum] = signal.getsignal(signum)
            signal.signal(signum, stop)
        # mkdir can succeed before Python records ownership of the new directory.
        raising_stops = False
        attempt.mkdir()
        attempt_owned = True
        raising_stops = True
        if controller_signals:
            raise InterruptedError("controller stopped during attempt reservation")
        write_new(attempt / "attempt.json", {"plan": plan, "inputs": inputs, "tool_aliases": aliases,
                                            "claim": "developer correctness reproduction; not independent tool/loader/backend admission"})
        for name in ("output", "run", "run/normal", "run/perf", "run/aot", "home", "tmp", "cache"):
            (directory / name).mkdir()
        while not controller_signals and (stage := next_stage(plan, results)) is not None:
            raising_stops = True
            if controller_signals:
                break  # A stop may have arrived while the condition was evaluated.
            receipt = {"name": stage["name"], "status": "not_launched"}
            try:
                if check_sources(directory, manifest, stage_names) != sources:
                    raise ValueError("source or backend filesystem identity changed")
                for name, expected in {**inputs, **artifacts}.items():
                    if name not in sources and file_identity(Path(name)) != expected:
                        raise ValueError(f"input or prior output changed: {name}")
                for name, resolved in aliases.items():
                    if str(Path(name).resolve(strict=True)) != resolved:
                        raise ValueError(f"tool alias changed: {name}")
                artifact = Path(stage["artifact"]) if stage["artifact"] else None
                if artifact is not None and (artifact.exists() or artifact.is_symlink()):
                    raise FileExistsError(f"stage output already exists: {artifact}")
                write_new(attempt / (stage["name"] + "-launch.json"), stage)
                with (attempt / (stage["name"] + ".stdout")).open("xb") as out, (attempt / (stage["name"] + ".stderr")).open("xb") as err:
                    execution = owned_process.run(stage["argv"], Path(stage["cwd"]), stage["environment"], out, err, stage["limit_seconds"])
                try:
                    receipt["execution"] = json.loads(json.dumps(execution, allow_nan=False))
                except ValueError:
                    receipt["invalid_execution"] = repr(execution)
                    raise
                require_success(execution, stage["limit_seconds"])
                if artifact is not None:
                    value = file_identity(artifact)
                    if not value["bytes"] or (stage["name"] != "jit_normal" and not os.access(artifact, os.X_OK)):
                        raise ValueError(f"stage did not produce a nonempty usable output: {artifact}")
                    artifacts[str(artifact)] = value
                    receipt["artifact"] = {"path": str(artifact), **value}
                check_stage_outputs(stage, directory, attempt)
                receipt["status"] = "passed"
            except BaseException as error:
                receipt["status"] = "failed"
                receipt["error"] = f"{type(error).__name__}: {error}"
            finally:
                # Only the supervisor handles child termination. After it
                # returns, defer outer stops while preserving its evidence.
                raising_stops = False
                try:
                    current = check_sources(directory, manifest, stage_names)
                    for name, expected in {**inputs, **artifacts}.items():
                        value = current[name] if name in current else file_identity(Path(name))
                        if value != expected:
                            raise ValueError(f"input or output changed after stage: {name}")
                    for name, resolved in aliases.items():
                        if str(Path(name).resolve(strict=True)) != resolved:
                            raise ValueError(f"tool alias changed after stage: {name}")
                    receipt["inputs_unchanged"] = True
                except BaseException as error:
                    receipt["status"] = "failed"
                    receipt["inputs_unchanged"] = False
                    receipt["postflight_error"] = f"{type(error).__name__}: {error}"
                if controller_signals:
                    receipt["status"] = "failed"
                receipt["controller_signals"] = list(controller_signals)
                results.append(receipt)
                write_new(attempt / (stage["name"] + "-result.json"), receipt)
                # A stop during that non-raising write is still terminal. It is
                # included in the overall receipt even if the stage was done.
    except BaseException as error:
        attempt_error = f"{type(error).__name__}: {error}"
    finally:
        raising_stops = False
        old_mask = None
        try:
            old_mask = signal.pthread_sigmask(signal.SIG_BLOCK, stop_signals)
        except BaseException as error:
            signal_cleanup_errors.append(f"block: {type(error).__name__}: {error}")
        for signum, handler in handlers.items():
            try:
                signal.signal(signum, handler)
            except BaseException as error:
                signal_cleanup_errors.append(f"restore: {type(error).__name__}: {error}")
        try:
            pending_stops = signal.sigpending()
            controller_signals.extend(signal.Signals(signum).name for signum in stop_signals
                                      if signum in pending_stops)
            # Terminal evidence boundary: no successor work remains. Success
            # below also requires every child's verified ownership cleanup.
            # Stops already observed/pending forbid success. Later queued stops
            # are delivered after this exclusive final receipt is committed.
            result = {"status": "passed" if not (attempt_error or controller_signals or signal_cleanup_errors)
                      and len(results) == len(plan) and all(row["status"] == "passed" for row in results) else "failed",
                      "stages": results, "not_run": [stage["name"] for stage in plan[len(results):]],
                      "controller_signals": controller_signals, "signal_cleanup_errors": signal_cleanup_errors,
                      "claim": "source-native developer correctness reproduction only; no throughput or compiler/toolchain/backend admission"}
            if attempt_error:
                result["attempt_error"] = attempt_error
            if attempt_owned:
                write_new(attempt / "result.json", result)
            else:
                raise RuntimeError(attempt_error or "native attempt directory was not reserved")
        finally:
            if old_mask is not None:
                # Failure propagates, so a committed receipt alone is not enough:
                # success also requires this controller to return/exit normally.
                signal.pthread_sigmask(signal.SIG_SETMASK, old_mask)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--seed", required=True, type=Path)
    parser.add_argument("--llvm-bin", required=True, type=Path)
    parser.add_argument("--cc", required=True, type=Path)
    parser.add_argument("--library-dir", action="append", default=[], type=Path)
    args = parser.parse_args()
    try:
        result = run(args.directory, args.seed, args.llvm_bin, args.cc, args.library_dir)
    except (OSError, ValueError) as error:
        parser.exit(1, f"native reproduction: {error}\n")
    print(json.dumps(result, allow_nan=False))
    sys.exit(0 if result["status"] == "passed" else 1)
