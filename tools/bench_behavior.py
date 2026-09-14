#!/usr/bin/env python3
"""Compare whole-function SLJIT with C emitted from the same lowered program."""
import argparse
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
    parser.add_argument("--iterations", type=int, default=2_000_000)
    parser.add_argument("--max-native-ratio", type=float, default=1.5, help="SLJIT/generated-C optimization target")
    parser.add_argument("--enforce-native-ratio", action="store_true", help="fail when a measured ratio exceeds its target")
    args = parser.parse_args()
    if args.trials < 3 or args.iterations < 100_000 or not math.isfinite(args.max_native_ratio) or args.max_native_ratio <= 0:
        parser.error("use at least three trials, 100000 iterations, and a positive ratio")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    cache = (build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized native build with sanitizers disabled")
    binary = build / "bench_behavior"
    measurements = {}
    with tempfile.TemporaryDirectory(prefix="behavior-c-") as temp:
        source = Path(temp) / "generated.c"
        library = Path(temp) / "generated.so"
        subprocess.run([binary, "--emit-c", source], check=True, cwd=root)
        subprocess.run([
            compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
            "-I" + str(root / "src"), str(source), "-o", str(library)
        ], check=True, cwd=root)
        for trial in range(args.trials):
            result = subprocess.check_output([
                binary, str(args.iterations), str(trial % 3), library
            ], cwd=root, text=True)
            print(f"trial={trial}\n{result}", end="", flush=True)
            for label, value in re.findall(r"^(.+?) ns=([0-9.]+) checksum=", result, re.M):
                measurements.setdefault(label, []).append(float(value))
    for label, values in measurements.items():
        if len(values) != args.trials or any(not math.isfinite(v) or v <= 0 for v in values):
            raise SystemExit(f"FAIL: incomplete or invalid measurements for {label}")
        print(f"{label}: median={statistics.median(values):.3f} ns "
              f"range={min(values):.3f}..{max(values):.3f}")
    ratio = statistics.median(measurements["SLJIT whole_function"]) / statistics.median(
        measurements["GENERATED_C whole_function"]
    )
    print(f"SLJIT/generated-C ratio={ratio:.3f}; target={args.max_native_ratio:.3f}")
    arguments_ratio = statistics.median(measurements["SLJIT arguments"]) / statistics.median(
        measurements["GENERATED_C arguments"]
    )
    print(f"SLJIT/generated-C argument/return ratio={arguments_ratio:.3f}; target={args.max_native_ratio:.3f}")
    calls_ratio = statistics.median(measurements["SLJIT calls"]) / statistics.median(
        measurements["GENERATED_C calls"]
    )
    print(f"SLJIT/generated-C inlined-call ratio={calls_ratio:.3f}; target={args.max_native_ratio:.3f}")
    pure_ratio = statistics.median(measurements["SLJIT pure_body"]) / statistics.median(
        measurements["GENERATED_C pure_body"]
    )
    print(f"SLJIT/generated-C pure-body ratio={pure_ratio:.3f}; target={args.max_native_ratio:.3f}")
    math_ratio = statistics.median(measurements["SLJIT math_body"]) / statistics.median(
        measurements["GENERATED_C math_body"]
    )
    print(f"SLJIT/generated-C math-body ratio={math_ratio:.3f}; target={args.max_native_ratio:.3f}")
    aggregate_ratio = statistics.median(measurements["SLJIT aggregate_body"]) / statistics.median(
        measurements["GENERATED_C aggregate_body"]
    )
    print(f"SLJIT/generated-C aggregate-body ratio={aggregate_ratio:.3f}; target={args.max_native_ratio:.3f}")
    integer_ratios = []
    for width in (32, 64):
        integer_ratio = statistics.median(measurements[f"SLJIT integer{width}_body"]) / statistics.median(
            measurements[f"GENERATED_C integer{width}_body"]
        )
        integer_ratios.append(integer_ratio)
        print(f"SLJIT/generated-C integer{width}-body ratio={integer_ratio:.3f}; target={args.max_native_ratio:.3f}")
    dispatch_ratio = statistics.median(measurements["SLJIT dispatch"]) / statistics.median(
        measurements["C dispatch"]
    )
    print(f"SLJIT/C-scheduler dispatch ratio={dispatch_ratio:.3f}; target=1.100")
    ratios = {
        "dispatch": (dispatch_ratio, 1.1),
        "whole function": (ratio, args.max_native_ratio),
        "argument/return": (arguments_ratio, args.max_native_ratio),
        "inlined call": (calls_ratio, args.max_native_ratio),
        "pure body": (pure_ratio, args.max_native_ratio),
        "math body": (math_ratio, args.max_native_ratio),
        "aggregate body": (aggregate_ratio, args.max_native_ratio),
        "integer body": (max(integer_ratios), args.max_native_ratio),
    }
    missed = [label for label, (value, target) in ratios.items() if value > target]
    if missed:
        message = "TARGET MISSED: " + ", ".join(missed) + "; optimization remains open"
        if args.enforce_native_ratio:
            raise SystemExit("FAIL: " + message)
        print(message)
    else:
        print("TARGET MET: whole-function and dispatch performance")


if __name__ == "__main__":
    main()
