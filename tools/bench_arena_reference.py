#!/usr/bin/env python3
"""Compare actual idle ArenaEnemy Tick execution with an independent C loop."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from bench_arena_scale import PHASES, TIMING, digest


REFERENCE = re.compile(
    r"^scale_reference c_p50_ms=([0-9.]+) c_p95_ms=([0-9.]+) "
    r"update_c_ratio=([0-9.]+) tick_c_ratio=([0-9.]+) snapshots=(\d+)$", re.M)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--counts", type=int, nargs="+", default=[100, 1000, 10000, 20000])
    parser.add_argument("--frames", type=int, default=256)
    parser.add_argument("--trials", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.trials < 3 or not 8 <= args.frames <= 10000 or any(not 1 <= n <= 20000 for n in args.counts):
        parser.error("use at least three trials, 8..10000 frames and 1..20000 entities")
    binary = args.binary.resolve()
    cache = (binary.parent / "CMakeCache.txt").read_text()
    if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
            r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M):
        parser.error("use an optimized build with sanitizers disabled")
    fingerprint = digest(binary)
    root = Path(__file__).resolve().parent.parent
    env = {k: v for k, v in os.environ.items() if not k.startswith("CBLUEPRINT_BENCH_")}
    env["MallocNanoZone"] = "0"
    samples = []
    for trial in range(args.trials):
        for count in (args.counts if trial % 2 == 0 else reversed(args.counts)):
            log = subprocess.check_output([binary, "--bench-arena-reference", str(count),
                                           str(args.frames), "0", "0"],
                                          cwd=root, env=env, text=True, stderr=subprocess.STDOUT)
            refs, timings, phases = REFERENCE.findall(log), TIMING.findall(log), PHASES.findall(log)
            if len(refs) != 1 or len(timings) != 1 or len(phases) != 1:
                raise ValueError("missing or duplicate timing/state checks")
            r, t, p = refs[0], timings[0], phases[0]
            if (t[0], int(t[1]), int(t[2]), int(r[4])) != (
                    "SLJIT", count, args.frames, count * (args.frames + 8)):
                raise ValueError("wrong backend, count or snapshot coverage")
            metrics = dict(zip(("c_p50_ms", "c_p95_ms", "update_c_ratio", "tick_c_ratio"), map(float, r[:4])))
            metrics.update(zip(("tick_p50_ms", "tick_p95_ms"), map(float, t[3:5])))
            metrics.update(zip(("update_p50_ms", "pump_p50_ms", "first_tick_ms"), map(float, p)))
            samples.append({"trial": trial, "count": count, "metrics": metrics, "log": log})
            print(f"trial={trial + 1} count={count} "
                  f"update={p[0]} ms C={r[0]} ms ratio={r[2]}", flush=True)
    if fingerprint != digest(binary):
        raise ValueError("binary changed during measurement")
    summary = {str(n): {key: statistics.median(s["metrics"][key] for s in samples if s["count"] == n)
                        for key in samples[0]["metrics"]} for n in args.counts}
    args.output.write_text(json.dumps({"binary_sha256": fingerprint, "frames": args.frames,
        "trials": args.trials, "warmup_frames": 8,
        "workload": "Fixed ArenaEnemy positions, all five attachments, inactive feedback, no physics/rendering",
        "reference": "Independent C with prebound component pointers and one nearest query per entity",
        "order": "Alternate native/C phase order each frame; verify every velocity outside timing",
        "summary": summary, "samples": samples}, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
