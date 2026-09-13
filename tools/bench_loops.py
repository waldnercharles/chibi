#!/usr/bin/env python3
"""Measure checked whole-function counted loops against C emitted from the same IR."""
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
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=100_000)
    parser.add_argument("--length", type=int, default=64)
    parser.add_argument("--max-native-ratio", type=float, default=1.5, help="SLJIT/generated-C optimization target")
    parser.add_argument("--enforce-native-ratio", action="store_true", help="fail when a measured ratio exceeds its target")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.trials < 3 or args.iterations < 100_000 or not math.isfinite(args.max_native_ratio) or args.max_native_ratio <= 0:
        parser.error("use at least three trials, 100000 iterations and a positive ratio")
    if not 1 <= args.length <= 10000:
        parser.error("use a loop length between 1 and 10000")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    cache = (build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized build with sanitizers disabled")
    binary = build / "test_suite"
    measurements = {}
    logs = []
    with tempfile.TemporaryDirectory(prefix="loop-c-") as temp:
        source = Path(temp) / "generated.c"
        library = Path(temp) / "generated.so"
        subprocess.run([binary, "--emit-loop-c", source], check=True, cwd=root)
        subprocess.run([
            compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
            "-I" + str(root / "src"), str(source), "-o", str(library)
        ], check=True, cwd=root)
        for trial in range(args.trials):
            result = subprocess.check_output([
                binary, "--bench-loop", library, str(args.iterations), str(trial % 3), str(args.length)
            ], cwd=root, text=True)
            logs.append(result)
            print(f"trial={trial}\n{result}", end="", flush=True)
            for label, value in re.findall(r"^(.+?) ns=([0-9.]+) checksum=", result, re.M):
                measurements.setdefault(label, []).append(float(value))
    medians = {label: statistics.median(values) for label, values in measurements.items()}
    for label, values in measurements.items():
        if len(values) != args.trials or any(not math.isfinite(v) or v <= 0 for v in values):
            raise SystemExit(f"FAIL: incomplete or invalid measurements for {label}")
        print(f"{label}: median={medians[label]:.3f} ns range={min(values):.3f}..{max(values):.3f}")
    ratio = medians["SLJIT loop_body"] / medians["GENERATED_C loop_body"]
    print(f"SLJIT/generated-C ratio={ratio:.3f}; target={args.max_native_ratio:.3f}")
    if args.output:
        args.output.write_text(json.dumps({
            "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "compiler": subprocess.check_output([compiler, "--version"], text=True).strip(),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "loop_length": args.length,
            "iterations": args.iterations, "trials": logs, "nanoseconds": measurements,
            "medians": medians, "native_c_ratio": ratio, "target_ratio": args.max_native_ratio,
            "target_met": ratio <= args.max_native_ratio, "target_enforced": args.enforce_native_ratio,
        }, indent=2) + "\n")
    if ratio > args.max_native_ratio and args.enforce_native_ratio:
        raise SystemExit("FAIL: whole loop performance acceptance")
    print("TARGET MET: whole loop performance" if ratio <= args.max_native_ratio
          else "TARGET MISSED: whole loop performance; optimization remains open")


if __name__ == "__main__":
    main()
