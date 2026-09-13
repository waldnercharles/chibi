#!/usr/bin/env python3
"""Pair optimized executables on deferred entity destruction, excluding setup."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from bench_arena_scale import digest


TIMING = re.compile(r"^behavior_destroy entities=(\d+) attachments=(\d+) ms=([0-9.]+) oracle_calls=0$", re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--counts", type=int, nargs="+", default=[1000, 5000, 20000])
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or any(not 1 <= n <= 20000 for n in args.counts):
        parser.error("use at least three trials and 1..20000 entities")
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    for path in binaries.values():
        cache = (path.parent / "CMakeCache.txt").read_text()
        if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
                r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M):
            parser.error("use optimized builds without sanitizers")
    hashes = {v: digest(p) for v, p in binaries.items()}
    env = dict(os.environ, MallocNanoZone="0")
    samples = []
    root = Path(__file__).resolve().parent.parent
    for count in args.counts:
        for trial in range(args.trials):
            for version in (("before", "after") if trial % 2 == 0 else ("after", "before")):
                result = subprocess.run([binaries[version], "--bench-behavior-destroy", str(count)],
                    cwd=root, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                if result.returncode:
                    raise RuntimeError(f"{version} failed ({result.returncode}):\n{result.stdout}")
                matches = TIMING.findall(result.stdout)
                if len(matches) != 1 or tuple(map(int, matches[0][:2])) != (count, count * 5):
                    raise ValueError(f"wrong workload or missing measurement: {result.stdout}")
                ms = float(matches[0][2])
                samples.append({"count": count, "trial": trial, "version": version, "ms": ms})
                print(f"count={count} trial={trial + 1} {version} ms={ms:.6f}", flush=True)
    if hashes != {v: digest(p) for v, p in binaries.items()}:
        raise ValueError("binary changed during measurement")
    summary = {str(n): {v: statistics.median(s["ms"] for s in samples
        if s["count"] == n and s["version"] == v) for v in binaries} for n in args.counts}
    args.output.write_text(json.dumps({"binary_sha256": hashes, "trials": args.trials,
        "workload": "Deferred host destruction; five idle behavior attachments per entity; "
                    "no components or rendering; instance creation and final pool release excluded",
        "summary_ms": summary, "samples": samples}, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
