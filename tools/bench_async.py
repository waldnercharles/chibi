#!/usr/bin/env python3
"""Compare nested asynchronous behavior activations through identical host services."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-performance")
    parser.add_argument("--count", type=int, default=20000)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--max-native-ratio", type=float, default=1.5)
    parser.add_argument("--enforce-native-ratio", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (args.trials < 3 or args.count < 100 or not math.isfinite(args.max_native_ratio)
            or args.max_native_ratio <= 0):
        parser.error("use at least three trials, 100 activations and a positive ratio")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    cache = (build / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized build with sanitizers disabled")
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    binary = build / "test_suite"
    samples = {mode: [] for mode in ("SLJIT", "BYTECODE", "GENERATED_C", "ORACLE")}
    logs = []
    pattern = re.compile(r"^(\w+) async_activation ns=([0-9.]+) checksum=(\d+) runtime_stores=(\d+)$", re.M)
    with tempfile.TemporaryDirectory(prefix="async-c-") as temp:
        source, library = Path(temp) / "generated.c", Path(temp) / "generated.so"
        subprocess.run([binary, "--emit-async-c", source], cwd=root, check=True)
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        subprocess.run([compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
                        "-I" + str(root / "src"), str(source), "-o", str(library)], check=True)
        subprocess.run([binary, "--whole-async", library], cwd=root, check=True)
        for trial in range(args.trials):
            output = subprocess.check_output([binary, "--bench-async", library,
                str(args.count), str(trial % 4)], cwd=root, text=True)
            print(f"trial={trial}\n{output}", end="", flush=True)
            logs.append(output)
            expected = set(samples)
            for mode, ns, checksum, stores in pattern.findall(output):
                if mode not in expected:
                    raise SystemExit("FAIL: unexpected or duplicate backend measurement")
                expected.remove(mode)
                value = float(ns)
                if not math.isfinite(value) or value <= 0 or int(checksum) != args.count * 102:
                    raise SystemExit("FAIL: invalid measurement or state mismatch")
                if mode != "ORACLE" and int(stores):
                    raise SystemExit("FAIL: compiled activation storage did not reuse")
                samples[mode].append(value)
            if expected:
                raise SystemExit(f"FAIL: missing measurements {expected}")
    summary = {mode: {"median_ns": statistics.median(values), "min_ns": min(values),
                      "max_ns": max(values)} for mode, values in samples.items()}
    ratio = summary["SLJIT"]["median_ns"] / summary["GENERATED_C"]["median_ns"]
    met = ratio <= args.max_native_ratio
    print(f"SLJIT/generated-C={ratio:.3f}; target={args.max_native_ratio:.3f}; met={met}")
    if args.output:
        args.output.write_text(json.dumps({
            "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "compiler": compiler, "source_sha256": source_hash, "count": args.count,
            "trials": args.trials, "summary": summary, "samples": samples, "logs": logs,
            "native_c_ratio": ratio, "target_met": met, "target": args.max_native_ratio,
            "strict": args.enforce_native_ratio,
        }, indent=2) + "\n")
    if not met:
        print("Native/C target missed; continue profiling this workload.")
        if args.enforce_native_ratio:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
