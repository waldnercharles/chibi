#!/usr/bin/env python3
"""Compare whole motion functions emitted from the same lowered program."""
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
    parser.add_argument("--count", type=int, default=500000)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--max-native-ratio", type=float, default=1.5)
    parser.add_argument("--enforce-native-ratio", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (args.trials < 3 or args.count < 1000 or not math.isfinite(args.max_native_ratio)
            or args.max_native_ratio <= 0):
        parser.error("use at least three trials, 1000 calls and a positive ratio")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    cache = (build / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized build with sanitizers disabled")
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    binary = build / "test_suite"
    modes = ("SLJIT", "GENERATED_C", "BYTECODE")
    samples = {f"motion{k}": {mode: [] for mode in modes} for k in range(7)}
    logs = []
    pattern = re.compile(r"^(\w+) (motion\d+) ns=([0-9.]+) checksum=([0-9.]+)$", re.M)
    with tempfile.TemporaryDirectory(prefix="motion-c-") as temp:
        source, library = Path(temp) / "generated.c", Path(temp) / "generated.so"
        subprocess.run([binary, "--emit-motion-c", source], cwd=root, check=True)
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        subprocess.run([compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
                        "-I" + str(root / "src"), str(source), "-o", str(library)], check=True)
        subprocess.run([binary, "--whole-motion", library], cwd=root, check=True)
        for trial in range(args.trials):
            output = subprocess.check_output([binary, "--bench-motion", library,
                str(args.count), str(trial % 3)], cwd=root, text=True)
            print(f"trial={trial}\n{output}", end="", flush=True)
            logs.append(output)
            expected = {(kind, mode) for kind in samples for mode in modes}
            checksums = {}
            for mode, kind, ns, checksum in pattern.findall(output):
                if (kind, mode) not in expected:
                    raise SystemExit("FAIL: unexpected or duplicate backend measurement")
                expected.remove((kind, mode))
                value = float(ns)
                if not math.isfinite(value) or value <= 0 or not math.isfinite(float(checksum)):
                    raise SystemExit("FAIL: invalid measurement")
                if checksum != checksums.setdefault(kind, checksum):
                    raise SystemExit("FAIL: state mismatch")
                samples[kind][mode].append(value)
            if expected:
                raise SystemExit(f"FAIL: missing measurements {expected}")
    summary = {kind: {mode: {"median_ns": statistics.median(values), "min_ns": min(values),
                            "max_ns": max(values)} for mode, values in backends.items()}
               for kind, backends in samples.items()}
    ratios = {kind: result["SLJIT"]["median_ns"] / result["GENERATED_C"]["median_ns"]
              for kind, result in summary.items()}
    met = all(ratio <= args.max_native_ratio for ratio in ratios.values())
    for kind, ratio in ratios.items():
        print(f"{kind} SLJIT/generated-C={ratio:.3f}; target={args.max_native_ratio:.3f}")
    if args.output:
        args.output.write_text(json.dumps({
            "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "compiler": compiler, "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(), "source_sha256": source_hash, "count": args.count,
            "trials": args.trials, "summary": summary, "samples": samples, "logs": logs,
            "native_c_ratios": ratios, "target_met": met, "target": args.max_native_ratio,
            "strict": args.enforce_native_ratio,
        }, indent=2) + "\n")
    if not met:
        print("Native/C target missed; continue profiling this workload.")
        if args.enforce_native_ratio:
            raise SystemExit(1)


if __name__ == "__main__":
    main()
