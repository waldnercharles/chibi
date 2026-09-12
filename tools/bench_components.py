#!/usr/bin/env python3
"""Measure whole-function memory operations against C emitted from the same IR."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-performance")
    parser.add_argument("--workload", choices=("component", "alias", "store"), default="component")
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--iterations", type=int, default=2_000_000)
    parser.add_argument("--max-native-ratio", type=float, default=1.5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.trials < 3 or args.iterations < 100_000 or args.max_native_ratio <= 0:
        parser.error("use at least three trials, 100000 iterations and a positive ratio")
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
    with tempfile.TemporaryDirectory(prefix="component-c-") as temp:
        source = Path(temp) / "generated.c"
        library = Path(temp) / "generated.so"
        subprocess.run([binary, f"--emit-{args.workload}-c", source], check=True, cwd=root)
        subprocess.run([
            compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
            "-I" + str(root / "src"), str(source), "-o", str(library)
        ], check=True, cwd=root)
        for trial in range(args.trials):
            result = subprocess.check_output([
                binary, f"--bench-{args.workload}", library, str(args.iterations), str(trial % 3)
            ], cwd=root, text=True)
            logs.append(result)
            print(f"trial={trial}\n{result}", end="", flush=True)
            for label, value in re.findall(r"^(.+?) ns=([0-9.]+) checksum=", result, re.M):
                measurements.setdefault(label, []).append(float(value))
    medians = {label: statistics.median(values) for label, values in measurements.items()}
    for label, values in measurements.items():
        print(f"{label}: median={medians[label]:.3f} ns range={min(values):.3f}..{max(values):.3f}")
    bodies = ("store_body", "store_dynamic_body") if args.workload == "store" else (f"{args.workload}_body",)
    ratios = {body: medians[f"SLJIT {body}"] / medians[f"GENERATED_C {body}"] for body in bodies}
    ratio = max(ratios.values())
    for body, value in ratios.items():
        print(f"{body} SLJIT/generated-C ratio={value:.3f}; limit={args.max_native_ratio:.3f}")
    if args.output:
        args.output.write_text(json.dumps({
            "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
            "compiler": subprocess.check_output([compiler, "--version"], text=True).strip(),
            "iterations": args.iterations, "trials": logs, "nanoseconds": measurements,
            "workload": args.workload,
            "native_c_ratios": ratios,
            "medians": medians, "native_c_ratio": ratio, "maximum_ratio": args.max_native_ratio,
        }, indent=2) + "\n")
    if ratio > args.max_native_ratio:
        raise SystemExit(f"FAIL: whole {args.workload} performance acceptance")
    print(f"PASS: whole {args.workload} performance acceptance")


if __name__ == "__main__":
    main()
