#!/usr/bin/env python3
"""Pair native executables on real ArenaEnemy Tick workloads at increasing counts."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess


TIMING = re.compile(
    r"^scale backend=(SLJIT|BYTECODE) entities=(\d+) frames=(\d+) profile=0 "
    r"tick_p50_ms=([0-9.]+) tick_p95_ms=([0-9.]+) ns_per_entity=([0-9.]+) "
    r"batches=(\d+) checksum=(-?[0-9.]+) peak_rss_mb=([0-9.]+) oracle_calls=0$", re.M)
SETUP = re.compile(
    r"^scale_setup backend=(SLJIT|BYTECODE) entities=(\d+) attachments=(\d+) "
    r"setup_ms=([0-9.]+) peak_rss_mb=([0-9.]+) peak_growth_mb=([0-9.]+)$", re.M)
PHASES = re.compile(
    r"^scale_phases update_p50_ms=([0-9.]+) pump_p50_ms=([0-9.]+) "
    r"first_tick_ms=([0-9.]+)$", re.M)
CLOCKS = re.compile(
    r"^scale_clocks clock_p50_ms=([0-9.]+) complete_p50_ms=([0-9.]+) "
    r"complete_p95_ms=([0-9.]+)$", re.M)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, default=Path("build-performance/test_suite"))
    parser.add_argument("--counts", type=int, nargs="+", default=[100, 1000, 9999])
    parser.add_argument("--backends", nargs="+", choices=("SLJIT", "BYTECODE"), default=["SLJIT", "BYTECODE"])
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--trials", type=int, default=3)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--clocks", action="store_true", help="include engine clock propagation")
    args = parser.parse_args()
    if args.trials < 3 or args.frames < 8 or any(n < 1 for n in args.counts):
        parser.error("use at least three trials, eight frames and positive entity counts")
    root = Path(__file__).resolve().parent.parent
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    for path in binaries.values():
        cache = (path.parent / "CMakeCache.txt").read_text()
        if "CHIBI_ENABLE_SANITIZERS:BOOL=OFF" not in cache or not re.search(
                r"^CMAKE_BUILD_TYPE:STRING=(Release|RelWithDebInfo)$", cache, re.M):
            parser.error("use optimized builds with sanitizers disabled")
    hashes = {k: digest(p) for k, p in binaries.items()}
    content = hashlib.sha256()
    for path in sorted((root / "content").rglob("*.json")):
        content.update(str(path.relative_to(root)).encode())
        content.update(path.read_bytes())
    env = {k: v for k, v in os.environ.items() if not k.startswith("CBLUEPRINT_BENCH_")}
    env["MallocNanoZone"] = "0"
    samples, checksums = [], {}
    for count in args.counts:
        for trial in range(args.trials):
            for backend in (args.backends if trial % 2 == 0 else reversed(args.backends)):
                portable = int(backend == "BYTECODE")
                for version in (("before", "after") if trial % 2 == 0 else ("after", "before")):
                    result = subprocess.run([binaries[version],
                        "--bench-arena-clock" if args.clocks else "--bench-arena-scale",
                        str(count), str(args.frames), str(portable), "0"], cwd=root,
                        env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    if result.returncode:
                        raise SystemExit(f"Incomplete scaling acceptance: {version} {backend} {count}\n{result.stdout}")
                    timing, setup = TIMING.findall(result.stdout), SETUP.findall(result.stdout)
                    if len(timing) != 1 or len(setup) != 1:
                        raise ValueError("missing or duplicate measurements")
                    t, s = timing[0], setup[0]
                    if (t[0], int(t[1]), int(t[2])) != (backend, count, args.frames):
                        raise ValueError("wrong backend, count or sampling window")
                    if (s[0], int(s[1]), int(s[2])) != (backend, count, count * 5):
                        raise ValueError("missing authored attachments")
                    if not int(t[6]) or t[7] != checksums.setdefault(count, t[7]):
                        raise ValueError("batch execution or backend state mismatch")
                    metrics = dict(zip(("tick_p50_ms", "tick_p95_ms", "ns_per_entity"), map(float, t[3:6])))
                    metrics.update(peak_rss_mb=float(t[8]), setup_ms=float(s[3]), peak_growth_mb=float(s[5]))
                    if any(not math.isfinite(v) or v <= 0 for v in metrics.values()):
                        raise ValueError("invalid measurement")
                    phases = PHASES.findall(result.stdout)
                    if len(phases) > 1:
                        raise ValueError("duplicate phase measurements")
                    if phases:
                        metrics.update(zip(("update_p50_ms", "pump_p50_ms", "first_tick_ms"),
                                           map(float, phases[0])))
                    if args.clocks:
                        clocks = CLOCKS.findall(result.stdout)
                        if len(clocks) != 1:
                            raise ValueError("missing or duplicate clock measurements")
                        metrics.update(zip(("clock_p50_ms", "complete_p50_ms", "complete_p95_ms"),
                                           map(float, clocks[0])))
                    if any(not math.isfinite(v) or v < 0 for v in metrics.values()):
                        raise ValueError("invalid measurement")
                    samples.append({"count": count, "trial": trial, "backend": backend,
                                    "version": version, "metrics": metrics, "log": result.stdout})
                    print(f"{count} trial={trial + 1} {version} {backend} "
                          f"tick_p50_ms={metrics['tick_p50_ms']:.6f}", flush=True)
    if hashes != {k: digest(p) for k, p in binaries.items()}:
        raise ValueError("executables changed during measurement")
    summary = {}
    for count in args.counts:
        for backend in args.backends:
            values = {v: [s for s in samples if s["count"] == count and
                          s["backend"] == backend and s["version"] == v] for v in binaries}
            row = {v: {m: statistics.median(s["metrics"][m] for s in group)
                       for m in group[0]["metrics"]} for v, group in values.items()}
            row["speedup"] = row["before"]["tick_p50_ms"] / row["after"]["tick_p50_ms"]
            row["paired_p50_delta_ms"] = statistics.median(
                b["metrics"]["tick_p50_ms"] - a["metrics"]["tick_p50_ms"]
                for a, b in zip(values["before"], values["after"]))
            summary[f"{backend}/{count}"] = row
    args.output.write_text(json.dumps({"binary_sha256": hashes, "content_sha256": content.hexdigest(),
        "frames": args.frames, "warmup_frames": 8, "trials": args.trials, "backends": args.backends,
        "workload": "ArenaEnemy components and five attachments; fixed positions; no physics or rendering",
        "profiling": False, "include_clocks": args.clocks,
        "summary": summary, "samples": samples}, indent=2) + "\n")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
