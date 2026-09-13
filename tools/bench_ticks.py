#!/usr/bin/env python3
"""Measure ordered Tick broadcasts with ordinary activations, batches and generated C."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import tempfile


MODES = ("SLJIT_ACTIVATION", "BYTECODE_ACTIVATION", "SLJIT_BATCH",
         "BYTECODE_BATCH", "GENERATED_C_BATCH", "ORACLE",
         "SLJIT_UPDATE", "BYTECODE_UPDATE", "GENERATED_C_UPDATE", "SLJIT_C_UPDATE")


WORKLOADS = ("counter", "component", "int_array", "string_array", "name_array", "struct_array")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default="build-performance")
    parser.add_argument("--before", type=Path, help="pair a saved optimized executable with the current build")
    parser.add_argument("--entities", type=int, nargs="+", default=[64, 512, 4096])
    parser.add_argument("--frames", type=int, default=128)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--max-native-ratio", type=float, default=1.5)
    parser.add_argument("--enforce-native-ratio", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if (args.trials < 3 or args.frames < 16 or any(n < 1 for n in args.entities)
            or len(set(args.entities)) != len(args.entities)
            or not math.isfinite(args.max_native_ratio) or args.max_native_ratio <= 0):
        parser.error("use at least three trials, 16 frames, distinct positive entity counts and a positive ratio")
    root = Path(__file__).resolve().parent.parent
    build = (root / args.build_dir).resolve()
    cache = (build / "CMakeCache.txt").read_text()
    compiler = re.search(r"^CMAKE_C_COMPILER:[^=]+=(.+)$", cache, re.M).group(1)
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
        r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M
    ):
        parser.error("use an optimized build with sanitizers disabled")
    binary = build / "test_suite"
    binaries = {"after": binary}
    if args.before:
        binaries["before"] = args.before.resolve()
        baseline_cache = (binaries["before"].parent / "CMakeCache.txt").read_text()
        if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in baseline_cache or not re.search(
            r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", baseline_cache, re.M
        ):
            parser.error("the saved executable must come from an optimized build with sanitizers disabled")
    hashes = {version: hashlib.sha256(path.read_bytes()).hexdigest() for version, path in binaries.items()}
    samples, logs, ratios, summaries = {}, [], {}, {}
    before_samples, before_summary, paired = {}, {}, {}
    source_hashes, libraries, checksums = {}, {}, {}
    pattern = re.compile(
        r"^(\w+) (counter|component|int_array|string_array|name_array|struct_array) entities=(\d+) ns=([0-9.]+) "
        r"checksum=(\d+) batches=(\d+) vm_allocations=(\d+) runtime_stores=(\d+)(?: arena_blocks=(\d+))?$", re.M)
    with tempfile.TemporaryDirectory(prefix="tick-c-") as temp:
        for version, executable in binaries.items():
            source, library = Path(temp) / f"{version}.c", Path(temp) / f"{version}.so"
            subprocess.run([executable, "--emit-batch-c", source], check=True, cwd=root)
            source_hashes[version] = hashlib.sha256(source.read_bytes()).hexdigest()
            subprocess.run([
                compiler, "-std=c23", "-O3", "-ffp-contract=off", "-shared", "-fPIC",
                "-I" + str(root / "src"), str(source), "-o", str(library)
            ], check=True, cwd=root)
            libraries[version] = library
        for trial in range(args.trials):
            for count in args.entities:
                versions = list(binaries) if trial % 2 else list(reversed(binaries))
                for version in versions:
                    result = subprocess.check_output([
                        binaries[version], "--bench-batch", libraries[version], str(count),
                        str(args.frames), str(trial % len(MODES))
                    ], cwd=root, text=True)
                    logs.append({"trial": trial, "version": version, "entities": count, "output": result})
                    print(f"trial={trial} version={version} entities={count}\n{result}", end="", flush=True)
                    expected = {(mode, workload) for mode in MODES for workload in WORKLOADS}
                    for mode, workload, entities, ns, checksum, batches, vm, stores, blocks in pattern.findall(result):
                        pair = (mode, workload)
                        if pair not in expected or int(entities) != count:
                            raise SystemExit("FAIL: duplicate or unexpected measurement")
                        expected.remove(pair)
                        value = float(ns)
                        if not math.isfinite(value) or value <= 0:
                            raise SystemExit("FAIL: invalid timing")
                        if checksums.setdefault((workload, count), checksum) != checksum:
                            raise SystemExit("FAIL: backend or executable state mismatch")
                        batched = mode.endswith(("_BATCH", "_UPDATE"))
                        if int(batches) != (count * args.frames if batched else 0) or (batched and (int(vm) or int(stores))):
                            raise SystemExit("FAIL: batch execution or scratch reuse mismatch")
                        if version == "after" and (not blocks or (batched and int(blocks))):
                            raise SystemExit("FAIL: missing allocation counts or warmed arena growth")
                        key = f"{workload}/{count}/{mode}"
                        target = samples if version == "after" else before_samples
                        target.setdefault(key, []).append(value)
                    if expected:
                        raise SystemExit(f"FAIL: missing measurements {expected}")
    if hashes != {version: hashlib.sha256(path.read_bytes()).hexdigest() for version, path in binaries.items()}:
        raise SystemExit("FAIL: an executable changed during measurement")
    for measurements, summary in ((samples, summaries), (before_samples, before_summary)):
        for key, values in measurements.items():
            if len(values) != args.trials:
                raise SystemExit(f"FAIL: incomplete measurements for {key}")
            summary[key] = {"median_ns": statistics.median(values), "min_ns": min(values), "max_ns": max(values)}
    for key, values in before_samples.items():
        deltas = [after - before for before, after in zip(values, samples[key])]
        paired[key] = {"speedup": before_summary[key]["median_ns"] / summaries[key]["median_ns"],
                       "deltas_ns": deltas, "median_delta_ns": statistics.median(deltas)}
    for workload in WORKLOADS:
        for count in args.entities:
            prefix = f"{workload}/{count}"
            times = {mode: summaries[f"{prefix}/{mode}"]["median_ns"] for mode in MODES}
            ratios[prefix] = times["SLJIT_UPDATE"] / times["GENERATED_C_UPDATE"]
            print(f"{prefix}: SLJIT update={times['SLJIT_UPDATE']:.3f} ns/entity "
                  f"speedup={times['SLJIT_BATCH'] / times['SLJIT_UPDATE']:.3f}x "
                  f"bytecode speedup={times['BYTECODE_BATCH'] / times['BYTECODE_UPDATE']:.3f}x "
                  f"SLJIT/generated-C={ratios[prefix]:.3f}")
    met = max(ratios.values()) <= args.max_native_ratio
    if args.output:
        args.output.write_text(json.dumps({
            "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
            "binary_sha256": hashes["after"], "paired_binary_sha256": hashes,
            "generated_c_sha256": source_hashes["after"], "paired_generated_c_sha256": source_hashes,
            "compiler": subprocess.check_output([compiler, "--version"], text=True).strip(),
            "entities": args.entities, "frames": args.frames, "trials": logs,
            "nanoseconds": samples, "summary": summaries, "native_c_ratios": ratios,
            "before_nanoseconds": before_samples, "before_summary": before_summary, "paired": paired,
            "target_ratio": args.max_native_ratio, "target_met": met,
            "target_enforced": args.enforce_native_ratio,
            "allocation_metric": "VM scratch and arena backing-block allocations; does not count every allocation",
            "comparison": "same lowered bodies and prepared rows; C bodies use a test-only quiescent entry replacement; generated C is compared through the reference C outer loop, SLJIT_UPDATE uses the native outer loop",
        }, indent=2) + "\n")
    if not met and args.enforce_native_ratio:
        raise SystemExit("FAIL: Tick batch performance target")
    print("TARGET MET: Tick batches" if met else "TARGET MISSED: Tick batches; optimization remains open")


if __name__ == "__main__":
    main()
