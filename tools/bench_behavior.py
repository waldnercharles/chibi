#!/usr/bin/env python3
"""Compare whole-function SLJIT with C emitted from the same lowered program."""
import argparse
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
    parser.add_argument("--max-native-ratio", type=float, default=1.5)
    args = parser.parse_args()
    if args.trials < 3 or args.iterations < 100_000 or args.max_native_ratio <= 0:
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
        print(f"{label}: median={statistics.median(values):.3f} ns "
              f"range={min(values):.3f}..{max(values):.3f}")
    ratio = statistics.median(measurements["SLJIT whole_function"]) / statistics.median(
        measurements["GENERATED_C whole_function"]
    )
    print(f"SLJIT/generated-C ratio={ratio:.3f}; limit={args.max_native_ratio:.3f}")
    dispatch_ratio = statistics.median(measurements["SLJIT dispatch"]) / statistics.median(
        measurements["C dispatch"]
    )
    print(f"SLJIT/C-scheduler dispatch ratio={dispatch_ratio:.3f}; limit=1.100")
    if dispatch_ratio > 1.1:
        raise SystemExit("FAIL: scalar dispatch performance acceptance")
    if ratio > args.max_native_ratio:
        raise SystemExit("FAIL: scalar whole-function performance acceptance")
    print("PASS: scalar whole-function performance acceptance")


if __name__ == "__main__":
    main()
